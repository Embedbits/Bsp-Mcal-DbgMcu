/**
 * \author Mr.Nobody
 * \file DbgMcu.h
 * \ingroup DbgMcu
 * \brief DbgMcu module common functionality header file.
 *
 * This file contains the common functionality used internally by the module,
 * and shall provide interface between the module and the application.
 *
 */

#ifndef DBGMCU_DBGMCU_H
#define DBGMCU_DBGMCU_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "DbgMcu_Types.h"          /* Module types definitions       */
#include "DbgMcu_Reg.h"            /* Register identifiers           */
/* ============================= TYPEDEFS =================================== */

/** \brief Freeze item: the register and the bit which stops the peripheral while the core is halted */
typedef struct
{
    dbgMcu_Periph_t PeriphId; /**< Peripheral (position of the item in the table)    */
    dbgMcu_RegId_t  RegId;    /**< Freeze register of the peripheral                 */
    uint32_t        BitMask;  /**< Freeze bit of the peripheral in the register      */
}   dbgMcu_FreezeItem_t;

/* ========================= SYMBOLIC CONSTANTS ============================= */

/* ========================= EXPORTED MACROS ================================ */

/* ========================= EXPORTED VARIABLES ============================= */

/* ======================== EXPORTED FUNCTIONS ============================== */

#ifdef __cplusplus
}
#endif

#endif /* DBGMCU_DBGMCU_H */
