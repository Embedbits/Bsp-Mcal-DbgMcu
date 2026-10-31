/**
 * \author Mr.Nobody
 * \file DbgMcu_Types.h
 * \ingroup DbgMcu
 * \brief DbgMcu module global types definition
 *
 * This file contains the types definitions used across the module and are
 * available for other modules through Port file.
 *
 * The lists of the freezable peripherals (\ref dbgMcu_Periph_t) contain exactly
 * the items of the device line: an item exists only if the freeze bit of the
 * peripheral exists in the DBGMCU registers of the line (CMSIS device header).
 *
 */

/**
 * \defgroup DbgMcu DbgMcu
 * \brief DbgMcu module
 */

#ifndef DBGMCU_DBGMCU_TYPES_H
#define DBGMCU_DBGMCU_TYPES_H
/* ============================== INCLUDES ================================== */
#include "stdint.h"                         /* Module types definition        */
#include "Stm32.h"                          /* MCU device header (freeze bits) */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Null pointer definition */
#define DBGMCU_NULL_PTR                      ( ( void* ) 0u )

/** Device identifier of STM32H503 (DBGMCU_IDCODE.DEV_ID) */
#define DBGMCU_DEVICE_ID_STM32H503           ( 0x474u )

/** Device identifier of STM32H523 and STM32H533 (DBGMCU_IDCODE.DEV_ID) */
#define DBGMCU_DEVICE_ID_STM32H523_H533      ( 0x478u )

/** Device identifier of STM32H543 and STM32H553 (DBGMCU_IDCODE.DEV_ID) */
#define DBGMCU_DEVICE_ID_STM32H543_H553      ( 0x47Cu )

/** Device identifier of STM32H562, STM32H563 and STM32H573 (DBGMCU_IDCODE.DEV_ID) */
#define DBGMCU_DEVICE_ID_STM32H562_H563_H573 ( 0x484u )

/** Device identifier of STM32H5Exx and STM32H5Fxx (DBGMCU_IDCODE.DEV_ID) */
#define DBGMCU_DEVICE_ID_STM32H5EXX_H5FXX    ( 0x47Au )

/** Silicon revision A (DBGMCU_IDCODE.REV_ID, documented for all lines) */
#define DBGMCU_REVISION_ID_A                 ( 0x1000u )

/** Silicon revision Z (DBGMCU_IDCODE.REV_ID, documented for all lines) */
#define DBGMCU_REVISION_ID_Z                 ( 0x1001u )

/** Silicon revision Y (DBGMCU_IDCODE.REV_ID, documented for STM32H503) */
#define DBGMCU_REVISION_ID_Y                 ( 0x1002u )

/** Silicon revision X (DBGMCU_IDCODE.REV_ID, documented for STM32H523, STM32H533, STM32H543, STM32H553,
 *  STM32H562, STM32H563 and STM32H573) */
#define DBGMCU_REVISION_ID_X                 ( 0x1007u )

/** Silicon revision W (DBGMCU_IDCODE.REV_ID, documented for STM32H523, STM32H533, STM32H543, STM32H553,
 *  STM32H562, STM32H563 and STM32H573) */
#define DBGMCU_REVISION_ID_W                 ( 0x100Fu )

/** Debug state: nothing reported by the hardware (access ports locked, no authentication message) */
#define DBGMCU_DEBUG_STATE_NONE              ( 0x00000000u )

/** Debug state: at least one debug access port is enabled (the debug access is open, the device is open or the
 *  debug authentication succeeded). The DBGMCU does not report whether a debugger is connected. */
#define DBGMCU_DEBUG_STATE_ACCESS_ENABLED    ( 0x00000001u )

/** Debug state: the host to device mailbox of the debug authentication holds an unread message
 *  (DBG_AUTH_ACK.HOST_ACK) */
#define DBGMCU_DEBUG_STATE_AUTH_HOST_MSG     ( 0x00000002u )

/** Debug state: the device to host mailbox of the debug authentication holds an unread message
 *  (DBG_AUTH_ACK.DEV_ACK) */
#define DBGMCU_DEBUG_STATE_AUTH_DEVICE_MSG   ( 0x00000004u )

/* ========================== EXPORTED MACROS =============================== */

/* ============================== TYPEDEFS ================================== */

/** \brief Type signaling major version of SW module */
typedef uint8_t dbgMcu_MajorVersion_t;


/** \brief Type signaling minor version of SW module */
typedef uint8_t dbgMcu_MinorVersion_t;


/** \brief Type signaling patch version of SW module */
typedef uint8_t dbgMcu_PatchVersion_t;


/** \brief Type signaling actual version of SW module */
typedef struct
{
    dbgMcu_MajorVersion_t Major; /**< Major version */
    dbgMcu_MinorVersion_t Minor; /**< Minor version */
    dbgMcu_PatchVersion_t Patch; /**< Patch version */
}   dbgMcu_ModuleVersion_t;


/** Function status enumeration */
typedef enum
{
    DBGMCU_FUNCTION_INACTIVE = 0u, /**< Function status is inactive */
    DBGMCU_FUNCTION_ACTIVE         /**< Function status is active   */
}   dbgMcu_FunctionState_t;


/** Enumeration used to signal request processing state */
typedef enum
{
    DBGMCU_REQUEST_ERROR = 0u, /**< Processing request failed  */
    DBGMCU_REQUEST_OK          /**< Processing request succeed */
}   dbgMcu_RequestState_t;


/** Flag states enumeration */
typedef enum
{
    DBGMCU_FLAG_INACTIVE = 0u, /**< Inactive flag state */
    DBGMCU_FLAG_ACTIVE         /**< Active flag state   */
}   dbgMcu_FlagState_t;


/** \brief Type used to store the device identifier (12 bits, see DBGMCU_DEVICE_ID_xxx) */
typedef uint16_t dbgMcu_DeviceId_t;


/** \brief Type used to store the silicon revision (16 bits, see DBGMCU_REVISION_ID_xxx) */
typedef uint16_t dbgMcu_RevisionId_t;


/** \brief Type used to store the debug state, bit mask of DBGMCU_DEBUG_STATE_xxx */
typedef uint32_t dbgMcu_DebugState_t;


/** \brief Low-power modes in which the debug connection can be kept.
 *
 * \note STM32H5 has no setting for the Sleep mode (the DBGMCU_CR register has the bits DBG_STOP and
 *       DBG_STANDBY only, the core is clocked in the Sleep mode, the debug connection is kept by the
 *       hardware). \ref DBGMCU_LP_SLEEP stays in the list for the common interface, the requests of the
 *       Sleep mode are refused.
 */
typedef enum
{
    DBGMCU_LP_SLEEP = 0u, /**< Sleep mode (no setting on STM32H5, refused) */
    DBGMCU_LP_STOP,       /**< Stop mode (DBGMCU_CR.DBG_STOP)              */
    DBGMCU_LP_STANDBY,    /**< Standby mode (DBGMCU_CR.DBG_STANDBY)        */
    DBGMCU_LP_CNT         /**< Count of low-power modes                    */
}   dbgMcu_LowPower_t;


/** \brief Assignment of the trace pins and the trace clock (debugger side of the trace) */
typedef enum
{
    DBGMCU_TRACE_DISABLED = 0u, /**< Trace clock disabled, trace pins not assigned                        */
    DBGMCU_TRACE_ASYNC,         /**< Trace clock enabled, pins assigned for asynchronous mode (TRACESWO)  */
    DBGMCU_TRACE_SYNC_1BIT,     /**< Trace clock enabled, synchronous mode, port width 1 (TRACECK, D0)    */
    DBGMCU_TRACE_SYNC_2BIT,     /**< Trace clock enabled, synchronous mode, port width 2 (TRACECK, D0-1)  */
    DBGMCU_TRACE_SYNC_4BIT,     /**< Trace clock enabled, synchronous mode, port width 4 (TRACECK, D0-3)  */
    DBGMCU_TRACE_CNT            /**< Count of trace modes                                                 */
}   dbgMcu_TraceMode_t;


/** \brief Peripherals (and DMA channels) with a freeze bit, which stops them while the core is halted by the debugger.
 *
 * The list contains only the items of the device line, every item is available if the DBGMCU of the line has the
 * freeze bit of the peripheral (the bit of an absent peripheral is reserved).
 */
typedef enum
{
    DBGMCU_PERIPH_IWDG = 0u,                  /**< Independent watchdog                     */
    DBGMCU_PERIPH_WWDG,                       /**< Window watchdog                          */

    DBGMCU_PERIPH_TIM1,                       /**< General purpose timer 1                  */
    DBGMCU_PERIPH_TIM2,                       /**< General purpose timer 2                  */
    DBGMCU_PERIPH_TIM3,                       /**< General purpose timer 3                  */
#if defined(DBGMCU_APB1FZR1_DBG_TIM4_STOP)
    DBGMCU_PERIPH_TIM4,                       /**< General purpose timer 4                  */
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM5_STOP)
    DBGMCU_PERIPH_TIM5,                       /**< General purpose timer 5                  */
#endif
    DBGMCU_PERIPH_TIM6,                       /**< Basic timer 6                            */
    DBGMCU_PERIPH_TIM7,                       /**< Basic timer 7                            */
#if defined(DBGMCU_APB2FZR_DBG_TIM8_STOP)
    DBGMCU_PERIPH_TIM8,                       /**< General purpose timer 8                  */
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM12_STOP)
    DBGMCU_PERIPH_TIM12,                      /**< General purpose timer 12                 */
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM13_STOP)
    DBGMCU_PERIPH_TIM13,                      /**< General purpose timer 13                 */
#endif
#if defined(DBGMCU_APB1FZR1_DBG_TIM14_STOP)
    DBGMCU_PERIPH_TIM14,                      /**< General purpose timer 14                 */
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM15_STOP)
    DBGMCU_PERIPH_TIM15,                      /**< General purpose timer 15                 */
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM16_STOP)
    DBGMCU_PERIPH_TIM16,                      /**< General purpose timer 16                 */
#endif
#if defined(DBGMCU_APB2FZR_DBG_TIM17_STOP)
    DBGMCU_PERIPH_TIM17,                      /**< General purpose timer 17                 */
#endif

    DBGMCU_PERIPH_LPTIM1,                     /**< Low power timer 1                        */
    DBGMCU_PERIPH_LPTIM2,                     /**< Low power timer 2                        */
#if defined(DBGMCU_APB3FZR_DBG_LPTIM3_STOP)
    DBGMCU_PERIPH_LPTIM3,                     /**< Low power timer 3                        */
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM4_STOP)
    DBGMCU_PERIPH_LPTIM4,                     /**< Low power timer 4                        */
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM5_STOP)
    DBGMCU_PERIPH_LPTIM5,                     /**< Low power timer 5                        */
#endif
#if defined(DBGMCU_APB3FZR_DBG_LPTIM6_STOP)
    DBGMCU_PERIPH_LPTIM6,                     /**< Low power timer 6                        */
#endif

    DBGMCU_PERIPH_RTC,                        /**< Real time clock                          */

    DBGMCU_PERIPH_I2C1_TIMEOUT,               /**< I2C1 SMBUS timeout                       */
    DBGMCU_PERIPH_I2C2_TIMEOUT,               /**< I2C2 SMBUS timeout                       */
#if defined(DBGMCU_APB3FZR_DBG_I2C3_STOP)
    DBGMCU_PERIPH_I2C3_TIMEOUT,               /**< I2C3 SMBUS timeout                       */
#endif
#if defined(DBGMCU_APB3FZR_DBG_I2C4_STOP)
    DBGMCU_PERIPH_I2C4_TIMEOUT,               /**< I2C4 SMBUS timeout                       */
#endif
    DBGMCU_PERIPH_I3C1_TIMEOUT,               /**< I3C1 SCL stall timeout counter           */
#if defined(DBGMCU_APB3FZR_DBG_I3C2_STOP)
    DBGMCU_PERIPH_I3C2_TIMEOUT,               /**< I3C2 SCL stall timeout counter           */
#endif

    DBGMCU_PERIPH_GPDMA1_CH0,                 /**< General purpose DMA1 channel 0           */
    DBGMCU_PERIPH_GPDMA1_CH1,                 /**< General purpose DMA1 channel 1           */
    DBGMCU_PERIPH_GPDMA1_CH2,                 /**< General purpose DMA1 channel 2           */
    DBGMCU_PERIPH_GPDMA1_CH3,                 /**< General purpose DMA1 channel 3           */
    DBGMCU_PERIPH_GPDMA1_CH4,                 /**< General purpose DMA1 channel 4           */
    DBGMCU_PERIPH_GPDMA1_CH5,                 /**< General purpose DMA1 channel 5           */
    DBGMCU_PERIPH_GPDMA1_CH6,                 /**< General purpose DMA1 channel 6           */
    DBGMCU_PERIPH_GPDMA1_CH7,                 /**< General purpose DMA1 channel 7           */
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA1_CH8_STOP)
    DBGMCU_PERIPH_GPDMA1_CH8,                 /**< General purpose DMA1 channel 8           */
    DBGMCU_PERIPH_GPDMA1_CH9,                 /**< General purpose DMA1 channel 9           */
    DBGMCU_PERIPH_GPDMA1_CH10,                /**< General purpose DMA1 channel 10          */
    DBGMCU_PERIPH_GPDMA1_CH11,                /**< General purpose DMA1 channel 11          */
#endif
    DBGMCU_PERIPH_GPDMA2_CH0,                 /**< General purpose DMA2 channel 0           */
    DBGMCU_PERIPH_GPDMA2_CH1,                 /**< General purpose DMA2 channel 1           */
    DBGMCU_PERIPH_GPDMA2_CH2,                 /**< General purpose DMA2 channel 2           */
    DBGMCU_PERIPH_GPDMA2_CH3,                 /**< General purpose DMA2 channel 3           */
    DBGMCU_PERIPH_GPDMA2_CH4,                 /**< General purpose DMA2 channel 4           */
    DBGMCU_PERIPH_GPDMA2_CH5,                 /**< General purpose DMA2 channel 5           */
    DBGMCU_PERIPH_GPDMA2_CH6,                 /**< General purpose DMA2 channel 6           */
    DBGMCU_PERIPH_GPDMA2_CH7,                 /**< General purpose DMA2 channel 7           */
#if defined(DBGMCU_AHB1FZR_DBG_GPDMA2_CH8_STOP)
    DBGMCU_PERIPH_GPDMA2_CH8,                 /**< General purpose DMA2 channel 8           */
    DBGMCU_PERIPH_GPDMA2_CH9,                 /**< General purpose DMA2 channel 9           */
    DBGMCU_PERIPH_GPDMA2_CH10,                /**< General purpose DMA2 channel 10          */
    DBGMCU_PERIPH_GPDMA2_CH11,                /**< General purpose DMA2 channel 11          */
#endif
    DBGMCU_PERIPH_CNT                         /**< Count of the freezable peripherals of the device line */
}   dbgMcu_Periph_t;


/** \brief Type used to store DBGMCU configuration */
typedef struct
{
    /** Debug in Sleep mode. STM32H5 has no setting for it, \ref DBGMCU_FUNCTION_ACTIVE is refused. */
    dbgMcu_FunctionState_t LowPowerSleep;

    /** Debug in Stop mode (all clocks and oscillators keep running in Stop mode, higher consumption) */
    dbgMcu_FunctionState_t LowPowerStop;

    /** Debug in Standby mode (core supply and clocks are maintained, no IO state retention, system
     *  reset on exit from Standby mode, higher consumption) */
    dbgMcu_FunctionState_t LowPowerStandby;

    /** Trace pins and trace clock (debugger side of the trace) */
    dbgMcu_TraceMode_t     TraceMode;

    /** Freeze of the peripherals while the core is halted by the debugger, indexed by
     *  \ref dbgMcu_Periph_t. \ref DBGMCU_FUNCTION_ACTIVE stops the peripheral, \ref DBGMCU_FUNCTION_INACTIVE
     *  lets it run (the freeze bit is cleared). */
    dbgMcu_FunctionState_t Freeze[ DBGMCU_PERIPH_CNT ];
}   dbgMcu_Config_t;

/* ========================== EXPORTED VARIABLES ============================ */

/* ========================= EXPORTED FUNCTIONS ============================= */


#endif /* DBGMCU_DBGMCU_TYPES_H */
