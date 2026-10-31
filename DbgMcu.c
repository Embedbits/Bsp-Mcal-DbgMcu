/**
 * \author Mr.Nobody
 * \file DbgMcu.c
 * \ingroup DbgMcu
 * \brief DbgMcu module common functionality
 *
 * # Introduction #
 * The microcontroller debug unit (DBGMCU) of STM32H5 holds the identity code of the device,
 * the debug in the low-power modes (DBGMCU_CR), the freeze registers of the peripherals (they
 * stop the peripherals while the core is halted by the debugger) and the trace pin assignment.
 * The registers are reset only by a power-on reset and are accessed by the software at the
 * base address of the APB3 peripherals. The DBGMCU is clocked all the time, it has no
 * peripheral clock enable bit and no interrupt, the module therefore uses neither Rcc nor Nvic.
 *
 * # Identification #
 * The device identifier (DEV_ID, 12 bits) and the revision (REV_ID, 16 bits) are read from
 * DBGMCU_IDCODE. They do not depend on the initialization and can be read before \ref DbgMcu_Init.
 *
 * # Low-power debug #
 * DBG_STOP and DBG_STANDBY keep the clocks (and the core supply in the Standby mode) alive in the
 * mode, so the debugger keeps the connection. STM32H5 has no setting for the Sleep mode (the core is
 * clocked in the Sleep mode and the connection is kept by the hardware), the requests of
 * \ref DBGMCU_LP_SLEEP are refused. The consumption in the Stop and the Standby mode is higher, the
 * IO state is not retained in the Standby mode and the exit from the Standby mode is a system reset.
 *
 * # Peripheral freeze #
 * Every freezable peripheral has a bit DBG_xxx_STOP in a freeze register, the bits of the peripherals
 * (and of the DMA channels) which exist on the device line are the items of \ref dbgMcu_Periph_t. The
 * bit has an effect only while the core is halted by the debugger.
 *
 * # Access of the software #
 * The access of the software to DBGMCU_CR and to the freeze registers is ignored on a device with the
 * debug closed (RM0481, RM0517: accessible after a successful authentication), and the freeze bit of a
 * secure peripheral can not be changed while the secure invasive debug is disabled. Every write is
 * therefore verified by a read-back: a write which does not stay is an error.
 *
 */
/* ============================== INCLUDES ================================== */
#include "DbgMcu.h"                         /* Self include                   */
#include "DbgMcu_Port.h"                    /* Own port file include          */
#include "DbgMcu_Types.h"                   /* Module types definitions       */
#include "DbgMcu_Reg.h"                     /* Register access                */
#include "Stm32.h"                          /* DBGMCU registers definition    */
/* ============================== TYPEDEFS ================================== */

/** \brief Runtime data of the module */
typedef struct
{
    dbgMcu_FunctionState_t InitState; /**< The module is initialized (set by DbgMcu_Init) */
}   dbgMcu_RuntimeData_t;

/* ======================== FORWARD DECLARATIONS ============================ */

static dbgMcu_RequestState_t    DbgMcu_Check_Ready          ( void );
static dbgMcu_RequestState_t    DbgMcu_Check_FunctionState  ( dbgMcu_FunctionState_t state );
static dbgMcu_RequestState_t    DbgMcu_Check_Config         ( const dbgMcu_Config_t * const dbgMcuConfig );

static dbgMcu_RequestState_t    DbgMcu_Set_Config           ( const dbgMcu_Config_t * const dbgMcuConfig );
static dbgMcu_RequestState_t    DbgMcu_Set_ConfigRegister   ( const dbgMcu_Config_t * const dbgMcuConfig );
static dbgMcu_RequestState_t    DbgMcu_Set_FreezeTable      ( const dbgMcu_FunctionState_t * const freezeTable );

static dbgMcu_FunctionState_t   DbgMcu_Get_BitState         ( dbgMcu_RegId_t regId, uint32_t bitMask );
static dbgMcu_FlagState_t       DbgMcu_Get_FlagState        ( dbgMcu_RegId_t regId, uint32_t bitMask );

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Value of major version of SW module */
#define DBGMCU_MAJOR_VERSION            ( 0u )

/** Value of minor version of SW module */
#define DBGMCU_MINOR_VERSION            ( 1u )

/** Value of patch version of SW module */
#define DBGMCU_PATCH_VERSION            ( 0u )

/** Bits of DBGMCU_CR of the low-power debug (Stop and Standby mode) */
#define DBGMCU_CR_LOWPOWER_MASK         ( DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY )

/** Bits of DBGMCU_CR of the trace (clock, pin enable, pin assignment) */
#define DBGMCU_CR_TRACE_MASK            ( DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE )

/** Bits of DBGMCU_CR owned by the module, other bits (DCRT) are never written */
#define DBGMCU_CR_MODULE_MASK           ( DBGMCU_CR_LOWPOWER_MASK | DBGMCU_CR_TRACE_MASK )

/** Mask of the low-power mode without a bit in DBGMCU_CR (Sleep mode) */
#define DBGMCU_LP_BIT_NONE              ( 0u )

/** Value of the trace bits of DBGMCU_CR with the trace disabled */
#define DBGMCU_TRACE_BITS_NONE          ( 0u )

/* =============================== MACROS =================================== */

/** Converts the value of DBGMCU_IDCODE to the device identifier */
#define DBGMCU_REG_TO_DEVICE_ID( regVal )   ( ( ( regVal ) & DBGMCU_IDCODE_DEV_ID ) >> DBGMCU_IDCODE_DEV_ID_Pos )

/** Converts the value of DBGMCU_IDCODE to the silicon revision */
#define DBGMCU_REG_TO_REVISION_ID( regVal ) ( ( ( regVal ) & DBGMCU_IDCODE_REV_ID ) >> DBGMCU_IDCODE_REV_ID_Pos )

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

/** \brief Bits of DBGMCU_CR of the low-power modes, indexed by \ref dbgMcu_LowPower_t */
static const uint32_t dbgMcu_LowPowerBitLut[] =
{
    [ DBGMCU_LP_SLEEP   ] = DBGMCU_LP_BIT_NONE,       /**< Sleep mode: no setting on STM32H5 */
    [ DBGMCU_LP_STOP    ] = DBGMCU_CR_DBG_STOP,       /**< Stop mode                         */
    [ DBGMCU_LP_STANDBY ] = DBGMCU_CR_DBG_STANDBY,    /**< Standby mode                      */
};

_Static_assert( DBGMCU_LP_CNT == ( sizeof( dbgMcu_LowPowerBitLut ) / sizeof( uint32_t ) ),
                "DbgMcu: dbgMcu_LowPowerBitLut has incorrect size." );


/** \brief Bits of DBGMCU_CR of the trace modes, indexed by \ref dbgMcu_TraceMode_t */
static const uint32_t dbgMcu_TraceBitLut[] =
{
    /* Clock off, pins not assigned */
    [ DBGMCU_TRACE_DISABLED  ] = DBGMCU_TRACE_BITS_NONE,
    /* TRACE_MODE = 0 (asynchronous) */
    [ DBGMCU_TRACE_ASYNC     ] = ( DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN ),
    /* TRACE_MODE = 1 (synchronous, 1 bit) */
    [ DBGMCU_TRACE_SYNC_1BIT ] = ( DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE_0 ),
    /* TRACE_MODE = 2 (synchronous, 2 bits) */
    [ DBGMCU_TRACE_SYNC_2BIT ] = ( DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE_1 ),
    /* TRACE_MODE = 3 (synchronous, 4 bits) */
    [ DBGMCU_TRACE_SYNC_4BIT ] = ( DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE   ),
};

_Static_assert( DBGMCU_TRACE_CNT == ( sizeof( dbgMcu_TraceBitLut ) / sizeof( uint32_t ) ),
                "DbgMcu: dbgMcu_TraceBitLut has incorrect size." );


/** \brief Freeze bits of the peripherals, in the order of \ref dbgMcu_Periph_t
 *
 * The items (and their guards) correspond to the items of \ref dbgMcu_Periph_t, the freeze bits
 * of the CMSIS device header exist on exactly the lines (RM0481, RM0492, RM0517) which have the peripheral.
 */
static const dbgMcu_FreezeItem_t dbgMcu_FreezeLut[] =
{
    { .PeriphId = DBGMCU_PERIPH_IWDG,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_IWDG_STOP  },
    { .PeriphId = DBGMCU_PERIPH_WWDG,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_WWDG_STOP  },

    { .PeriphId = DBGMCU_PERIPH_TIM1,         .RegId = DBGMCU_REG_APB2FZR,  .BitMask = DBGMCU_APB2FZR_DBG_TIM1_STOP   },
    { .PeriphId = DBGMCU_PERIPH_TIM2,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM2_STOP  },
    { .PeriphId = DBGMCU_PERIPH_TIM3,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM3_STOP  },
#if defined(DBGMCU_APB1FZR1_DBG_TIM4_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM4,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM4_STOP  },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM5_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM5,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM5_STOP  },
#endif
    { .PeriphId = DBGMCU_PERIPH_TIM6,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM6_STOP  },
    { .PeriphId = DBGMCU_PERIPH_TIM7,         .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM7_STOP  },
#if defined(DBGMCU_APB2FZR_DBG_TIM8_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM8,         .RegId = DBGMCU_REG_APB2FZR,  .BitMask = DBGMCU_APB2FZR_DBG_TIM8_STOP   },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM12_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM12,        .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM12_STOP },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM13_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM13,        .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM13_STOP },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM14_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM14,        .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_TIM14_STOP },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM15_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM15,        .RegId = DBGMCU_REG_APB2FZR,  .BitMask = DBGMCU_APB2FZR_DBG_TIM15_STOP  },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM16_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM16,        .RegId = DBGMCU_REG_APB2FZR,  .BitMask = DBGMCU_APB2FZR_DBG_TIM16_STOP  },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM17_STOP)
    { .PeriphId = DBGMCU_PERIPH_TIM17,        .RegId = DBGMCU_REG_APB2FZR,  .BitMask = DBGMCU_APB2FZR_DBG_TIM17_STOP  },
#endif

    { .PeriphId = DBGMCU_PERIPH_LPTIM1,       .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_LPTIM1_STOP },
    { .PeriphId = DBGMCU_PERIPH_LPTIM2,       .RegId = DBGMCU_REG_APB1FZR2,
      .BitMask = DBGMCU_APB1FZR2_DBG_LPTIM2_STOP },
#if defined(DBGMCU_APB3FZR_DBG_LPTIM3_STOP)
    { .PeriphId = DBGMCU_PERIPH_LPTIM3,       .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_LPTIM3_STOP },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM4_STOP)
    { .PeriphId = DBGMCU_PERIPH_LPTIM4,       .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_LPTIM4_STOP },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM5_STOP)
    { .PeriphId = DBGMCU_PERIPH_LPTIM5,       .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_LPTIM5_STOP },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM6_STOP)
    { .PeriphId = DBGMCU_PERIPH_LPTIM6,       .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_LPTIM6_STOP },
#endif

    { .PeriphId = DBGMCU_PERIPH_RTC,          .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_RTC_STOP    },

    { .PeriphId = DBGMCU_PERIPH_I2C1_TIMEOUT, .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_I2C1_STOP  },
    { .PeriphId = DBGMCU_PERIPH_I2C2_TIMEOUT, .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_I2C2_STOP  },
#if defined(DBGMCU_APB3FZR_DBG_I2C3_STOP)
    { .PeriphId = DBGMCU_PERIPH_I2C3_TIMEOUT, .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_I2C3_STOP   },
#endif
#if defined(DBGMCU_APB3FZR_DBG_I2C4_STOP)
    { .PeriphId = DBGMCU_PERIPH_I2C4_TIMEOUT, .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_I2C4_STOP   },
#endif
    { .PeriphId = DBGMCU_PERIPH_I3C1_TIMEOUT, .RegId = DBGMCU_REG_APB1FZR1, .BitMask = DBGMCU_APB1FZR1_DBG_I3C1_STOP  },
#if defined(DBGMCU_APB3FZR_DBG_I3C2_STOP)
    { .PeriphId = DBGMCU_PERIPH_I3C2_TIMEOUT, .RegId = DBGMCU_REG_APB3FZR,  .BitMask = DBGMCU_APB3FZR_DBG_I3C2_STOP   },
#endif

    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH0,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH0_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH1,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH1_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH2,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH2_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH3,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH3_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH4,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH4_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH5,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH5_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH6,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH6_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH7,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH7_STOP },
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA1_CH8_STOP)
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH8,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH8_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH9,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH9_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH10,  .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH10_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA1_CH11,  .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA1_CH11_STOP },
#endif
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH0,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH0_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH1,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH1_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH2,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH2_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH3,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH3_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH4,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH4_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH5,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH5_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH6,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH6_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH7,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH7_STOP },
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA2_CH8_STOP)
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH8,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH8_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH9,   .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH9_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH10,  .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH10_STOP },
    { .PeriphId = DBGMCU_PERIPH_GPDMA2_CH11,  .RegId = DBGMCU_REG_AHB1FZR,
      .BitMask = DBGMCU_AHB1FZR_DBG_GPDMA2_CH11_STOP },
#endif
};

_Static_assert( DBGMCU_PERIPH_CNT == ( sizeof( dbgMcu_FreezeLut ) / sizeof( dbgMcu_FreezeItem_t ) ),
                "DbgMcu: dbgMcu_FreezeLut has incorrect size." );


/** \brief Runtime data of the module */
static volatile dbgMcu_RuntimeData_t dbgMcu_RuntimeData =
{
    .InitState = DBGMCU_FUNCTION_INACTIVE,
};

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Returns module SW version
 *
 * \return Module SW version
 */
dbgMcu_ModuleVersion_t DbgMcu_Get_ModuleVersion( void )
{
    dbgMcu_ModuleVersion_t retVersion;

    retVersion.Major = DBGMCU_MAJOR_VERSION;
    retVersion.Minor = DBGMCU_MINOR_VERSION;
    retVersion.Patch = DBGMCU_PATCH_VERSION;

    return (retVersion);
}


/**
 * \brief Initializes the module and applies the configuration.
 *
 * The configuration is checked completely before the first register is written. Then the
 * debug in the low-power modes and the trace (DBGMCU_CR) and the freeze bits of all peripherals
 * of the device line are written: a peripheral with \ref DBGMCU_FUNCTION_ACTIVE in the table
 * is frozen, the other ones run while the core is halted (their freeze bit is cleared). Every
 * written register is verified by a read-back.
 *
 * \note  The module owns the bits of DBGMCU_CR (low-power debug, trace) and the freeze bits,
 *        the bits set by the debugger or by other code are overwritten by the configuration.
 * \note  The registers are changed by the software only if the device allows it (debug open,
 *        see the description of the module), otherwise the read-back fails.
 *
 * \pre   Configuration shall be valid: not NULL, states and trace mode in their lists,
 *        \ref dbgMcu_Config_t::LowPowerSleep inactive (STM32H5 has no setting for the Sleep mode);
 *        otherwise \ref DBGMCU_REQUEST_ERROR is returned and nothing is changed. Failure of the
 *        read-back deinitializes the module (\ref DbgMcu_Deinit) and returns \ref DBGMCU_REQUEST_ERROR.
 *
 * \param dbgMcuConfig [in]: Pointer to DBGMCU configuration, see \ref dbgMcu_Config_t. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Init( const dbgMcu_Config_t * const dbgMcuConfig )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_NULL_PTR != dbgMcuConfig )
    {
        /*------------------------ Configuration check -----------------------*/
        retState = DbgMcu_Check_Config( dbgMcuConfig );

        /*------------------------ Register configuration --------------------*/
        if( DBGMCU_REQUEST_OK == retState )
        {
            retState = DbgMcu_Set_Config( dbgMcuConfig );

            if( DBGMCU_REQUEST_OK == retState )
            {
                dbgMcu_RuntimeData.InitState = DBGMCU_FUNCTION_ACTIVE;
            }
            else
            {
                /* Self-verification failed - reset state of the module */
                (void)DbgMcu_Deinit();
            }
        }
        else
        {
            /* Invalid configuration, nothing is written */
        }
    }
    else
    {
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Deinitializes module DbgMcu
 *
 * Clears the bits owned by the module (the debug in the low-power modes, the trace and the freeze
 * bits of all peripherals of the device line, the reset values of the registers) and verifies them by
 * a read-back. The module is deinitialized even if the verification fails.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Deinit( void )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;
    dbgMcu_Config_t       resetConfig;

    (void)DbgMcu_Get_DefaultConfig( &resetConfig );

    retState = DbgMcu_Set_Config( &resetConfig );

    dbgMcu_RuntimeData.InitState = DBGMCU_FUNCTION_INACTIVE;

    return ( retState );
}


/**
 * \brief Main task of module DbgMcu
 *
 * The module has nothing to process periodically, the function is kept for the
 * common interface of the modules.
 */
void DbgMcu_Task( void )
{
    return;
}


/**
 * \brief Fills configuration structure with default values.
 *
 * Default configuration: no debug in the low-power modes, trace disabled, no
 * peripheral frozen.
 *
 * \param dbgMcuConfig [out]: Pointer to store default configuration. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_DefaultConfig( dbgMcu_Config_t * const dbgMcuConfig )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_NULL_PTR != dbgMcuConfig )
    {
        dbgMcuConfig->LowPowerSleep   = DBGMCU_FUNCTION_INACTIVE;
        dbgMcuConfig->LowPowerStop    = DBGMCU_FUNCTION_INACTIVE;
        dbgMcuConfig->LowPowerStandby = DBGMCU_FUNCTION_INACTIVE;
        dbgMcuConfig->TraceMode       = DBGMCU_TRACE_DISABLED;

        for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx ++ )
        {
            dbgMcuConfig->Freeze[ periphIdx ] = DBGMCU_FUNCTION_INACTIVE;
        }

        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Returns the device identifier.
 *
 * The identifier is the field DEV_ID (12 bits) of DBGMCU_IDCODE, see DBGMCU_DEVICE_ID_xxx. The
 * function does not depend on the configuration and can be called before \ref DbgMcu_Init.
 *
 * \param deviceId [out]: Pointer to store the device identifier. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_DeviceId( dbgMcu_DeviceId_t * const deviceId )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_NULL_PTR != deviceId )
    {
        const uint32_t idCode = DbgMcu_Get_RegVal( DBGMCU_REG_IDCODE, DBGMCU_IDCODE_DEV_ID );

        *deviceId = (dbgMcu_DeviceId_t)DBGMCU_REG_TO_DEVICE_ID( idCode );

        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Returns the silicon revision.
 *
 * The revision is the field REV_ID (16 bits) of DBGMCU_IDCODE, see DBGMCU_REVISION_ID_xxx. The
 * function does not depend on the configuration and can be called before \ref DbgMcu_Init.
 *
 * \param revisionId [out]: Pointer to store the silicon revision. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_RevisionId( dbgMcu_RevisionId_t * const revisionId )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_NULL_PTR != revisionId )
    {
        const uint32_t idCode = DbgMcu_Get_RegVal( DBGMCU_REG_IDCODE, DBGMCU_IDCODE_REV_ID );

        *revisionId = (dbgMcu_RevisionId_t)DBGMCU_REG_TO_REVISION_ID( idCode );

        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Enables or disables the debug in a low-power mode.
 *
 * Writes the bit of the mode in DBGMCU_CR and verifies it by a read-back. STM32H5 has no
 * setting for the Sleep mode, the request of \ref DBGMCU_LP_SLEEP is refused (no register is written).
 *
 * \note  The debug in the low-power modes raises the consumption, it is a setting of the
 *        development builds.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param lowPower [in]: Low-power mode, see \ref dbgMcu_LowPower_t
 * \param state    [in]: \ref DBGMCU_FUNCTION_ACTIVE keeps the debug connection in the mode
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Set_LowPowerDebug( dbgMcu_LowPower_t lowPower, dbgMcu_FunctionState_t state )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();
    const dbgMcu_RequestState_t stateCheck = DbgMcu_Check_FunctionState( state );

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_LP_CNT     >  lowPower   ) &&
        ( DBGMCU_REQUEST_OK == stateCheck )    )
    {
        const uint32_t bitMask = dbgMcu_LowPowerBitLut[ lowPower ];

        if( DBGMCU_LP_BIT_NONE != bitMask )
        {
            retState = DbgMcu_Reg_Set_BitState( DBGMCU_REG_CR, bitMask, state );
        }
        else
        {
            /* The mode has no setting on this family, nothing is written */
            retState = DBGMCU_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized or invalid parameter, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Returns the debug in a low-power mode.
 *
 * Reads the bit of the mode in DBGMCU_CR. STM32H5 has no setting for the Sleep mode, the
 * request of \ref DBGMCU_LP_SLEEP returns \ref DBGMCU_REQUEST_ERROR.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param lowPower [in]:  Low-power mode, see \ref dbgMcu_LowPower_t
 * \param state    [out]: Pointer to store the state, \ref DBGMCU_FUNCTION_ACTIVE if the debug connection
 *                        is kept in the mode. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_LowPowerDebug( dbgMcu_LowPower_t lowPower, dbgMcu_FunctionState_t * const state )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_LP_CNT     >  lowPower   ) &&
        ( DBGMCU_NULL_PTR   != state      )    )
    {
        const uint32_t bitMask = dbgMcu_LowPowerBitLut[ lowPower ];

        if( DBGMCU_LP_BIT_NONE != bitMask )
        {
            *state   = DbgMcu_Get_BitState( DBGMCU_REG_CR, bitMask );
            retState = DBGMCU_REQUEST_OK;
        }
        else
        {
            /* The mode has no setting on this family */
            retState = DBGMCU_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized or invalid parameter */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Freezes or releases a peripheral while the core is halted by the debugger.
 *
 * Writes the freeze bit of the peripheral and verifies it by a read-back (the write of the freeze bit
 * of a secure peripheral is ignored while the secure invasive debug is disabled, then the function
 * returns an error). The freeze has an effect only while the core is halted.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param periph [in]: Peripheral, see \ref dbgMcu_Periph_t (only the peripherals of the device line)
 * \param freeze [in]: \ref DBGMCU_FUNCTION_ACTIVE stops the peripheral while the core is halted,
 *                     \ref DBGMCU_FUNCTION_INACTIVE lets it run
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Set_PeriphFreeze( dbgMcu_Periph_t periph, dbgMcu_FunctionState_t freeze )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();
    const dbgMcu_RequestState_t stateCheck = DbgMcu_Check_FunctionState( freeze );

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_PERIPH_CNT >  periph     ) &&
        ( DBGMCU_REQUEST_OK == stateCheck )    )
    {
        retState = DbgMcu_Reg_Set_BitState( dbgMcu_FreezeLut[ periph ].RegId,
                                            dbgMcu_FreezeLut[ periph ].BitMask,
                                            freeze );
    }
    else
    {
        /* Module not initialized or invalid parameter, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Returns the freeze of a peripheral.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param periph [in]:  Peripheral, see \ref dbgMcu_Periph_t
 * \param freeze [out]: Pointer to store the state, \ref DBGMCU_FUNCTION_ACTIVE if the peripheral is
 *                      stopped while the core is halted. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_PeriphFreeze( dbgMcu_Periph_t periph, dbgMcu_FunctionState_t * const freeze )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_PERIPH_CNT >  periph     ) &&
        ( DBGMCU_NULL_PTR   != freeze     )    )
    {
        *freeze  = DbgMcu_Get_BitState( dbgMcu_FreezeLut[ periph ].RegId,
                                        dbgMcu_FreezeLut[ periph ].BitMask );
        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid parameter */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Freezes or releases all peripherals of the device line.
 *
 * Writes the freeze bits of all items of \ref dbgMcu_Periph_t and verifies them by a read-back.
 * The registers are written one by one, the first failure stops the request (the registers
 * written before keep the new value).
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param freeze [in]: \ref DBGMCU_FUNCTION_ACTIVE stops all peripherals while the core is halted,
 *                     \ref DBGMCU_FUNCTION_INACTIVE lets them run
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Set_PeriphFreezeAll( dbgMcu_FunctionState_t freeze )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();
    const dbgMcu_RequestState_t stateCheck = DbgMcu_Check_FunctionState( freeze );

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_REQUEST_OK == stateCheck )    )
    {
        dbgMcu_FunctionState_t freezeTable[ DBGMCU_PERIPH_CNT ];

        for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx ++ )
        {
            freezeTable[ periphIdx ] = freeze;
        }

        retState = DbgMcu_Set_FreezeTable( freezeTable );
    }
    else
    {
        /* Module not initialized or invalid parameter, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Sets the trace pins and the trace clock (debugger side of the trace).
 *
 * Writes the trace clock enable, the trace pin enable and the pin assignment of DBGMCU_CR and
 * verifies them by a read-back. The sources of the trace and the instrumentation are configured
 * in the Core module.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param traceMode [in]: Trace mode, see \ref dbgMcu_TraceMode_t
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Set_TraceEnable( dbgMcu_TraceMode_t traceMode )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_TRACE_CNT  >  traceMode  )    )
    {
        retState = DbgMcu_Reg_Set_Verified( DBGMCU_REG_CR, DBGMCU_CR_TRACE_MASK, dbgMcu_TraceBitLut[ traceMode ] );
    }
    else
    {
        /* Module not initialized or invalid parameter, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Returns the debug state reported by the hardware.
 *
 * The state is a bit mask of DBGMCU_DEBUG_STATE_xxx: the debug access (the debug access ports are
 * enabled, DBGMCU_SR) and the messages of the debug authentication mailbox (DBGMCU_DBG_AUTH_ACK).
 * The DBGMCU does not report whether a debugger is connected.
 *
 * \pre   The module shall be initialized by \ref DbgMcu_Init.
 *
 * \param debugState [out]: Pointer to store the debug state. Must not be NULL.
 *
 * \return Function processing state. Returns \ref DBGMCU_REQUEST_OK if request
 *         was processed without problems. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
dbgMcu_RequestState_t DbgMcu_Get_DebugState( dbgMcu_DebugState_t * const debugState )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t readyState = DbgMcu_Check_Ready();

    if( ( DBGMCU_REQUEST_OK == readyState ) &&
        ( DBGMCU_NULL_PTR   != debugState )    )
    {
        const dbgMcu_FlagState_t enabledPorts = DbgMcu_Get_FlagState( DBGMCU_REG_SR, DBGMCU_SR_ACC_PORT_ENBL );
        const dbgMcu_FlagState_t hostMessage  = DbgMcu_Get_FlagState( DBGMCU_REG_DBG_AUTH_ACK,
                                                                      DBGMCU_DBG_AUTH_ACK_HOST );
        const dbgMcu_FlagState_t devMessage   = DbgMcu_Get_FlagState( DBGMCU_REG_DBG_AUTH_ACK,
                                                                      DBGMCU_DBG_AUTH_ACK_DEV );
        dbgMcu_DebugState_t      stateMask    = DBGMCU_DEBUG_STATE_NONE;

        if( DBGMCU_FLAG_ACTIVE == enabledPorts )
        {
            stateMask |= DBGMCU_DEBUG_STATE_ACCESS_ENABLED;
        }
        else
        {
            /* All debug access ports are locked */
        }

        if( DBGMCU_FLAG_ACTIVE == hostMessage )
        {
            stateMask |= DBGMCU_DEBUG_STATE_AUTH_HOST_MSG;
        }
        else
        {
            /* No unread message from the host */
        }

        if( DBGMCU_FLAG_ACTIVE == devMessage )
        {
            stateMask |= DBGMCU_DEBUG_STATE_AUTH_DEVICE_MSG;
        }
        else
        {
            /* No unread message from the device */
        }

        *debugState = stateMask;
        retState    = DBGMCU_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid parameter */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Checks that the module was initialized.
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if \ref DbgMcu_Init succeeded (and \ref DbgMcu_Deinit was not
 *         called after it). Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Check_Ready( void )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_FUNCTION_ACTIVE == dbgMcu_RuntimeData.InitState )
    {
        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        /* Module is not initialized */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Checks that the function state is a member of its enumeration.
 *
 * \param state [in]: Function state to check
 *
 * \return Returns \ref DBGMCU_REQUEST_OK for \ref DBGMCU_FUNCTION_INACTIVE and \ref DBGMCU_FUNCTION_ACTIVE.
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Check_FunctionState( dbgMcu_FunctionState_t state )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( ( DBGMCU_FUNCTION_INACTIVE == state ) ||
        ( DBGMCU_FUNCTION_ACTIVE   == state )    )
    {
        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        /* Value is not a member of the enumeration */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Checks the configuration, no register is accessed.
 *
 * \param dbgMcuConfig [in]: Pointer to DBGMCU configuration, must not be NULL
 *
 * \return Returns \ref DBGMCU_REQUEST_OK for a valid configuration (see \ref DbgMcu_Init).
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Check_Config( const dbgMcu_Config_t * const dbgMcuConfig )
{
    dbgMcu_RequestState_t       retState     = DBGMCU_REQUEST_ERROR;
    dbgMcu_RequestState_t       freezeState  = DBGMCU_REQUEST_OK;
    const dbgMcu_RequestState_t stopState    = DbgMcu_Check_FunctionState( dbgMcuConfig->LowPowerStop );
    const dbgMcu_RequestState_t standbyState = DbgMcu_Check_FunctionState( dbgMcuConfig->LowPowerStandby );

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx ++ )
    {
        const dbgMcu_RequestState_t itemState = DbgMcu_Check_FunctionState( dbgMcuConfig->Freeze[ periphIdx ] );

        if( DBGMCU_REQUEST_OK != itemState )
        {
            freezeState = DBGMCU_REQUEST_ERROR;
            break;
        }
        else
        {
            /* Item of the freeze table is valid */
        }
    }

    if( ( DBGMCU_FUNCTION_INACTIVE == dbgMcuConfig->LowPowerSleep ) &&
        ( DBGMCU_REQUEST_OK        == stopState                   ) &&
        ( DBGMCU_REQUEST_OK        == standbyState                ) &&
        ( DBGMCU_TRACE_CNT         >  dbgMcuConfig->TraceMode     ) &&
        ( DBGMCU_REQUEST_OK        == freezeState                 )    )
    {
        retState = DBGMCU_REQUEST_OK;
    }
    else
    {
        /* Sleep mode has no setting, or a member is out of its list */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Writes the configuration to the registers (DBGMCU_CR, then the freeze registers).
 *
 * \param dbgMcuConfig [in]: Pointer to DBGMCU configuration, checked (or the reset configuration)
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if all registers read back as written.
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Set_Config( const dbgMcu_Config_t * const dbgMcuConfig )
{
    dbgMcu_RequestState_t retState = DbgMcu_Set_ConfigRegister( dbgMcuConfig );

    if( DBGMCU_REQUEST_OK == retState )
    {
        retState = DbgMcu_Set_FreezeTable( dbgMcuConfig->Freeze );
    }
    else
    {
        /* DBGMCU_CR was not written as required, freeze registers are not written */
    }

    return ( retState );
}


/**
 * \brief Writes the debug in the low-power modes and the trace to DBGMCU_CR.
 *
 * \param dbgMcuConfig [in]: Pointer to DBGMCU configuration, checked (or the reset configuration)
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if the register read back as written.
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Set_ConfigRegister( const dbgMcu_Config_t * const dbgMcuConfig )
{
    uint32_t regValue = dbgMcu_TraceBitLut[ dbgMcuConfig->TraceMode ];

    if( DBGMCU_FUNCTION_ACTIVE == dbgMcuConfig->LowPowerStop )
    {
        regValue |= DBGMCU_CR_DBG_STOP;
    }
    else
    {
        /* Debug in Stop mode is not requested */
    }

    if( DBGMCU_FUNCTION_ACTIVE == dbgMcuConfig->LowPowerStandby )
    {
        regValue |= DBGMCU_CR_DBG_STANDBY;
    }
    else
    {
        /* Debug in Standby mode is not requested */
    }

    return ( DbgMcu_Reg_Set_Verified( DBGMCU_REG_CR, DBGMCU_CR_MODULE_MASK, regValue ) );
}


/**
 * \brief Writes the freeze bits of all peripherals of the device line.
 *
 * The bits are collected per register, every register is written once (the bits outside of the list
 * keep their value) and verified. The first failure stops the request.
 *
 * \param freezeTable [in]: Freeze state of the peripherals, indexed by \ref dbgMcu_Periph_t
 *                          (\ref DBGMCU_PERIPH_CNT items), the items shall be valid
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if all freeze registers read back as written.
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Set_FreezeTable( const dbgMcu_FunctionState_t * const freezeTable )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_OK;
    uint32_t              maskLut[ DBGMCU_REG_CNT ]  = { 0u };
    uint32_t              valueLut[ DBGMCU_REG_CNT ] = { 0u };

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx ++ )
    {
        const dbgMcu_RegId_t regId = dbgMcu_FreezeLut[ periphIdx ].RegId;

        maskLut[ regId ] |= dbgMcu_FreezeLut[ periphIdx ].BitMask;

        if( DBGMCU_FUNCTION_ACTIVE == freezeTable[ periphIdx ] )
        {
            valueLut[ regId ] |= dbgMcu_FreezeLut[ periphIdx ].BitMask;
        }
        else
        {
            /* Peripheral is not frozen, its bit stays cleared */
        }
    }

    for( uint32_t regIdx = 0u; DBGMCU_REG_CNT > regIdx; regIdx ++ )
    {
        if( 0u != maskLut[ regIdx ] )
        {
            retState = DbgMcu_Reg_Set_Verified( (dbgMcu_RegId_t)regIdx, maskLut[ regIdx ], valueLut[ regIdx ] );

            if( DBGMCU_REQUEST_OK != retState )
            {
                break;
            }
            else
            {
                /* Register is written, continue with the next one */
            }
        }
        else
        {
            /* Register has no freeze bit */
        }
    }

    return ( retState );
}


/**
 * \brief Returns the state of the register bits.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask of the bits
 *
 * \return Returns \ref DBGMCU_FUNCTION_ACTIVE if any bit of the mask is set.
 *         Otherwise returns \ref DBGMCU_FUNCTION_INACTIVE.
 */
static dbgMcu_FunctionState_t DbgMcu_Get_BitState( dbgMcu_RegId_t regId, uint32_t bitMask )
{
    dbgMcu_FunctionState_t retState = DBGMCU_FUNCTION_INACTIVE;
    const uint32_t         bitValue = DbgMcu_Get_RegBit( regId, bitMask );

    if( 0u != bitValue )
    {
        retState = DBGMCU_FUNCTION_ACTIVE;
    }
    else
    {
        retState = DBGMCU_FUNCTION_INACTIVE;
    }

    return ( retState );
}


/**
 * \brief Returns the state of a status flag of the register.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask of the flag bits
 *
 * \return Returns \ref DBGMCU_FLAG_ACTIVE if any bit of the mask is set.
 *         Otherwise returns \ref DBGMCU_FLAG_INACTIVE.
 */
static dbgMcu_FlagState_t DbgMcu_Get_FlagState( dbgMcu_RegId_t regId, uint32_t bitMask )
{
    dbgMcu_FlagState_t           retState = DBGMCU_FLAG_INACTIVE;
    const dbgMcu_FunctionState_t bitState = DbgMcu_Get_BitState( regId, bitMask );

    if( DBGMCU_FUNCTION_ACTIVE == bitState )
    {
        retState = DBGMCU_FLAG_ACTIVE;
    }
    else
    {
        retState = DBGMCU_FLAG_INACTIVE;
    }

    return ( retState );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */
