/**
 * \author Mr.Nobody
 * \file ItTest_DbgMcu.c
 * \ingroup DbgMcu
 * \brief Integration tests of Microcontroller Debug Unit (DBGMCU) module on target.
 *
 * DbgMcu module runs on the MCU with the real DBGMCU. Tests verify behavior which cannot be
 * verified by unit tests (emulated registers): the identification of the device line, the
 * freeze bits and the low-power debug bits which stay as written (the write is accepted by the
 * debug state of the device), the debug state reported by the hardware and the registers which are
 * reset only by a power-on reset.
 *
 * Used resources:
 * - DBGMCU (identification, DBGMCU_CR, freeze registers, DBGMCU_SR), no clock, no interrupt
 * - no external wiring
 *
 * \note The DBGMCU registers are reset only by a power-on reset, a system reset between the
 *       test cases does not clear them. Every test case ends with DbgMcu_Deinit() which clears
 *       the bits owned by the module.
 * \note The behavior while the core is halted by the debugger (the frozen timer and watchdog do not
 *       count, the debug connection survives the Stop mode) needs a debugger which halts the core
 *       and is therefore not part of the automatic test cases (the target can not halt itself).
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "IntegrationTesting.h"             /* Integration testing on target  */
#include "DbgMcu_Port.h"                    /* Module under test              */
#include "Nvic_Port.h"                      /* System reset                   */
#include "Stm32.h"                          /* DBGMCU registers (reset test)  */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

static void             ItDbgMcu_Init_Default      ( void );

/* ========================= SYMBOLIC CONSTANTS ============================= */

/** Device identifier of the device line the firmware is built for (RM0481, RM0492, RM0517) */
#if defined(STM32H503xx)
#define IT_DBGMCU_DEVICE_ID                 ( DBGMCU_DEVICE_ID_STM32H503 )
#elif defined(STM32H523xx) || \
      defined(STM32H533xx)
#define IT_DBGMCU_DEVICE_ID                 ( DBGMCU_DEVICE_ID_STM32H523_H533 )
#elif defined(STM32H543xx) || \
      defined(STM32H553xx)
#define IT_DBGMCU_DEVICE_ID                 ( DBGMCU_DEVICE_ID_STM32H543_H553 )
#elif defined(STM32H562xx) || \
      defined(STM32H563xx) || \
      defined(STM32H573xx)
#define IT_DBGMCU_DEVICE_ID                 ( DBGMCU_DEVICE_ID_STM32H562_H563_H573 )
#elif defined(STM32H5E4xx) || \
      defined(STM32H5E5xx) || \
      defined(STM32H5F4xx) || \
      defined(STM32H5F5xx)
#define IT_DBGMCU_DEVICE_ID                 ( DBGMCU_DEVICE_ID_STM32H5EXX_H5FXX )
#else
#error "DbgMcu integration tests: unknown STM32H5 device line"
#endif

/** Lowest documented silicon revision (all revisions of the reference manuals are in the range 0x1000 - 0x1FFF) */
#define IT_DBGMCU_REVISION_MIN              ( DBGMCU_REVISION_ID_A )

/** Highest value of the documented revision range */
#define IT_DBGMCU_REVISION_MAX              ( 0x1FFFu )

/** Number of the stage in which the registers are set before the system reset (stage 1 follows it) */
#define IT_DBGMCU_STAGE_SET_REGISTERS       ( 0u )

/* ========================== LOCAL VARIABLES =============================== */

/* ============================= TEST SETUP ================================= */

void setUp( void )
{
    /* DBGMCU registers are not reset by the system reset between the test cases,
       the tests start with DbgMcu_Init() which writes the whole state of the module */
}


void tearDown( void )
{
    /* Bits owned by the module are cleared (they would survive the system reset) */
    (void)DbgMcu_Deinit();
}

/* =============================== TESTS ==================================== */

/*------------------------------ Identification ------------------------------*/

/**
 * \brief   The device identifier read on the target matches the device line of the firmware.
 *
 * \details Reads the identifier without the initialization of the module.
 *
 * \par Expected results
 * - DbgMcu_Get_DeviceId() returns DBGMCU_REQUEST_OK and the identifier of the device line
 *   (STM32H503 0x474, H523/H533 0x478, H543/H553 0x47C, H562/H563/H573 0x484, H5Exx/H5Fxx 0x47A).
 */
void It_DbgMcu_Get_DeviceId_OnTarget_MatchesDeviceLine( void )
{
    dbgMcu_DeviceId_t deviceId = 0u;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DeviceId( &deviceId ) );
    TEST_ASSERT_EQUAL_HEX16( IT_DBGMCU_DEVICE_ID, deviceId );
}


/**
 * \brief   The silicon revision read on the target is in the range of the documented revisions.
 *
 * \details Reads the revision without the initialization of the module.
 *
 * \par Expected results
 * - DbgMcu_Get_RevisionId() returns DBGMCU_REQUEST_OK and a revision in the range 0x1000 - 0x1FFF
 *   (A 0x1000, Z 0x1001, Y 0x1002, X 0x1007, W 0x100F).
 */
void It_DbgMcu_Get_RevisionId_OnTarget_InDocumentedRange( void )
{
    dbgMcu_RevisionId_t revisionId = 0u;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_RevisionId( &revisionId ) );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16( IT_DBGMCU_REVISION_MIN, revisionId );
    TEST_ASSERT_LESS_OR_EQUAL_UINT16( IT_DBGMCU_REVISION_MAX, revisionId );
}

/*--------------------------- Initialization and freeze ----------------------*/

/**
 * \brief   DbgMcu_Init() with the default configuration freezes nothing.
 *
 * \details Initializes the module with the default configuration (the state of the registers
 *          written by an earlier test case or by the debugger is overwritten), reads the freeze of
 *          every peripheral and the low-power debug.
 *
 * \par Expected results
 * - DbgMcu_Init() returns DBGMCU_REQUEST_OK, every peripheral reads DBGMCU_FUNCTION_INACTIVE,
 *   the debug in the Stop and Standby mode reads DBGMCU_FUNCTION_INACTIVE.
 */
void It_DbgMcu_Init_DefaultConfig_NothingFrozen( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_ACTIVE;

    ItDbgMcu_Init_Default();

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        state = DBGMCU_FUNCTION_ACTIVE;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( (dbgMcu_Periph_t)periphIdx, &state ) );
        TEST_ASSERT_EQUAL_MESSAGE( DBGMCU_FUNCTION_INACTIVE,
                                   state,
                                   "peripheral frozen after the default configuration" );
    }

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
}


/**
 * \brief   DbgMcu_Init() freezes the requested peripherals on the hardware.
 *
 * \details Requests the freeze of IWDG, WWDG, TIM2 and RTC together with the debug in the Stop mode.
 *
 * \par Expected results
 * - DbgMcu_Init() returns DBGMCU_REQUEST_OK (every written register reads back as written).
 * - The requested peripherals and the Stop mode read DBGMCU_FUNCTION_ACTIVE, TIM3 and the Standby
 *   mode read DBGMCU_FUNCTION_INACTIVE.
 */
void It_DbgMcu_Init_FreezeTable_ReadsBackRequestedPeripherals( void )
{
    dbgMcu_Config_t        config;
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );

    config.LowPowerStop                 = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_IWDG ] = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_WWDG ] = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_TIM2 ] = DBGMCU_FUNCTION_ACTIVE;
    config.Freeze[ DBGMCU_PERIPH_RTC  ] = DBGMCU_FUNCTION_ACTIVE;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_IWDG, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_WWDG, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_TIM2, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_RTC, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( DBGMCU_PERIPH_TIM3, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
}


/**
 * \brief   The freeze bit of every peripheral of the device line can be set and cleared on the hardware.
 *
 * \details For every item of the list: freezes the peripheral, reads the state, releases it and reads the
 *          state. The module verifies every write by a read-back, a freeze bit which the device does not
 *          accept (for example secure peripheral with the secure invasive debug disabled) fails the test.
 *
 * \par Expected results
 * - DbgMcu_Set_PeriphFreeze() returns DBGMCU_REQUEST_OK for both states of every peripheral,
 *   DbgMcu_Get_PeriphFreeze() returns the state which was set.
 */
void It_DbgMcu_Set_PeriphFreeze_EveryPeripheral_ReadsBackOnHardware( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    ItDbgMcu_Init_Default();

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        const dbgMcu_Periph_t periph = (dbgMcu_Periph_t)periphIdx;

        TEST_ASSERT_EQUAL_MESSAGE( DBGMCU_REQUEST_OK,
                                   DbgMcu_Set_PeriphFreeze( periph, DBGMCU_FUNCTION_ACTIVE ),
                                   "freeze of a peripheral not accepted" );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

        TEST_ASSERT_EQUAL_MESSAGE( DBGMCU_REQUEST_OK,
                                   DbgMcu_Set_PeriphFreeze( periph, DBGMCU_FUNCTION_INACTIVE ),
                                   "release of a peripheral not accepted" );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( periph, &state ) );
        TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );
    }
}


/**
 * \brief   DbgMcu_Set_PeriphFreezeAll() freezes and releases all peripherals on the hardware.
 *
 * \details Freezes all peripherals and reads the state of every one, releases all of them and reads again.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK for both calls, every peripheral reads DBGMCU_FUNCTION_ACTIVE after the freeze and
 *   DBGMCU_FUNCTION_INACTIVE after the release.
 */
void It_DbgMcu_Set_PeriphFreezeAll_ActiveAndInactive_ReadsBackEveryPeripheral( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    ItDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_ACTIVE ) );

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        state = DBGMCU_FUNCTION_INACTIVE;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( (dbgMcu_Periph_t)periphIdx, &state ) );
        TEST_ASSERT_EQUAL_MESSAGE( DBGMCU_FUNCTION_ACTIVE, state, "peripheral not frozen by the freeze of all" );
    }

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreezeAll( DBGMCU_FUNCTION_INACTIVE ) );

    for( uint32_t periphIdx = 0u; DBGMCU_PERIPH_CNT > periphIdx; periphIdx++ )
    {
        state = DBGMCU_FUNCTION_ACTIVE;

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_PeriphFreeze( (dbgMcu_Periph_t)periphIdx, &state ) );
        TEST_ASSERT_EQUAL_MESSAGE( DBGMCU_FUNCTION_INACTIVE, state, "peripheral not released by the release of all" );
    }
}

/*------------------------------ Low-power debug -----------------------------*/

/**
 * \brief   The debug in the Stop and the Standby mode can be set and cleared on the hardware.
 *
 * \details Enables and disables the debug in the Stop mode and in the Standby mode, then requests the Sleep mode
 *          (STM32H5 has no setting for it). The core does not enter the low-power modes.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK for the Stop and Standby mode (the write reads back), the state follows the request.
 * - DBGMCU_REQUEST_ERROR for the Sleep mode (no setting).
 */
void It_DbgMcu_Set_LowPowerDebug_StopAndStandby_ReadsBackOnHardware( void )
{
    dbgMcu_FunctionState_t state = DBGMCU_FUNCTION_INACTIVE;

    ItDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STANDBY, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_ACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_INACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STOP, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STANDBY, DBGMCU_FUNCTION_INACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_LowPowerDebug( DBGMCU_LP_STANDBY, &state ) );
    TEST_ASSERT_EQUAL( DBGMCU_FUNCTION_INACTIVE, state );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_ERROR, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_SLEEP, DBGMCU_FUNCTION_ACTIVE ) );
}

/*-------------------------------- Trace and state ---------------------------*/

/**
 * \brief   The asynchronous trace assignment is accepted by the hardware.
 *
 * \details Assigns the trace pins for the asynchronous mode (TRACESWO) and disables the trace again.
 *          The sources of the trace (Core module) are not configured, only the debugger side is set.
 *
 * \par Expected results
 * - DBGMCU_REQUEST_OK for both requests (DBGMCU_CR reads back as written).
 */
void It_DbgMcu_Set_TraceEnable_Async_AcceptedByHardware( void )
{
    ItDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_ASYNC ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_TraceEnable( DBGMCU_TRACE_DISABLED ) );
}


/**
 * \brief   The debug state reports the open debug access of the board.
 *
 * \details Reads the debug state of the device which is programmed and debugged by the test host (open
 *          device, the system access port is enabled after the reset).
 *
 * \par Expected results
 * - DbgMcu_Get_DebugState() returns DBGMCU_REQUEST_OK, the state has the bit DBGMCU_DEBUG_STATE_ACCESS_ENABLED.
 */
void It_DbgMcu_Get_DebugState_OnTarget_AccessEnabled( void )
{
    dbgMcu_DebugState_t state = DBGMCU_DEBUG_STATE_NONE;

    ItDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DebugState( &state ) );
    TEST_ASSERT_BITS_HIGH( DBGMCU_DEBUG_STATE_ACCESS_ENABLED, state );
}

/*-------------------------------- Deinitialization --------------------------*/

/**
 * \brief   DbgMcu_Deinit() clears the bits of the module on the hardware.
 *
 * \details Freezes IWDG and enables the debug in the Stop mode, deinitializes the module and reads the
 *          registers directly (the getters of the module need the initialization).
 *
 * \par Expected results
 * - DbgMcu_Deinit() returns DBGMCU_REQUEST_OK, the IWDG freeze bit and DBG_STOP read 0.
 */
void It_DbgMcu_Deinit_AfterFreeze_ClearsBitsOfModule( void )
{
    ItDbgMcu_Init_Default();

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_IWDG, DBGMCU_FUNCTION_ACTIVE ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Deinit() );

    TEST_ASSERT_BITS_LOW( DBGMCU_APB1FZR1_DBG_IWDG_STOP, DBGMCU->APB1FZR1 );
    TEST_ASSERT_BITS_LOW( DBGMCU_CR_DBG_STOP, DBGMCU->CR );
}

/*------------------------------- System reset -------------------------------*/

/**
 * \brief   The freeze bits and the low-power debug bits survive a system reset.
 *
 * \details
 * 1. Stage 0: expects reset, enables the debug in the Stop mode and freezes IWDG, requests system reset.
 * 2. Stage 1 (after reset): reads the registers directly (the module is not initialized after the reset).
 *
 * \par Expected results
 * 1. MCU is reset, code after the request is not executed.
 * 2. The IWDG freeze bit and DBG_STOP are still set (the DBGMCU registers are reset only by a power-on reset).
 */
void It_DbgMcu_Registers_SystemReset_KeepFreezeAndLowPowerBits( void )
{
    const integrationTesting_Stage_t stage = IntegrationTesting_Get_Stage();

    if( IT_DBGMCU_STAGE_SET_REGISTERS == stage )
    {
        ItDbgMcu_Init_Default();

        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_PeriphFreeze( DBGMCU_PERIPH_IWDG, DBGMCU_FUNCTION_ACTIVE ) );
        TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Set_LowPowerDebug( DBGMCU_LP_STOP, DBGMCU_FUNCTION_ACTIVE ) );

        IntegrationTesting_Set_ResetExpected();

        Nvic_Set_SystemReset();

        TEST_FAIL_MESSAGE( "System reset did not occur" );
    }
    else
    {
        TEST_ASSERT_BITS_HIGH( DBGMCU_APB1FZR1_DBG_IWDG_STOP, DBGMCU->APB1FZR1 );
        TEST_ASSERT_BITS_HIGH( DBGMCU_CR_DBG_STOP, DBGMCU->CR );
    }
}

/* ========================== LOCAL FUNCTIONS =============================== */

/**
 * \brief Initializes the module with the default configuration and checks the result.
 */
static void ItDbgMcu_Init_Default( void )
{
    dbgMcu_Config_t config;

    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( DBGMCU_REQUEST_OK, DbgMcu_Init( &config ) );
}
