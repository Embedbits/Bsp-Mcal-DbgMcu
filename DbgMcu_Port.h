/**
 * \author Mr.Nobody
 * \file DbgMcu_Port.h
 * \ingroup DbgMcu
 * \brief DbgMcu module public functionality
 *
 * This file contains all available public functionality, any other files shall
 * not used outside of the module.
 *
 */

#ifndef DBGMCU_DBGMCU_PORT_H
#define DBGMCU_DBGMCU_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== INCLUDES ================================== */
#include "DbgMcu_Types.h"                   /* Module types definition        */
/* ============================== TYPEDEFS ================================== */

/* ========================== SYMBOLIC CONSTANTS ============================ */

/* ========================== EXPORTED MACROS =============================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* ========================= EXPORTED FUNCTIONS ============================= */

dbgMcu_ModuleVersion_t      DbgMcu_Get_ModuleVersion        ( void );

dbgMcu_RequestState_t       DbgMcu_Init                     ( const dbgMcu_Config_t * const dbgMcuConfig );
dbgMcu_RequestState_t       DbgMcu_Deinit                   ( void );
void                        DbgMcu_Task                     ( void );

dbgMcu_RequestState_t       DbgMcu_Get_DefaultConfig        ( dbgMcu_Config_t * const dbgMcuConfig );

/* ----------------------------- Identification ----------------------------- */

dbgMcu_RequestState_t       DbgMcu_Get_DeviceId             ( dbgMcu_DeviceId_t * const deviceId );
dbgMcu_RequestState_t       DbgMcu_Get_RevisionId           ( dbgMcu_RevisionId_t * const revisionId );

/* ---------------------------- Low-power debug ----------------------------- */

dbgMcu_RequestState_t       DbgMcu_Set_LowPowerDebug        ( dbgMcu_LowPower_t lowPower,
                                                              dbgMcu_FunctionState_t state );
dbgMcu_RequestState_t       DbgMcu_Get_LowPowerDebug        ( dbgMcu_LowPower_t lowPower,
                                                              dbgMcu_FunctionState_t * const state );

/* ---------------------------- Peripheral freeze --------------------------- */

dbgMcu_RequestState_t       DbgMcu_Set_PeriphFreeze         ( dbgMcu_Periph_t periph,
                                                              dbgMcu_FunctionState_t freeze );
dbgMcu_RequestState_t       DbgMcu_Get_PeriphFreeze         ( dbgMcu_Periph_t periph,
                                                              dbgMcu_FunctionState_t * const freeze );
dbgMcu_RequestState_t       DbgMcu_Set_PeriphFreezeAll      ( dbgMcu_FunctionState_t freeze );

/* ---------------------------- Trace and state ----------------------------- */

dbgMcu_RequestState_t       DbgMcu_Set_TraceEnable          ( dbgMcu_TraceMode_t traceMode );
dbgMcu_RequestState_t       DbgMcu_Get_DebugState           ( dbgMcu_DebugState_t * const debugState );

#ifdef __cplusplus
}
#endif

#endif /* DBGMCU_DBGMCU_PORT_H */
