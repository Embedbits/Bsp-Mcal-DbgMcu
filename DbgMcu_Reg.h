/**
 * \author Mr.Nobody
 * \file DbgMcu_Reg.h
 * \ingroup DbgMcu
 * \brief DbgMcu module Reg component header file.
 *
 * This component contains functionality for register operations. It is private
 * to the module (the register access is not shared with other modules).
 *
 */

#ifndef DBGMCU_REG_DBGMCU_REG_H
#define DBGMCU_REG_DBGMCU_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "DbgMcu_Types.h"                   /* Module types definition        */
/* ============================= TYPEDEFS =================================== */

/** \brief List of DBGMCU registers
 *
 * All registers exist on all STM32H5 lines (the layout of the freeze registers is common,
 * the freeze bits differ per line, see \ref dbgMcu_Periph_t).
 */
typedef enum
{
    DBGMCU_REG_IDCODE = 0u,  /**< Device identity code register                      */
    DBGMCU_REG_CR,           /**< Configuration register                             */
    DBGMCU_REG_APB1FZR1,     /**< APB1L peripheral freeze register (DBGMCU_APB1LFZR) */
    DBGMCU_REG_APB1FZR2,     /**< APB1H peripheral freeze register (DBGMCU_APB1HFZR) */
    DBGMCU_REG_APB2FZR,      /**< APB2 peripheral freeze register                    */
    DBGMCU_REG_APB3FZR,      /**< APB3 peripheral freeze register                    */
    DBGMCU_REG_AHB1FZR,      /**< AHB1 peripheral freeze register                    */
    DBGMCU_REG_SR,           /**< Status register (access ports)                     */
    DBGMCU_REG_DBG_AUTH_ACK, /**< Debug authentication mailbox acknowledge register  */
    DBGMCU_REG_CNT           /**< Count of available DBGMCU registers                */
}   dbgMcu_RegId_t;


/** \brief Access of the software to a register */
typedef enum
{
    DBGMCU_REG_ACCESS_READ = 0u, /**< Register is read only, writes are refused */
    DBGMCU_REG_ACCESS_READ_WRITE /**< Register can be read and written          */
}   dbgMcu_RegAccess_t;

/* ========================= SYMBOLIC CONSTANTS ============================= */

/* ========================= EXPORTED MACROS ================================ */

/* ========================= EXPORTED VARIABLES ============================= */

/* ======================== EXPORTED FUNCTIONS ============================== */

/* ---------------------------- Generic accessors --------------------------- */

void                        DbgMcu_Set_RegBit           ( dbgMcu_RegId_t regId, uint32_t bitMask );
void                        DbgMcu_Reset_RegBit         ( dbgMcu_RegId_t regId, uint32_t bitMask );
uint32_t                    DbgMcu_Get_RegBit           ( dbgMcu_RegId_t regId, uint32_t bitMask );

void                        DbgMcu_Set_RegVal           ( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue );
uint32_t                    DbgMcu_Get_RegVal           ( dbgMcu_RegId_t regId, uint32_t regMask );

/* ----------------------------- Register protocol -------------------------- */

dbgMcu_RequestState_t       DbgMcu_Reg_Set_Verified     ( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue );
dbgMcu_RequestState_t       DbgMcu_Reg_Set_BitState     ( dbgMcu_RegId_t regId,
                                                          uint32_t bitMask,
                                                          dbgMcu_FunctionState_t state );

#ifdef __cplusplus
}
#endif

#endif /* DBGMCU_REG_DBGMCU_REG_H */
