/**
 * \author Mr.Nobody
 * \file Test_DbgMcu.c
 * \ingroup DbgMcu
 * \brief Unit tests of Microcontroller Debug Unit (DBGMCU) module.
 *
 * DbgMcu.c and DbgMcu_Reg.c are compiled unchanged, DBGMCU registers (base address
 * 0x44024000, inside of the peripheral region of RegMem) are emulated by RegMem. The module
 * uses no other module (no clock enable, no interrupt), no mock is needed.
 *
 * The tests of the freeze bits compare the module with an independent table of the tests
 * (utDbgMcu_ExpectedLut: peripheral, register, bit of the CMSIS device header), the table is
 * guarded by the same freeze bit macros as the list of the peripherals, the size of the list
 * is checked against the known number of freezable peripherals of every device line.
 *
 * \note Limitations of the register emulation (RegMem registers are plain memory):
 *       - DBGMCU_IDCODE, DBGMCU_SR and DBGMCU_DBG_AUTH_ACK are read only on HW, the tests preset
 *         them by plain writes.
 *       - Ignored writes of HW (debug closed, freeze bit of a secure peripheral while the secure
 *         invasive debug is disabled) are created by the HW model of RegMem (UtDbgMcu_HwModel_xxx),
 *         which runs after every store.
 *       - Every test case is executed in its own process (CTest), the runtime data of the module
 *         start from the reset state (not initialized) in every test.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "RegMem.h"                         /* Register memory emulation      */
#include "DbgMcu_Port.h"                    /* Module under test              */
#include "DbgMcu_Reg.h"                     /* Register access of the module  */
#include "Stm32.h"                          /* DBGMCU registers definition    */
/* ============================= TYPEDEFS =================================== */

/** \brief Expected freeze bit of a peripheral (independent of the table of the module) */
typedef struct
{
    dbgMcu_Periph_t     Periph; /**< Peripheral (position in the table)  */
    volatile uint32_t * Reg;    /**< Freeze register of the peripheral   */
    uint32_t            Mask;   /**< Freeze bit of the peripheral        */
}   utDbgMcu_FreezeItem_t;


/** \brief Expected bits of DBGMCU_CR of a trace mode */
typedef struct
{
    dbgMcu_TraceMode_t  Mode; /**< Trace mode                                    */
    uint32_t            Bits; /**< Expected trace bits of DBGMCU_CR              */
}   utDbgMcu_TraceItem_t;

/* ======================= FORWARD DECLARATIONS ============================= */

static void             UtDbgMcu_Init_Default      ( void );
static void             UtDbgMcu_Init_AllFrozen    ( void );
static uint32_t         UtDbgMcu_Get_FreezeMask    ( volatile uint32_t * reg );
static void             UtDbgMcu_Preset_Noise      ( void );
static void             UtDbgMcu_Check_Noise       ( void );
static void             UtDbgMcu_Check_FreezeRegs  ( uint32_t apb1l,
                                                     uint32_t apb1h,
                                                     uint32_t apb2,
                                                     uint32_t apb3,
                                                     uint32_t ahb1 );

static void             UtDbgMcu_HwModel_StopNotKept       ( void );
static void             UtDbgMcu_HwModel_WwdgNotKept       ( void );
static void             UtDbgMcu_HwModel_IwdgSticky        ( void );
static void             UtDbgMcu_HwModel_TraceNotKept      ( void );

/* ========================= SYMBOLIC CONSTANTS ============================= */

/** Bits of DBGMCU_CR owned by the module (low-power debug and trace) */
#define UT_DBGMCU_CR_MODULE_MASK            ( DBGMCU_CR_DBG_STOP      | \
                                              DBGMCU_CR_DBG_STANDBY   | \
                                              DBGMCU_CR_TRACE_IOEN    | \
                                              DBGMCU_CR_TRACE_CLKEN   | \
                                              DBGMCU_CR_TRACE_MODE      )

/** Value outside of the enumeration of the function state */
#define UT_DBGMCU_INVALID_STATE             ( 7u )

/** Value of DBGMCU_IDCODE: revision X (0x1007), device STM32H562/563/573 (0x484), reserved bits 15:12 = 6 */
#define UT_DBGMCU_IDCODE_H563_REV_X         ( 0x10076484u )

/** Value of DBGMCU_IDCODE: revision Z (0x1001), device STM32H523/533 (0x478), reserved bits 15:12 = 6 */
#define UT_DBGMCU_IDCODE_H523_REV_Z         ( 0x10016478u )

/** Debug authentication mailbox acknowledge: both messages pending */
#define UT_DBGMCU_AUTH_ACK_BOTH             ( DBGMCU_DBG_AUTH_ACK_HOST | DBGMCU_DBG_AUTH_ACK_DEV )

/** Register value or mask with every bit set (preset of a register, mask of the whole register) */
#define UT_DBGMCU_ALL_BITS                  ( 0xFFFFFFFFu )

/** Value of the field DBGMCU_IDCODE.DEV_ID with every bit set (12 bits of the field) */
#define UT_DBGMCU_DEVICE_ID_ALL_BITS        ( 0x0FFFu )

/** Value of the field DBGMCU_IDCODE.REV_ID with every bit set (16 bits of the field) */
#define UT_DBGMCU_REVISION_ID_ALL_BITS      ( 0xFFFFu )

/** Trace bits of DBGMCU_CR with the trace disabled */
#define UT_DBGMCU_TRACE_BITS_NONE           ( 0x00000000u )

/** Offset of a peripheral value far behind the end of the list of the peripherals */
#define UT_DBGMCU_PERIPH_BEYOND_OFFSET      ( 5u )

/** Value of the field DBGMCU_SR.ACC_PORT_ENBL with one access port enabled (bit 0 of the field) */
#define UT_DBGMCU_SR_ACC_PORT_ENBL_FIRST    ( 0x0001u )

/** Value preset in DBGMCU_SR of the read only test: ACC_PORT_ENBL = 1 (bits 31:16), ACC_PORT_PRES = 3 (bits 15:0) */
#define UT_DBGMCU_SR_PRESET                 ( 0x00010003u )

/** Lowest bit of a register (mask and value of the test writes) */
#define UT_DBGMCU_BIT0                      ( 0x1u )

/** Mask of the two lowest bits of a register */
#define UT_DBGMCU_BITS_0_1_MASK             ( 0x3u )

/** Value of a test write with a bit (bit 2) outside of UT_DBGMCU_BITS_0_1_MASK */
#define UT_DBGMCU_VALUE_OUTSIDE_MASK        ( 0x6u )

/** Value of a test write inside of UT_DBGMCU_BITS_0_1_MASK */
#define UT_DBGMCU_VALUE_INSIDE_MASK         ( 0x2u )

/** Value preset in DBGMCU_APB1FZR2 of the test of the verified write: bits 31:28, bit 2 and bit 0 */
#define UT_DBGMCU_APB1FZR2_PRESET           ( 0xF0000005u )

/** Value of DBGMCU_APB1FZR2 after the masked write of UT_DBGMCU_VALUE_INSIDE_MASK to the preset value */
#define UT_DBGMCU_APB1FZR2_MASKED_WRITE     ( 0xF0000006u )

/** Value preset in DBGMCU_APB1FZR2 of the test of the bit state: bit 2 only */
#define UT_DBGMCU_APB1FZR2_BIT2             ( 0x00000004u )

/** Value of DBGMCU_APB1FZR2 of the test of the bit state with the bit 0 set additionally */
#define UT_DBGMCU_APB1FZR2_BIT2_BIT0        ( 0x00000005u )

/* Expected count of the freezable peripherals of the device line (RM0481, RM0492, RM0517):
 * common items 29 (IWDG, WWDG, TIM1, TIM2, TIM3, TIM6, TIM7, LPTIM1, LPTIM2, RTC, I2C1, I2C2, I3C1,
 * GPDMA1 channels 0 - 7, GPDMA2 channels 0 - 7). */
#if defined(STM32H503xx)
/** STM32H503: common items and I3C2 */
#define UT_DBGMCU_PERIPH_CNT                ( 30u )
#elif defined(STM32H523xx) || \
      defined(STM32H533xx) || \
      defined(STM32H543xx) || \
      defined(STM32H553xx)
/** STM32H523, H533, H543, H553: common items, TIM4, TIM5, TIM8, TIM12, TIM15, I2C3, I3C2 */
#define UT_DBGMCU_PERIPH_CNT                ( 36u )
#elif defined(STM32H562xx) || \
      defined(STM32H563xx) || \
      defined(STM32H573xx)
/** STM32H562, H563, H573: common items, TIM4, TIM5, TIM8, TIM12 - TIM17, I2C3, I2C4, LPTIM3 - LPTIM6 */
#define UT_DBGMCU_PERIPH_CNT                ( 44u )
#elif defined(STM32H5E4xx) || \
      defined(STM32H5E5xx) || \
      defined(STM32H5F4xx) || \
      defined(STM32H5F5xx)
/** STM32H5E4, H5E5, H5F4, H5F5: all items of H562 with I3C2 and the GPDMA channels 8 - 11 */
#define UT_DBGMCU_PERIPH_CNT                ( 53u )
#else
#error "DbgMcu unit tests: unknown STM32H5 device line"
#endif

/* ========================== LOCAL VARIABLES =============================== */

/** \brief Expected mapping of the peripherals to the freeze bits (CMSIS device header), in the order of the list */
static const utDbgMcu_FreezeItem_t utDbgMcu_ExpectedLut[] =
{
    { DBGMCU_PERIPH_IWDG,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_IWDG_STOP         },
    { DBGMCU_PERIPH_WWDG,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_WWDG_STOP         },
    { DBGMCU_PERIPH_TIM1,         &DBGMCU->APB2FZR,  DBGMCU_APB2FZR_DBG_TIM1_STOP          },
    { DBGMCU_PERIPH_TIM2,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM2_STOP         },
    { DBGMCU_PERIPH_TIM3,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM3_STOP         },
#if defined(DBGMCU_APB1FZR1_DBG_TIM4_STOP)
    { DBGMCU_PERIPH_TIM4,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM4_STOP         },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM5_STOP)
    { DBGMCU_PERIPH_TIM5,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM5_STOP         },
#endif
    { DBGMCU_PERIPH_TIM6,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM6_STOP         },
    { DBGMCU_PERIPH_TIM7,         &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM7_STOP         },
#if defined(DBGMCU_APB2FZR_DBG_TIM8_STOP)
    { DBGMCU_PERIPH_TIM8,         &DBGMCU->APB2FZR,  DBGMCU_APB2FZR_DBG_TIM8_STOP          },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM12_STOP)
    { DBGMCU_PERIPH_TIM12,        &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM12_STOP        },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM13_STOP)
    { DBGMCU_PERIPH_TIM13,        &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM13_STOP        },
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM14_STOP)
    { DBGMCU_PERIPH_TIM14,        &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM14_STOP        },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM15_STOP)
    { DBGMCU_PERIPH_TIM15,        &DBGMCU->APB2FZR,  DBGMCU_APB2FZR_DBG_TIM15_STOP         },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM16_STOP)
    { DBGMCU_PERIPH_TIM16,        &DBGMCU->APB2FZR,  DBGMCU_APB2FZR_DBG_TIM16_STOP         },
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM17_STOP)
    { DBGMCU_PERIPH_TIM17,        &DBGMCU->APB2FZR,  DBGMCU_APB2FZR_DBG_TIM17_STOP         },
#endif
    { DBGMCU_PERIPH_LPTIM1,       &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_LPTIM1_STOP        },
    { DBGMCU_PERIPH_LPTIM2,       &DBGMCU->APB1FZR2, DBGMCU_APB1FZR2_DBG_LPTIM2_STOP       },
#if defined(DBGMCU_APB3FZR_DBG_LPTIM3_STOP)
    { DBGMCU_PERIPH_LPTIM3,       &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_LPTIM3_STOP        },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM4_STOP)
    { DBGMCU_PERIPH_LPTIM4,       &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_LPTIM4_STOP        },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM5_STOP)
    { DBGMCU_PERIPH_LPTIM5,       &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_LPTIM5_STOP        },
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM6_STOP)
    { DBGMCU_PERIPH_LPTIM6,       &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_LPTIM6_STOP        },
#endif
    { DBGMCU_PERIPH_RTC,          &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_RTC_STOP           },
    { DBGMCU_PERIPH_I2C1_TIMEOUT, &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_I2C1_STOP         },
    { DBGMCU_PERIPH_I2C2_TIMEOUT, &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_I2C2_STOP         },
#if defined(DBGMCU_APB3FZR_DBG_I2C3_STOP)
    { DBGMCU_PERIPH_I2C3_TIMEOUT, &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_I2C3_STOP          },
#endif
#if defined(DBGMCU_APB3FZR_DBG_I2C4_STOP)
    { DBGMCU_PERIPH_I2C4_TIMEOUT, &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_I2C4_STOP          },
#endif
    { DBGMCU_PERIPH_I3C1_TIMEOUT, &DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_I3C1_STOP         },
#if defined(DBGMCU_APB3FZR_DBG_I3C2_STOP)
    { DBGMCU_PERIPH_I3C2_TIMEOUT, &DBGMCU->APB3FZR,  DBGMCU_APB3FZR_DBG_I3C2_STOP          },
#endif
    { DBGMCU_PERIPH_GPDMA1_CH0,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH0_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH1,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH1_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH2,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH2_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH3,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH3_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH4,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH4_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH5,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH5_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH6,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH6_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH7,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH7_STOP    },
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA1_CH8_STOP)
    { DBGMCU_PERIPH_GPDMA1_CH8,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH8_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH9,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH9_STOP    },
    { DBGMCU_PERIPH_GPDMA1_CH10,  &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH10_STOP   },
    { DBGMCU_PERIPH_GPDMA1_CH11,  &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA1_CH11_STOP   },
#endif
    { DBGMCU_PERIPH_GPDMA2_CH0,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH0_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH1,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH1_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH2,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH2_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH3,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH3_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH4,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH4_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH5,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH5_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH6,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH6_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH7,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH7_STOP    },
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA2_CH8_STOP)
    { DBGMCU_PERIPH_GPDMA2_CH8,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH8_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH9,   &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH9_STOP    },
    { DBGMCU_PERIPH_GPDMA2_CH10,  &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH10_STOP   },
    { DBGMCU_PERIPH_GPDMA2_CH11,  &DBGMCU->AHB1FZR,  DBGMCU_AHB1FZR_DBG_GPDMA2_CH11_STOP   },
#endif
};

#define UT_DBGMCU_EXPECTED_CNT              ( sizeof( utDbgMcu_ExpectedLut ) / sizeof( utDbgMcu_FreezeItem_t ) )


/** \brief Expected trace bits of DBGMCU_CR per trace mode
 *         (RM0481: TRACE_EN clock, TRACE_IOEN pins, TRACE_MODE assignment) */
static const utDbgMcu_TraceItem_t utDbgMcu_TraceLut[] =
{
    { DBGMCU_TRACE_DISABLED,  UT_DBGMCU_TRACE_BITS_NONE                              },
    { DBGMCU_TRACE_ASYNC,     DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN           },
    { DBGMCU_TRACE_SYNC_1BIT, DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE_0 },
    { DBGMCU_TRACE_SYNC_2BIT, DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE_1 },
    { DBGMCU_TRACE_SYNC_4BIT, DBGMCU_CR_TRACE_CLKEN | DBGMCU_CR_TRACE_IOEN | DBGMCU_CR_TRACE_MODE   },
};

#define UT_DBGMCU_TRACE_CNT                 ( sizeof( utDbgMcu_TraceLut ) / sizeof( utDbgMcu_TraceItem_t ) )

/* ============================ TEST FIXTURE ================================ */

void setUp( void )
{
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Reset() );
}


void tearDown( void )
{
    /* Nothing to verify, the module uses no mock */
}

/* ========================== MODULE VERSION ================================ */

/**
 * \brief   DbgMcu_Get_ModuleVersion() returns version of the module.
 *
 * \details Reads the module version structure.
 *
 * \par Expected results
 * - Version is 0.1.0 (Major 0, Minor 1, Patch 0).
 */
void Ut_DbgMcu_Get_ModuleVersion_ReturnsVersion( void )
{
    dbgMcu_ModuleVersion_t version = DbgMcu_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 0u, version.Major );
    TEST_ASSERT_EQUAL_UINT8( 1u, version.Minor );
    TEST_ASSERT_EQUAL_UINT8( 0u, version.Patch );
}

/* =========================== DEFAULT CONFIG =============================== */

/**
 * \brief   DbgMcu_Get_DefaultConfig() fills default configuration.
 *
 * \details Fills the structure with a pattern, reads the default configuration, then calls
 *          the function with NULL.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, no low-power debug, trace disabled, no peripheral frozen (every item).
 * - NULL pointer: DBGMCU_REQUEST_ERROR.
 */
void Ut_DbgMcu_Get_DefaultConfig_ReturnsDefaults( void )
{
    dbgMcu_Config_t config;

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        config.Freeze[ periphIdx ] = DBGMCU_FUNCTION_ACTIVE;
    }

    config.LowPowerSleep   = DBGMCU_FUNCTION_ACTIVE;
    config.LowPowerStop    = DBGMCU_FUNCTION_ACTIVE;
    config.LowPowerStandby = DBGMCU_FUNCTION_ACTIVE;
    config.TraceMode       = DBGMCU_TRACE_SYNC_4BIT;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, config.LowPowerSleep );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, config.LowPowerStop );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, config.LowPowerStandby );
    TEST_ASSERT_EQUAL( DBGMCU_TRACE_DISABLED,    config.TraceMode );

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, config.Freeze[ periphIdx ] );
    }

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_DefaultConfig( NULL ) );
}

/* ============================ IDENTIFICATION ============================== */

/**
 * \brief   DbgMcu_Get_DeviceId() decodes the device identifier.
 *
 * \details Presets DBGMCU_IDCODE of STM32H563 revision X (0x10076484) and reads the
 *          identifier without the initialization of the module.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, identifier 0x484 (12 bits, the reserved bits 15:12 are not part of it).
 * - NULL pointer: DBGMCU_REQUEST_ERROR.
 */
void Ut_DbgMcu_Get_DeviceId_BeforeInit_DecodesField( void )
{
    dbgMcu_DeviceId_t deviceId = 0u;

    DBGMCU->IDCODE = UT_DBGMCU_IDCODE_H563_REV_X;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DeviceId( &deviceId ) );
    TEST_ASSERT_EQUAL_HEX16( DBGMCU_DEVICE_ID_STM32H562_H563_H573, deviceId );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_DeviceId( NULL ) );
}


/**
 * \brief   DbgMcu_Get_DeviceId() decodes the identifier of other devices.
 *
 * \details Presets DBGMCU_IDCODE of STM32H523 revision Z (0x10016478), then of a register with
 *          every bit set.
 *
 * \par Expected results
 * - Identifier 0x478 for the first value, 0xFFF (the 12 bits of the field only) for the second one.
 */
void Ut_DbgMcu_Get_DeviceId_OtherValues_DecodesTwelveBits( void )
{
    dbgMcu_DeviceId_t deviceId = 0u;

    DBGMCU->IDCODE = UT_DBGMCU_IDCODE_H523_REV_Z;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DeviceId( &deviceId ) );
    TEST_ASSERT_EQUAL_HEX16( DBGMCU_DEVICE_ID_STM32H523_H533, deviceId );

    DBGMCU->IDCODE = UT_DBGMCU_ALL_BITS;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DeviceId( &deviceId ) );
    TEST_ASSERT_EQUAL_HEX16( UT_DBGMCU_DEVICE_ID_ALL_BITS, deviceId );
}


/**
 * \brief   DbgMcu_Get_RevisionId() decodes the silicon revision.
 *
 * \details Presets DBGMCU_IDCODE of STM32H563 revision X (0x10076484) and reads the
 *          revision without the initialization of the module, then the value of STM32H523 revision Z
 *          and the value with every bit set.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, revision 0x1007, 0x1001 and 0xFFFF (the 16 bits of the field).
 * - NULL pointer: DBGMCU_REQUEST_ERROR.
 */
void Ut_DbgMcu_Get_RevisionId_BeforeInit_DecodesField( void )
{
    dbgMcu_RevisionId_t revisionId = 0u;

    DBGMCU->IDCODE = UT_DBGMCU_IDCODE_H563_REV_X;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_RevisionId( &revisionId ) );
    TEST_ASSERT_EQUAL_HEX16( DBGMCU_REVISION_ID_X, revisionId );

    DBGMCU->IDCODE = UT_DBGMCU_IDCODE_H523_REV_Z;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_RevisionId( &revisionId ) );
    TEST_ASSERT_EQUAL_HEX16( DBGMCU_REVISION_ID_Z, revisionId );

    DBGMCU->IDCODE = UT_DBGMCU_ALL_BITS;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_RevisionId( &revisionId ) );
    TEST_ASSERT_EQUAL_HEX16( UT_DBGMCU_REVISION_ID_ALL_BITS, revisionId );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_RevisionId( NULL ) );
}

/* ============================ INITIALIZATION ============================== */

/**
 * \brief   DbgMcu_Init() with a NULL configuration is refused.
 *
 * \details Presets noise to the registers, calls the function with NULL, then tries a function
 *          which needs the initialization.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written, the module is not initialized.
 */
void Ut_DbgMcu_Init_NullConfig_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( NULL ) );

    UtDbgMcu_Check_Noise();
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Init() with the default configuration clears the bits owned by the module.
 *
 * \details Presets every bit of DBGMCU_CR and of the freeze registers, initializes the module
 *          with the default configuration.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK.
 * - DBGMCU_CR: low-power debug and trace bits cleared, the other bits (debug credentials reset type) kept.
 * - Freeze registers: the freeze bits of the device line cleared, the other (reserved) bits kept.
 */
void Ut_DbgMcu_Init_DefaultConfig_ClearsOnlyBitsOfModule( void )
{
    dbgMcu_Config_t config;

    DBGMCU->CR       = UT_DBGMCU_ALL_BITS;
    DBGMCU->APB1FZR1 = UT_DBGMCU_ALL_BITS;
    DBGMCU->APB1FZR2 = UT_DBGMCU_ALL_BITS;
    DBGMCU->APB2FZR  = UT_DBGMCU_ALL_BITS;
    DBGMCU->APB3FZR  = UT_DBGMCU_ALL_BITS;
    DBGMCU->AHB1FZR  = UT_DBGMCU_ALL_BITS;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( ~UT_DBGMCU_CR_MODULE_MASK,                        DBGMCU->CR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR1 ),    DBGMCU->APB1FZR1 );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR2 ),    DBGMCU->APB1FZR2 );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB2FZR ),     DBGMCU->APB2FZR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB3FZR ),     DBGMCU->APB3FZR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->AHB1FZR ),     DBGMCU->AHB1FZR );
}


/**
 * \brief   DbgMcu_Init() freezes the requested peripherals.
 *
 * \details Requests the freeze of IWDG, WWDG, I2C1 timeout (APB1L), TIM1 (APB2), LPTIM1, RTC (APB3),
 *          GPDMA1 channel 0 and GPDMA2 channel 7 (AHB1). The bit positions are written here as
 *          the CMSIS masks of the reference manual registers.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK.
 * - APB1L: IWDG, WWDG and I2C1 bits, APB1H: none, APB2: TIM1 bit, APB3: LPTIM1 and RTC bits,
 *   AHB1: GPDMA1 channel 0 and GPDMA2 channel 7 bits, DBGMCU_CR: 0.
 */
void Ut_DbgMcu_Init_FreezeTable_SetsRequestedBits( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.Freeze[ DBGMCU_PERIPH_IWDG ]         = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_WWDG ]         = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_I2C1_TIMEOUT ] = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_TIM1 ]         = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_LPTIM1 ]       = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_RTC ]          = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_GPDMA1_CH0 ]   = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_GPDMA2_CH7 ]   = DBGMCU_FUNCTION_ACTIVE;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( 0u, DBGMCU->CR );

    UtDbgMcu_Check_FreezeRegs( DBGMCU_APB1FZR1_DBG_IWDG_STOP |
                               DBGMCU_APB1FZR1_DBG_WWDG_STOP |
                               DBGMCU_APB1FZR1_DBG_I2C1_STOP,
                               0u,
                               DBGMCU_APB2FZR_DBG_TIM1_STOP,
                               DBGMCU_APB3FZR_DBG_LPTIM1_STOP | DBGMCU_APB3FZR_DBG_RTC_STOP,
                               DBGMCU_AHB1FZR_DBG_GPDMA1_CH0_STOP | DBGMCU_AHB1FZR_DBG_GPDMA2_CH7_STOP );
}


/**
 * \brief   DbgMcu_Init() freezes every peripheral of the device line.
 *
 * \details Requests the freeze of all items of the list.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK.
 * - Every freeze register holds exactly the bits of the peripherals of the line (taken from
 *   the table of the tests), DBGMCU_CR: 0.
 */
void Ut_DbgMcu_Init_AllPeripheralsFrozen_SetsEveryBitOfTheLine( void )
{
    UtDbgMcu_Init_AllFrozen();

    TEST_ASSERT_EQUAL_HEX32( 0u, DBGMCU->CR );

    UtDbgMcu_Check_FreezeRegs( UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR1 ),
                               UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR2 ),
                               UtDbgMcu_Get_FreezeMask( &DBGMCU->APB2FZR ),
                               UtDbgMcu_Get_FreezeMask( &DBGMCU->APB3FZR ),
                               UtDbgMcu_Get_FreezeMask( &DBGMCU->AHB1FZR ) );
}


/**
 * \brief   DbgMcu_Init() enables the debug in the Stop and the Standby mode.
 *
 * \details Initializes the module three times (the module can be initialized again): Stop only,
 *          Standby only, both modes.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, DBGMCU_CR holds DBG_STOP, DBG_STANDBY and both bits respectively,
 *   the freeze registers are 0.
 */
void Ut_DbgMcu_Init_LowPowerDebug_SetsStopAndStandbyBits( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.LowPowerStop = DBGMCU_FUNCTION_ACTIVE;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DBG_STOP, DBGMCU->CR );

    config.LowPowerStop    = DBGMCU_FUNCTION_INACTIVE;
    config.LowPowerStandby = DBGMCU_FUNCTION_ACTIVE;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DBG_STANDBY, DBGMCU->CR );

    config.LowPowerStop = DBGMCU_FUNCTION_ACTIVE;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY, DBGMCU->CR );

    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
}


/**
 * \brief   DbgMcu_Init() assigns the trace pins and the trace clock.
 *
 * \details Initializes the module with every trace mode (the module can be initialized again).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, DBGMCU_CR holds the trace bits of the mode: disabled 0, asynchronous
 *   TRACE_EN + TRACE_IOEN, synchronous modes with TRACE_MODE 1, 2 and 3 (RM0481).
 */
void Ut_DbgMcu_Init_TraceModes_WritesTraceBits( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    for( uint32_t modeIdx = 0u; UT_DBGMCU_TRACE_CNT > modeIdx; modeIdx++ )
    {
        config.TraceMode = utDbgMcu_TraceLut[ modeIdx ].Mode;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
        TEST_ASSERT_EQUAL_HEX32( utDbgMcu_TraceLut[ modeIdx ].Bits, DBGMCU->CR );
    }
}


/**
 * \brief   DbgMcu_Init() refuses the debug in the Sleep mode.
 *
 * \details STM32H5 has no Sleep bit in DBGMCU_CR (the debug connection is kept by the hardware).
 *          Presets noise to the registers and requests the debug in the Sleep mode together
 *          with a freeze and the Stop mode.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written (the valid items of the configuration too),
 *   the module is not initialized.
 */
void Ut_DbgMcu_Init_LowPowerSleepActive_ReturnsErrorNothingWritten( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.LowPowerSleep               = DBGMCU_FUNCTION_ACTIVE;
    config.LowPowerStop                = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_IWDG ] = DBGMCU_FUNCTION_ACTIVE;

    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );

    UtDbgMcu_Check_Noise();
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() refuses a freeze state outside of the enumeration.
 *
 * \details Requests a valid freeze together with an invalid state of the last item of the table.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written, the module is not initialized.
 */
void Ut_DbgMcu_Init_InvalidFreezeState_ReturnsErrorNothingWritten( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.Freeze[ DBGMCU_PERIPH_IWDG ]            = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_CNT - 1u ]        = (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE;

    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );

    UtDbgMcu_Check_Noise();
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() refuses a low-power state or a trace mode outside of the enumeration.
 *
 * \details Requests the Stop state 7, the Standby state 7 and the trace mode DBGMCU_TRACE_CNT in turn.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for each of them, no register is written, the module is not initialized.
 */
void Ut_DbgMcu_Init_InvalidLowPowerOrTrace_ReturnsErrorNothingWritten( void )
{
    dbgMcu_Config_t config;

    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    config.LowPowerStop = (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );
    UtDbgMcu_Check_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    config.LowPowerStandby = (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );
    UtDbgMcu_Check_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    config.TraceMode = DBGMCU_TRACE_CNT;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );
    UtDbgMcu_Check_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() with an invalid configuration keeps an initialized module as it is.
 *
 * \details Initializes the module, then requests an invalid configuration.
 *
 * \par Expected results
 * - The second DbgMcu_Init() returns DBGMCU_REQUEST_ERROR and writes no register.
 * - The module stays initialized (DbgMcu_Set_PeriphFreeze() returns DBGMCU_REQUEST_OK).
 */
void Ut_DbgMcu_Init_InvalidConfigAfterInit_ModuleStaysInitialized( void )
{
    dbgMcu_Config_t config;

    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    config.TraceMode = DBGMCU_TRACE_CNT;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );
    TEST_ASSERT_EQUAL_HEX32( 0u, DBGMCU->CR );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() verifies DBGMCU_CR by read-back.
 *
 * \details The HW model clears DBG_STOP after every store (the write is ignored, debug closed).
 *          Requests the debug in the Stop mode together with the freeze of all peripherals.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the module is deinitialized (not initialized afterwards).
 * - The freeze registers are not written (they are 0), DBGMCU_CR holds no module bit.
 */
void Ut_DbgMcu_Init_CrReadBackMismatch_ReturnsErrorDeinitialized( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.LowPowerStop = DBGMCU_FUNCTION_ACTIVE;

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        config.Freeze[ periphIdx ] = DBGMCU_FUNCTION_ACTIVE;
    }

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_StopNotKept ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( 0u, DBGMCU->CR & UT_DBGMCU_CR_MODULE_MASK );
    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_TIM2, DBGMCU_FUNCTION_INACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() verifies the freeze registers by read-back.
 *
 * \details The HW model clears the WWDG freeze bit after every store (the freeze bit of a
 *          secure peripheral can not be changed while the secure invasive debug is disabled).
 *          Requests the freeze of all peripherals.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the module is deinitialized: the bits written before the mismatch
 *   are cleared again (every freeze register is 0), the module is not initialized afterwards.
 */
void Ut_DbgMcu_Init_FreezeReadBackMismatch_ReturnsErrorBitsCleared( void )
{
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_WwdgNotKept ) );

    {
        dbgMcu_Config_t config;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

        for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
        {
            config.Freeze[ periphIdx ] = DBGMCU_FUNCTION_ACTIVE;
        }

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Init( &config ) );
    }

    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_TIM2, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Init() called again applies the new configuration.
 *
 * \details Initializes the module with every peripheral frozen, then with the default
 *          configuration.
 *
 * \par Expected results
 * - Both calls return DBGMCU_REQUEST_OK, the freeze registers are cleared by the second one,
 *   the module stays initialized.
 */
void Ut_DbgMcu_Init_Repeated_AppliesNewConfiguration( void )
{
    UtDbgMcu_Init_AllFrozen();
    TEST_ASSERT_NOT_EQUAL( 0u, DBGMCU->AHB1FZR );

    UtDbgMcu_Init_Default();

    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}

/* ============================ DEINITIALIZATION ============================ */

/**
 * \brief   DbgMcu_Deinit() clears the bits of the module and deinitializes it.
 *
 * \details Presets noise (the bits outside of the module), initializes the module with the Stop
 *          and Standby debug, the asynchronous trace and every peripheral frozen, then deinitializes it.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, every bit owned by the module is cleared, the other bits keep the preset
 *   noise.
 * - The module is not initialized (DbgMcu_Set_PeriphFreeze() returns DBGMCU_REQUEST_ERROR).
 */
void Ut_DbgMcu_Deinit_AfterInit_ClearsModuleBitsKeepsOthers( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.LowPowerStop    = DBGMCU_FUNCTION_ACTIVE;
    config.LowPowerStandby = DBGMCU_FUNCTION_ACTIVE;
    config.TraceMode       = DBGMCU_TRACE_ASYNC;

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        config.Freeze[ periphIdx ] = DBGMCU_FUNCTION_ACTIVE;
    }

    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Deinit() );

    UtDbgMcu_Check_Noise();
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}


/**
 * \brief   DbgMcu_Deinit() works without the initialization.
 *
 * \details Sets bits owned by the module (for example by a debugger), calls the function
 *          without DbgMcu_Init().
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, the bits owned by the module are cleared.
 */
void Ut_DbgMcu_Deinit_WithoutInit_ClearsModuleBits( void )
{
    DBGMCU->CR       = DBGMCU_CR_DBG_STOP | DBGMCU_CR_TRACE_CLKEN;
    DBGMCU->APB1FZR1 = DBGMCU_APB1FZR1_DBG_IWDG_STOP;
    DBGMCU->AHB1FZR  = DBGMCU_AHB1FZR_DBG_GPDMA1_CH0_STOP;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Deinit() );

    TEST_ASSERT_EQUAL_HEX32( 0u, DBGMCU->CR );
    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
}


/**
 * \brief   DbgMcu_Deinit() verifies the registers by read-back.
 *
 * \details The HW model sets the IWDG freeze bit after every store (the bit can not be cleared).
 *          Initializes the module with the default configuration (the model is activated after it),
 *          then deinitializes it.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the module is deinitialized anyway (DbgMcu_Set_PeriphFreeze()
 *   returns DBGMCU_REQUEST_ERROR).
 */
void Ut_DbgMcu_Deinit_ReadBackMismatch_ReturnsErrorModuleDeinitialized( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_IwdgSticky ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Deinit() );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
}

/* ============================ LOW-POWER DEBUG ============================= */

/**
 * \brief   DbgMcu_Set_LowPowerDebug() sets and clears the bits of the Stop and the Standby mode.
 *
 * \details Presets the debug credentials reset type (not owned by the module), enables Stop, then
 *          Standby, then disables Stop.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK each time, DBGMCU_CR: DBG_STOP, then DBG_STOP + DBG_STANDBY, then DBG_STANDBY,
 *   always with the credentials bit kept, the freeze registers are not touched.
 */
void Ut_DbgMcu_Set_LowPowerDebug_StopAndStandby_SetsAndClearsBits( void )
{
    DBGMCU->CR = DBGMCU_CR_DCRT;

    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DCRT | DBGMCU_CR_DBG_STOP, DBGMCU->CR );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STANDBY, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DCRT | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY, DBGMCU->CR );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_INACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DCRT | DBGMCU_CR_DBG_STANDBY, DBGMCU->CR );

    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
}


/**
 * \brief   DbgMcu_Set_LowPowerDebug() refuses the Sleep mode.
 *
 * \details STM32H5 has no Sleep bit in DBGMCU_CR. Requests the Sleep mode (both states) after
 *          the initialization, with noise preset in the registers.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_LowPowerDebug_Sleep_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_SLEEP, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_SLEEP, DBGMCU_FUNCTION_INACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_LowPowerDebug() checks the parameters before it writes a register.
 *
 * \details Requests a mode outside of the list, then a state outside of the enumeration (after
 *          the initialization, noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for both, no register is written.
 */
void Ut_DbgMcu_Set_LowPowerDebug_InvalidParameter_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_CNT, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_LowPowerDebug() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_LowPowerDebug_NotInitialized_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_LowPowerDebug() verifies the write by read-back.
 *
 * \details The HW model clears DBG_STOP after every store (the write is ignored).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for the Stop mode.
 * - DBGMCU_REQUEST_OK for the Standby mode (its bit stays as written).
 */
void Ut_DbgMcu_Set_LowPowerDebug_WriteNotKept_ReturnsError( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_StopNotKept ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,    DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STANDBY, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DBG_STANDBY, DBGMCU->CR );
}


/**
 * \brief   DbgMcu_Get_LowPowerDebug() reads the bits of the Stop and the Standby mode.
 *
 * \details Initializes the module, sets and clears DBG_STOP and DBG_STANDBY directly in the register.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, the state follows the bit of the mode (the other mode does not matter).
 */
void Ut_DbgMcu_Get_LowPowerDebug_StopAndStandby_ReadsBits( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    UtDbgMcu_Init_Default();

    DBGMCU->CR = DBGMCU_CR_DBG_STOP;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );

    DBGMCU->CR = DBGMCU_CR_DBG_STANDBY;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
}


/**
 * \brief   DbgMcu_Get_LowPowerDebug() refuses the Sleep mode and invalid parameters.
 *
 * \details Reads the Sleep mode, a mode outside of the list and with a NULL pointer after the
 *          initialization.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for each of them, the output is not written.
 */
void Ut_DbgMcu_Get_LowPowerDebug_SleepOrInvalid_ReturnsErrorOutputKept( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_ACTIVE;

    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_SLEEP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_CNT, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, NULL ) );
}


/**
 * \brief   DbgMcu_Get_LowPowerDebug() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() while DBG_STOP is set.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the output is not written.
 */
void Ut_DbgMcu_Get_LowPowerDebug_NotInitialized_ReturnsError( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    DBGMCU->CR = DBGMCU_CR_DBG_STOP;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
}

/* ============================ PERIPHERAL FREEZE =========================== */

/**
 * \brief   The list of the peripherals has the expected size and order on the device line.
 *
 * \details Compares the number of the items with the known number of the freezable peripherals
 *          of the device line (RM0481, RM0492, RM0517) and with the table of the tests, checks the
 *          order of the table of the tests.
 *
 * \par Expected results
 * - DBGMCU_PERIPH_CNT equals the expected count of the line and the size of the table of the tests.
 * - Every item of the table of the tests is at the position of its peripheral.
 */
void Ut_DbgMcu_PeriphList_DeviceLine_HasExpectedItems( void )
{
    TEST_ASSERT_EQUAL_UINT32( UT_DBGMCU_PERIPH_CNT, (uint32_t)DBGMCU_PERIPH_CNT );
    TEST_ASSERT_EQUAL_UINT32( UT_DBGMCU_PERIPH_CNT, (uint32_t)UT_DBGMCU_EXPECTED_CNT );

    for( uint32_t periphIdx = 0u; UT_DBGMCU_EXPECTED_CNT > periphIdx; periphIdx++ )
    {
        TEST_ASSERT_EQUAL_UINT32( periphIdx, (uint32_t)utDbgMcu_ExpectedLut[ periphIdx ].Periph );
        TEST_ASSERT_NOT_EQUAL( 0u, utDbgMcu_ExpectedLut[ periphIdx ].Mask );
    }
}


/**
 * \brief   DbgMcu_Set_PeriphFreeze() writes the freeze bit of every peripheral of the device line.
 *
 * \details For every item of the list: sets the freeze, reads the registers, reads the state
 *          back through DbgMcu_Get_PeriphFreeze(), clears the freeze. Noise is preset in the
 *          registers (the bits outside of the freeze bits).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK.
 * - After the set exactly the bit of the peripheral is set in its register (and in no other
 *   register), Get returns DBGMCU_FUNCTION_ACTIVE.
 * - After the clear the registers equal the preset noise again, Get returns DBGMCU_FUNCTION_INACTIVE.
 */
void Ut_DbgMcu_Set_PeriphFreeze_EveryPeripheral_SetsAndClearsOnlyItsBit( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    for( uint32_t periphIdx = 0u; UT_DBGMCU_EXPECTED_CNT > periphIdx; periphIdx++ )
    {
        const utDbgMcu_FreezeItem_t * const item = &utDbgMcu_ExpectedLut[ periphIdx ];
        const uint32_t                      regOld = *item->Reg;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( item->Periph, DBGMCU_FUNCTION_ACTIVE ) );
        TEST_ASSERT_EQUAL_HEX32( regOld | item->Mask, *item->Reg );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( item->Periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( item->Periph, DBGMCU_FUNCTION_INACTIVE ) );
        TEST_ASSERT_EQUAL_HEX32( regOld & ~item->Mask, *item->Reg );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( item->Periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );

        /* Restore the preset noise for the next item */
        *item->Reg = regOld;
    }

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreeze() changes only the bit of the requested peripheral.
 *
 * \details Freezes TIM2, then WWDG, then releases TIM2.
 *
 * \par Expected results
 * - APB1L holds TIM2, then TIM2 + WWDG, then WWDG bits only.
 */
void Ut_DbgMcu_Set_PeriphFreeze_TwoPeripherals_BitsAreIndependent( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_TIM2, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_APB1FZR1_DBG_TIM2_STOP, DBGMCU->APB1FZR1 );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_APB1FZR1_DBG_TIM2_STOP | DBGMCU_APB1FZR1_DBG_WWDG_STOP, DBGMCU->APB1FZR1 );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_TIM2, DBGMCU_FUNCTION_INACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_APB1FZR1_DBG_WWDG_STOP, DBGMCU->APB1FZR1 );
}


/**
 * \brief   DbgMcu_Set_PeriphFreeze() checks the parameters before it writes a register.
 *
 * \details Requests a peripheral outside of the list (DBGMCU_PERIPH_CNT and a larger value), then a state
 *          outside of the enumeration, after the initialization, with noise preset in the registers.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for each of them, no register is written.
 */
void Ut_DbgMcu_Set_PeriphFreeze_InvalidParameter_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_CNT, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Set_PeriphFreeze( (dbgMcu_Periph_t)( DBGMCU_PERIPH_CNT + UT_DBGMCU_PERIPH_BEYOND_OFFSET ),
                                                DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_IWDG, (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreeze() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_PeriphFreeze_NotInitialized_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_IWDG, DBGMCU_FUNCTION_ACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreeze() verifies the write by read-back.
 *
 * \details The HW model clears the WWDG freeze bit after every store (a secure peripheral while
 *          the secure invasive debug is disabled).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for WWDG.
 * - DBGMCU_REQUEST_OK for IWDG (the freeze bit of another peripheral is kept).
 */
void Ut_DbgMcu_Set_PeriphFreeze_WriteNotKept_ReturnsError( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_WwdgNotKept ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_WWDG, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,    DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_IWDG, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_APB1FZR1_DBG_IWDG_STOP, DBGMCU->APB1FZR1 );
}


/**
 * \brief   DbgMcu_Get_PeriphFreeze() reads the freeze bit of every peripheral of the device line.
 *
 * \details Initializes the module, sets every freeze bit directly in the registers, then clears them one by one.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, DBGMCU_FUNCTION_ACTIVE while the bit is set, DBGMCU_FUNCTION_INACTIVE after it was cleared
 *   (the other peripherals stay active).
 */
void Ut_DbgMcu_Get_PeriphFreeze_EveryPeripheral_ReadsItsBit( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    UtDbgMcu_Init_Default();

    for( uint32_t periphIdx = 0u; UT_DBGMCU_EXPECTED_CNT > periphIdx; periphIdx++ )
    {
        *utDbgMcu_ExpectedLut[ periphIdx ].Reg |= utDbgMcu_ExpectedLut[ periphIdx ].Mask;
    }

    for( uint32_t periphIdx = 0u; UT_DBGMCU_EXPECTED_CNT > periphIdx; periphIdx++ )
    {
        const utDbgMcu_FreezeItem_t * const item = &utDbgMcu_ExpectedLut[ periphIdx ];

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( item->Periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

        *item->Reg &= ~item->Mask;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( item->Periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
    }
}


/**
 * \brief   DbgMcu_Get_PeriphFreeze() refuses invalid parameters.
 *
 * \details Reads a peripheral outside of the list and with a NULL pointer after the initialization.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for both, the output is not written.
 */
void Ut_DbgMcu_Get_PeriphFreeze_InvalidParameter_ReturnsErrorOutputKept( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_ACTIVE;

    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_CNT, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_IWDG, NULL ) );
}


/**
 * \brief   DbgMcu_Get_PeriphFreeze() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() while the IWDG freeze bit is set.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the output is not written.
 */
void Ut_DbgMcu_Get_PeriphFreeze_NotInitialized_ReturnsError( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    DBGMCU->APB1FZR1 = DBGMCU_APB1FZR1_DBG_IWDG_STOP;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_IWDG, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
}


/**
 * \brief   DbgMcu_Set_PeriphFreezeAll() freezes and releases all peripherals of the device line.
 *
 * \details Presets noise (the bits outside of the freeze bits) and initializes the module with the
 *          default configuration, freezes all peripherals, then releases all of them.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK each time.
 * - After the freeze every freeze register holds all the freeze bits of the line plus the noise,
 *   DBGMCU_CR is not touched.
 * - After the release the registers equal the preset noise again.
 */
void Ut_DbgMcu_Set_PeriphFreezeAll_ActiveAndInactive_SetsAndClearsAllBits( void )
{
    UtDbgMcu_Preset_Noise();
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_ACTIVE ) );

    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_ALL_BITS, DBGMCU->APB1FZR1 );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_ALL_BITS, DBGMCU->APB1FZR2 );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_ALL_BITS, DBGMCU->APB2FZR );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_ALL_BITS, DBGMCU->APB3FZR );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_ALL_BITS, DBGMCU->AHB1FZR );
    TEST_ASSERT_EQUAL_HEX32( ~UT_DBGMCU_CR_MODULE_MASK, DBGMCU->CR );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_INACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreezeAll() checks the state before it writes a register.
 *
 * \details Requests an invalid state after the initialization (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_PeriphFreezeAll_InvalidState_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Set_PeriphFreezeAll( (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreezeAll() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_PeriphFreezeAll_NotInitialized_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_ACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_PeriphFreezeAll() verifies the registers by read-back.
 *
 * \details The HW model clears the WWDG freeze bit after every store.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR.
 */
void Ut_DbgMcu_Set_PeriphFreezeAll_WriteNotKept_ReturnsError( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_WwdgNotKept ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_ACTIVE ) );
}

/* ================================== TRACE ================================= */

/**
 * \brief   DbgMcu_Set_TraceEnable() writes the trace bits and keeps the other bits.
 *
 * \details Initializes the module with the Stop mode debug, presets the credentials bit, then sets
 *          every trace mode in the order of the list and back to disabled.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK, the trace bits of DBGMCU_CR equal the bits of the mode (TRACE_EN, TRACE_IOEN, TRACE_MODE),
 *   DBG_STOP and the credentials bit are kept, the freeze registers are not touched.
 */
void Ut_DbgMcu_Set_TraceEnable_EveryMode_WritesTraceBitsOnly( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    config.LowPowerStop = DBGMCU_FUNCTION_ACTIVE;

    DBGMCU->CR = DBGMCU_CR_DCRT;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );

    for( uint32_t modeIdx = 0u; UT_DBGMCU_TRACE_CNT > modeIdx; modeIdx++ )
    {
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_TraceEnable( utDbgMcu_TraceLut[ modeIdx ].Mode ) );
        TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DCRT | DBGMCU_CR_DBG_STOP | utDbgMcu_TraceLut[ modeIdx ].Bits, DBGMCU->CR );
    }

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_DISABLED ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_CR_DCRT | DBGMCU_CR_DBG_STOP, DBGMCU->CR );

    UtDbgMcu_Check_FreezeRegs( 0u, 0u, 0u, 0u, 0u );
}


/**
 * \brief   DbgMcu_Set_TraceEnable() refuses a trace mode outside of the list.
 *
 * \details Requests DBGMCU_TRACE_CNT after the initialization (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_TraceEnable_InvalidMode_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_CNT ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_TraceEnable() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() (noise preset in the registers).
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Set_TraceEnable_NotInitialized_ReturnsErrorNothingWritten( void )
{
    UtDbgMcu_Preset_Noise();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_ASYNC ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   DbgMcu_Set_TraceEnable() verifies the write by read-back.
 *
 * \details The HW model clears the trace clock enable bit after every store.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR for the asynchronous mode.
 * - DBGMCU_REQUEST_OK for the disabled trace (all trace bits are 0).
 */
void Ut_DbgMcu_Set_TraceEnable_WriteNotKept_ReturnsError( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtDbgMcu_HwModel_TraceNotKept ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_ASYNC ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,    DbgMcu_Set_TraceEnable( DBGMCU_TRACE_DISABLED ) );
}

/* ================================ DEBUG STATE ============================= */

/**
 * \brief   DbgMcu_Get_DebugState() decodes the status registers.
 *
 * \details Initializes the module, presets DBGMCU_SR (access port enable field) and DBGMCU_DBG_AUTH_ACK
 *          (mailbox acknowledge bits) with the combinations: nothing, an enabled port (also the
 *          presence field alone, which is not a state), the host message, the device message, both messages.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK each time.
 * - State: none, access enabled (any bit of AP_ENABLED), host message, device message, both messages
 *   (the presence field AP_PRESENT does not set any bit).
 */
void Ut_DbgMcu_Get_DebugState_StatusRegisters_DecodesState( void )
{
    dbgMcu_DebugState_t state = DBGMCU_DEBUG_STATE_ACCESS_ENABLED;

    UtDbgMcu_Init_Default();

    DBGMCU->SR = 0u;
    DBGMCU->DBG_AUTH_ACK = 0u;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_NONE, state );

    DBGMCU->SR = DBGMCU_SR_ACC_PORT_PRES;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_NONE, state );

    DBGMCU->SR = ( UT_DBGMCU_SR_ACC_PORT_ENBL_FIRST << DBGMCU_SR_ACC_PORT_ENBL_Pos ) | DBGMCU_SR_ACC_PORT_PRES;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_ACCESS_ENABLED, state );

    DBGMCU->SR = DBGMCU_SR_ACC_PORT_ENBL;
    DBGMCU->DBG_AUTH_ACK = DBGMCU_DBG_AUTH_ACK_HOST;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_ACCESS_ENABLED | DBGMCU_DEBUG_STATE_AUTH_HOST_MSG, state );

    DBGMCU->SR = 0u;
    DBGMCU->DBG_AUTH_ACK = DBGMCU_DBG_AUTH_ACK_DEV;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_AUTH_DEVICE_MSG, state );

    DBGMCU->DBG_AUTH_ACK = UT_DBGMCU_AUTH_ACK_BOTH;
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_AUTH_HOST_MSG | DBGMCU_DEBUG_STATE_AUTH_DEVICE_MSG, state );
}


/**
 * \brief   DbgMcu_Get_DebugState() refuses a NULL pointer.
 *
 * \details Calls the function with NULL after the initialization.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR.
 */
void Ut_DbgMcu_Get_DebugState_NullPointer_ReturnsError( void )
{
    UtDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_DebugState( NULL ) );
}


/**
 * \brief   DbgMcu_Get_DebugState() needs the initialization.
 *
 * \details Calls the function without DbgMcu_Init() while an access port is enabled.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_ERROR, the output is not written.
 */
void Ut_DbgMcu_Get_DebugState_NotInitialized_ReturnsError( void )
{
    dbgMcu_DebugState_t state = DBGMCU_DEBUG_STATE_NONE;

    DBGMCU->SR = DBGMCU_SR_ACC_PORT_ENBL;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DEBUG_STATE_NONE, state );
}

/* ================================== TASK ================================== */

/**
 * \brief   DbgMcu_Task() has nothing to process.
 *
 * \details Calls the task before and after the initialization (noise preset in the registers).
 *
 * \par Expected results
 * - No register is written.
 */
void Ut_DbgMcu_Task_Called_NoRegisterWritten( void )
{
    UtDbgMcu_Preset_Noise();

    DbgMcu_Task();
    UtDbgMcu_Check_Noise();

    UtDbgMcu_Init_Default();
    UtDbgMcu_Preset_Noise();

    DbgMcu_Task();
    UtDbgMcu_Check_Noise();
}

/* ============================ REGISTER ACCESS ============================= */

/**
 * \brief   The register accessors refuse an invalid register identifier.
 *
 * \details Calls every accessor and protocol function with DBGMCU_REG_CNT (noise preset in the registers).
 *
 * \par Expected results
 * - Get functions return 0, the protocol functions DBGMCU_REQUEST_ERROR, no register is written.
 */
void Ut_DbgMcu_Reg_InvalidRegId_NoAccess( void )
{
    UtDbgMcu_Preset_Noise();

    DbgMcu_Set_RegBit( DBGMCU_REG_CNT, UT_DBGMCU_ALL_BITS );
    DbgMcu_Reset_RegBit( DBGMCU_REG_CNT, UT_DBGMCU_ALL_BITS );
    DbgMcu_Set_RegVal( DBGMCU_REG_CNT, UT_DBGMCU_ALL_BITS, UT_DBGMCU_ALL_BITS );

    TEST_ASSERT_EQUAL_HEX32( 0u, DbgMcu_Get_RegBit( DBGMCU_REG_CNT, UT_DBGMCU_ALL_BITS ) );
    TEST_ASSERT_EQUAL_HEX32( 0u, DbgMcu_Get_RegVal( DBGMCU_REG_CNT, UT_DBGMCU_ALL_BITS ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Reg_Set_Verified( DBGMCU_REG_CNT, UT_DBGMCU_BIT0, UT_DBGMCU_BIT0 ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Reg_Set_BitState( DBGMCU_REG_CNT, UT_DBGMCU_BIT0, DBGMCU_FUNCTION_ACTIVE ) );

    UtDbgMcu_Check_Noise();
}


/**
 * \brief   The register accessors refuse writes to the read only registers.
 *
 * \details Presets IDCODE, SR and DBG_AUTH_ACK, writes them through every write accessor and protocol function.
 *
 * \par Expected results
 * - The registers keep the preset value, the protocol functions return DBGMCU_REQUEST_ERROR,
 *   the Get functions read the registers.
 */
void Ut_DbgMcu_Reg_ReadOnlyRegisters_WriteRefused( void )
{
    const dbgMcu_RegId_t readOnlyLut[] = { DBGMCU_REG_IDCODE, DBGMCU_REG_SR, DBGMCU_REG_DBG_AUTH_ACK };

    DBGMCU->IDCODE       = UT_DBGMCU_IDCODE_H563_REV_X;
    DBGMCU->SR           = UT_DBGMCU_SR_PRESET;
    DBGMCU->DBG_AUTH_ACK = DBGMCU_DBG_AUTH_ACK_HOST;

    for( uint32_t regIdx = 0u; ( sizeof( readOnlyLut ) / sizeof( dbgMcu_RegId_t ) ) > regIdx; regIdx++ )
    {
        const dbgMcu_RegId_t regId = readOnlyLut[ regIdx ];

        DbgMcu_Set_RegBit( regId, UT_DBGMCU_ALL_BITS );
        DbgMcu_Reset_RegBit( regId, UT_DBGMCU_ALL_BITS );
        DbgMcu_Set_RegVal( regId, UT_DBGMCU_ALL_BITS, 0u );

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                           DbgMcu_Reg_Set_Verified( regId, UT_DBGMCU_BIT0, UT_DBGMCU_BIT0 ) );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                           DbgMcu_Reg_Set_BitState( regId, UT_DBGMCU_BIT0, DBGMCU_FUNCTION_INACTIVE ) );
    }

    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_IDCODE_H563_REV_X,
                             DbgMcu_Get_RegVal( DBGMCU_REG_IDCODE, UT_DBGMCU_ALL_BITS ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_SR_PRESET,
                             DbgMcu_Get_RegVal( DBGMCU_REG_SR, UT_DBGMCU_ALL_BITS ) );
    TEST_ASSERT_EQUAL_HEX32( DBGMCU_DBG_AUTH_ACK_HOST,
                             DbgMcu_Get_RegBit( DBGMCU_REG_DBG_AUTH_ACK, UT_DBGMCU_ALL_BITS ) );
}


/**
 * \brief   DbgMcu_Reg_Set_Verified() refuses a value outside of the mask.
 *
 * \details Writes the value 0x6 with the mask 0x3 (bit 2 is outside), then the value 0x2 with the mask 0x3.
 *
 * \par Expected results
 * - The first call returns DBGMCU_REQUEST_ERROR and writes nothing.
 * - The second call returns DBGMCU_REQUEST_OK, only the bits of the mask are changed.
 */
void Ut_DbgMcu_Reg_Set_Verified_ValueOutsideMask_ReturnsErrorNothingWritten( void )
{
    DBGMCU->APB1FZR2 = UT_DBGMCU_APB1FZR2_PRESET;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Reg_Set_Verified( DBGMCU_REG_APB1FZR2,
                                                UT_DBGMCU_BITS_0_1_MASK,
                                                UT_DBGMCU_VALUE_OUTSIDE_MASK ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_APB1FZR2_PRESET, DBGMCU->APB1FZR2 );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,
                       DbgMcu_Reg_Set_Verified( DBGMCU_REG_APB1FZR2,
                                                UT_DBGMCU_BITS_0_1_MASK,
                                                UT_DBGMCU_VALUE_INSIDE_MASK ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_APB1FZR2_MASKED_WRITE, DBGMCU->APB1FZR2 );
}


/**
 * \brief   DbgMcu_Reg_Set_BitState() refuses a state outside of the enumeration.
 *
 * \details Requests the state 7 for a bit of DBGMCU_APB1FZR2, then sets and clears the bit.
 *
 * \par Expected results
 * - The first call returns DBGMCU_REQUEST_ERROR and writes nothing.
 * - Set and clear return DBGMCU_REQUEST_OK and change only the bit.
 */
void Ut_DbgMcu_Reg_Set_BitState_InvalidState_ReturnsErrorNothingWritten( void )
{
    DBGMCU->APB1FZR2 = UT_DBGMCU_APB1FZR2_BIT2;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR,
                       DbgMcu_Reg_Set_BitState( DBGMCU_REG_APB1FZR2,
                                                UT_DBGMCU_BIT0,
                                                (dbgMcu_FunctionState_t)UT_DBGMCU_INVALID_STATE ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_APB1FZR2_BIT2, DBGMCU->APB1FZR2 );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,
                       DbgMcu_Reg_Set_BitState( DBGMCU_REG_APB1FZR2, UT_DBGMCU_BIT0, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_APB1FZR2_BIT2_BIT0, DBGMCU->APB1FZR2 );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK,
                       DbgMcu_Reg_Set_BitState( DBGMCU_REG_APB1FZR2, UT_DBGMCU_BIT0, DBGMCU_FUNCTION_INACTIVE ) );
    TEST_ASSERT_EQUAL_HEX32( UT_DBGMCU_APB1FZR2_BIT2, DBGMCU->APB1FZR2 );
}

/* ========================== LOCAL FUNCTIONS =============================== */

/** Initializes the module with the default configuration and checks the result */
static void UtDbgMcu_Init_Default( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
}


/** Initializes the module with every peripheral of the line frozen and checks the result */
static void UtDbgMcu_Init_AllFrozen( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        config.Freeze[ periphIdx ] = DBGMCU_FUNCTION_ACTIVE;
    }

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
}


/** Returns all the freeze bits of the line in the register (sum of the table of the tests) */
static uint32_t UtDbgMcu_Get_FreezeMask( volatile uint32_t * reg )
{
    uint32_t mask = 0u;

    for( uint32_t periphIdx = 0u; UT_DBGMCU_EXPECTED_CNT > periphIdx; periphIdx++ )
    {
        if( reg == utDbgMcu_ExpectedLut[ periphIdx ].Reg )
        {
            mask |= utDbgMcu_ExpectedLut[ periphIdx ].Mask;
        }
        else
        {
            /* Freeze bit of another register */
        }
    }

    return ( mask );
}


/** Presets the bits outside of the bits owned by the module (the noise), the bits owned by the module are cleared */
static void UtDbgMcu_Preset_Noise( void )
{
    DBGMCU->CR       = ~UT_DBGMCU_CR_MODULE_MASK;
    DBGMCU->APB1FZR1 = ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR1 );
    DBGMCU->APB1FZR2 = ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR2 );
    DBGMCU->APB2FZR  = ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB2FZR );
    DBGMCU->APB3FZR  = ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB3FZR );
    DBGMCU->AHB1FZR  = ~UtDbgMcu_Get_FreezeMask( &DBGMCU->AHB1FZR );
}


/** Checks that the registers equal the noise of UtDbgMcu_Preset_Noise (nothing was written) */
static void UtDbgMcu_Check_Noise( void )
{
    TEST_ASSERT_EQUAL_HEX32( ~UT_DBGMCU_CR_MODULE_MASK,                        DBGMCU->CR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR1 ),    DBGMCU->APB1FZR1 );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB1FZR2 ),    DBGMCU->APB1FZR2 );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB2FZR ),     DBGMCU->APB2FZR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->APB3FZR ),     DBGMCU->APB3FZR );
    TEST_ASSERT_EQUAL_HEX32( ~UtDbgMcu_Get_FreezeMask( &DBGMCU->AHB1FZR ),     DBGMCU->AHB1FZR );
}


/** Checks the freeze registers (no noise): the registers equal the expected freeze bits */
static void UtDbgMcu_Check_FreezeRegs( uint32_t apb1l, uint32_t apb1h, uint32_t apb2, uint32_t apb3, uint32_t ahb1 )
{
    TEST_ASSERT_EQUAL_HEX32( apb1l, DBGMCU->APB1FZR1 );
    TEST_ASSERT_EQUAL_HEX32( apb1h, DBGMCU->APB1FZR2 );
    TEST_ASSERT_EQUAL_HEX32( apb2,  DBGMCU->APB2FZR );
    TEST_ASSERT_EQUAL_HEX32( apb3,  DBGMCU->APB3FZR );
    TEST_ASSERT_EQUAL_HEX32( ahb1,  DBGMCU->AHB1FZR );
}

/* ============================== HW MODELS ================================= */

/** HW model: DBG_STOP stays cleared (the write of DBGMCU_CR is ignored) */
static void UtDbgMcu_HwModel_StopNotKept( void )
{
    (void)__atomic_and_fetch( &DBGMCU->CR, (uint32_t)~DBGMCU_CR_DBG_STOP, __ATOMIC_SEQ_CST );
}


/** HW model: the freeze bit of the WWDG stays cleared (secure peripheral, secure invasive debug disabled) */
static void UtDbgMcu_HwModel_WwdgNotKept( void )
{
    (void)__atomic_and_fetch( &DBGMCU->APB1FZR1, (uint32_t)~DBGMCU_APB1FZR1_DBG_WWDG_STOP, __ATOMIC_SEQ_CST );
}


/** HW model: the freeze bit of the IWDG stays set (the clear is ignored) */
static void UtDbgMcu_HwModel_IwdgSticky( void )
{
    (void)__atomic_or_fetch( &DBGMCU->APB1FZR1, (uint32_t)DBGMCU_APB1FZR1_DBG_IWDG_STOP, __ATOMIC_SEQ_CST );
}


/** HW model: the trace clock enable stays cleared (the write is ignored) */
static void UtDbgMcu_HwModel_TraceNotKept( void )
{
    (void)__atomic_and_fetch( &DBGMCU->CR, (uint32_t)~DBGMCU_CR_TRACE_CLKEN, __ATOMIC_SEQ_CST );
}
