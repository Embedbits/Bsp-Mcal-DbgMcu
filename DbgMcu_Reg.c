/**
 * \author Mr.Nobody
 * \file DbgMcu_Reg.c
 * \ingroup DbgMcu
 * \brief DbgMcu module Reg component functionality.
 *
 * Register access of the module: generic accessors (bounds checked register ID,
 * writes to read only registers are refused) and the verified write of the register
 * bits. The registers are written only here, the masks come from the CMSIS device header.
 *
 * Properties of the registers, which the protocol respects (RM0481, RM0492,
 * RM0517 - DBGMCU chapter):
 * - The DBGMCU registers (except the status ones) are reset only by a power-on reset.
 * - The access of the software to DBGMCU_CR and to the freeze registers is ignored on the
 *   devices where the debug is closed (RM0481, RM0517: after a successful authentication only).
 * - The freeze bit of a TrustZone peripheral (or of a DMA channel) can not be modified while
 *   the secure invasive debug is disabled (SPIDEN = 0) and the peripheral is secure.
 * A write which does not stay is therefore found by the read-back after every write.
 *
 */
/* ============================== INCLUDES ================================== */
#include "DbgMcu_Reg.h"                     /* Self include                   */
#include "DbgMcu_Types.h"                   /* Module types definitions       */
#include "Stm32.h"                          /* DBGMCU registers definition    */
/* ============================== TYPEDEFS ================================== */

/** \brief Structure type used to store physical address of DBGMCU registers.
 * This array is used to reduce size of configuration array. With this style
 * the registers are referenced through enumeration.
 *
 */
typedef struct
{
    dbgMcu_RegId_t      RegId;   /**< Peripheral register ID                   */
    volatile uint32_t * RegAddr; /**< Peripheral register address              */
    dbgMcu_RegAccess_t  Access;  /**< Access of the software to the register   */
}   dbgMcu_RegList_t;

/* ======================== FORWARD DECLARATIONS ============================ */

static dbgMcu_RequestState_t    DbgMcu_Reg_Wait_RegVal  ( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue );
static dbgMcu_RequestState_t    DbgMcu_Reg_Check_Write  ( dbgMcu_RegId_t regId );

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** \brief Count of read-back iterations of a written register value */
#define DBGMCU_REG_READBACK_TIMEOUT_RAW     ( 0x1000u )

/* =============================== MACROS =================================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

/** \brief DBGMCU registers configuration structure
 *
 * This structure is used to store addresses of DBGMCU registers.
 * It is used to reduce code size while referring to DBGMCU registers.
 */
static const dbgMcu_RegList_t           dbgMcu_RegList[] =
{
    /* Device identity code register */
    { .RegId = DBGMCU_REG_IDCODE      , .RegAddr = &(DBGMCU->IDCODE       ), .Access = DBGMCU_REG_ACCESS_READ       },
    /* Configuration register */
    { .RegId = DBGMCU_REG_CR          , .RegAddr = &(DBGMCU->CR           ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* APB1L freeze register */
    { .RegId = DBGMCU_REG_APB1FZR1    , .RegAddr = &(DBGMCU->APB1FZR1     ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* APB1H freeze register */
    { .RegId = DBGMCU_REG_APB1FZR2    , .RegAddr = &(DBGMCU->APB1FZR2     ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* APB2 freeze register */
    { .RegId = DBGMCU_REG_APB2FZR     , .RegAddr = &(DBGMCU->APB2FZR      ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* APB3 freeze register */
    { .RegId = DBGMCU_REG_APB3FZR     , .RegAddr = &(DBGMCU->APB3FZR      ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* AHB1 freeze register */
    { .RegId = DBGMCU_REG_AHB1FZR     , .RegAddr = &(DBGMCU->AHB1FZR      ), .Access = DBGMCU_REG_ACCESS_READ_WRITE },
    /* Status register */
    { .RegId = DBGMCU_REG_SR          , .RegAddr = &(DBGMCU->SR           ), .Access = DBGMCU_REG_ACCESS_READ       },
    /* Authentication acknowledge register */
    { .RegId = DBGMCU_REG_DBG_AUTH_ACK, .RegAddr = &(DBGMCU->DBG_AUTH_ACK ), .Access = DBGMCU_REG_ACCESS_READ       },
};

_Static_assert( (sizeof(dbgMcu_RegList) / sizeof(dbgMcu_RegList_t)) == DBGMCU_REG_CNT,
                "DbgMcu_Reg: dbgMcu_RegList has incorrect size." );

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Sets the register bit with given mask.
 *
 * \note  Writes to a read only register are refused.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void DbgMcu_Set_RegBit( dbgMcu_RegId_t regId, uint32_t bitMask )
{
    const dbgMcu_RequestState_t writeState = DbgMcu_Reg_Check_Write( regId );

    if( DBGMCU_REQUEST_OK == writeState )
    {
        *dbgMcu_RegList[regId].RegAddr |= bitMask;
    }
    else
    {
        /* Invalid register ID or read only register, no action required */
    }
}


/**
 * \brief Resets the register bit with given mask.
 *
 * \note  Writes to a read only register are refused.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void DbgMcu_Reset_RegBit( dbgMcu_RegId_t regId, uint32_t bitMask )
{
    const dbgMcu_RequestState_t writeState = DbgMcu_Reg_Check_Write( regId );

    if( DBGMCU_REQUEST_OK == writeState )
    {
        *dbgMcu_RegList[regId].RegAddr &= ~bitMask;
    }
    else
    {
        /* Invalid register ID or read only register, no action required */
    }
}


/**
 * \brief Gets the register bit value with given mask.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask to apply to the register
 * \return Register bit value with applied mask, 0 for invalid register ID
 */
uint32_t DbgMcu_Get_RegBit( dbgMcu_RegId_t regId, uint32_t bitMask )
{
    uint32_t retValue = 0u;

    if( DBGMCU_REG_CNT > regId )
    {
        retValue = ( *dbgMcu_RegList[regId].RegAddr & bitMask );
    }
    else
    {
        /* Invalid register ID, zero is returned */
        retValue = 0u;
    }

    return ( retValue );
}


/**
 * \brief Updates the register value with given mask and value.
 *
 * \note  Writes to a read only register are refused.
 *
 * \param regId    [in]: DBGMCU register ID
 * \param regMask  [in]: Mask to apply to the register
 * \param regValue [in]: Value to set in the register
 */
void DbgMcu_Set_RegVal( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    const dbgMcu_RequestState_t writeState = DbgMcu_Reg_Check_Write( regId );

    if( DBGMCU_REQUEST_OK == writeState )
    {
        /* Set the register value with mask */
        *dbgMcu_RegList[regId].RegAddr = ( ( *dbgMcu_RegList[regId].RegAddr & ~regMask ) |
                                           ( regValue & regMask ) );
    }
    else
    {
        /* Invalid register ID or read only register, no action required */
    }
}


/**
 * \brief Gets the register value with given mask.
 *
 * \param regId   [in]: DBGMCU register ID.
 * \param regMask [in]: Mask to apply to the register
 * \return Register value with applied mask, 0 for invalid register ID
 */
uint32_t DbgMcu_Get_RegVal( dbgMcu_RegId_t regId, uint32_t regMask )
{
    uint32_t retValue = 0u;

    if( DBGMCU_REG_CNT > regId )
    {
        /* Get the register value with mask */
        retValue = ( *dbgMcu_RegList[regId].RegAddr & regMask );
    }
    else
    {
        /* Invalid register ID, zero is returned */
        retValue = 0u;
    }

    return ( retValue );
}


/**
 * \brief Writes the bits of the register with given mask and verifies them by read-back.
 *
 * The bits outside of the mask keep their value (read-modify-write). The value is
 * checked before the write: a value with a bit outside of the mask is refused.
 *
 * \param regId    [in]: DBGMCU register ID
 * \param regMask  [in]: Mask of the written bits
 * \param regValue [in]: Value of the masked bits (already positioned)
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if the value was read back within the timeout.
 *         Returns \ref DBGMCU_REQUEST_ERROR for an invalid register ID, a read only
 *         register or a value outside of the mask (nothing is written), or when the
 *         read-back does not match (the write was ignored by the hardware).
 */
dbgMcu_RequestState_t DbgMcu_Reg_Set_Verified( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t writeState = DbgMcu_Reg_Check_Write( regId );

    if( ( DBGMCU_REQUEST_OK == writeState              ) &&
        ( 0u                == ( regValue & ~regMask ) )    )
    {
        DbgMcu_Set_RegVal( regId, regMask, regValue );

        retState = DbgMcu_Reg_Wait_RegVal( regId, regMask, regValue );
    }
    else
    {
        /* Invalid register, or value out of the mask, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Sets or clears the register bits with given mask and verifies them by read-back.
 *
 * \param regId   [in]: DBGMCU register ID
 * \param bitMask [in]: Mask of the bits
 * \param state   [in]: \ref DBGMCU_FUNCTION_ACTIVE sets the bits, \ref DBGMCU_FUNCTION_INACTIVE clears them
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if the bits read back as requested within the timeout.
 *         Returns \ref DBGMCU_REQUEST_ERROR for an invalid register ID, a read only register
 *         or an invalid state (nothing is written), or when the read-back does not match.
 */
dbgMcu_RequestState_t DbgMcu_Reg_Set_BitState( dbgMcu_RegId_t regId, uint32_t bitMask, dbgMcu_FunctionState_t state )
{
    dbgMcu_RequestState_t       retState   = DBGMCU_REQUEST_ERROR;
    const dbgMcu_RequestState_t writeState = DbgMcu_Reg_Check_Write( regId );

    if( DBGMCU_REQUEST_OK == writeState )
    {
        if( DBGMCU_FUNCTION_ACTIVE == state )
        {
            DbgMcu_Set_RegBit( regId, bitMask );

            retState = DbgMcu_Reg_Wait_RegVal( regId, bitMask, bitMask );
        }
        else if( DBGMCU_FUNCTION_INACTIVE == state )
        {
            DbgMcu_Reset_RegBit( regId, bitMask );

            retState = DbgMcu_Reg_Wait_RegVal( regId, bitMask, 0u );
        }
        else
        {
            /* Invalid state, nothing is written */
            retState = DBGMCU_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid register or read only register, nothing is written */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Waits until the register (bit field) reads back the required value.
 *
 * \param regId    [in]: DBGMCU register ID
 * \param regMask  [in]: Mask of the checked bits
 * \param regValue [in]: Required value of the masked bits
 *
 * \return Returns \ref DBGMCU_REQUEST_OK if the value was read back within the
 *         timeout. Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Reg_Wait_RegVal( dbgMcu_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    for( uint32_t iterationCnt = 0u; DBGMCU_REG_READBACK_TIMEOUT_RAW > iterationCnt; iterationCnt ++ )
    {
        const uint32_t readValue = DbgMcu_Get_RegVal( regId, regMask );

        if( ( regValue & regMask ) == readValue )
        {
            retState = DBGMCU_REQUEST_OK;
            break;
        }
        else
        {
            /* Register has not been updated yet */
            retState = DBGMCU_REQUEST_ERROR;
        }
    }

    return ( retState );
}


/**
 * \brief Checks that the register can be written by the software.
 *
 * \param regId [in]: DBGMCU register ID
 *
 * \return Returns \ref DBGMCU_REQUEST_OK for a valid register ID of a writable register.
 *         Otherwise returns \ref DBGMCU_REQUEST_ERROR.
 */
static dbgMcu_RequestState_t DbgMcu_Reg_Check_Write( dbgMcu_RegId_t regId )
{
    dbgMcu_RequestState_t retState = DBGMCU_REQUEST_ERROR;

    if( DBGMCU_REG_CNT > regId )
    {
        if( DBGMCU_REG_ACCESS_READ_WRITE == dbgMcu_RegList[regId].Access )
        {
            retState = DBGMCU_REQUEST_OK;
        }
        else
        {
            /* Read only register */
            retState = DBGMCU_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid register ID */
        retState = DBGMCU_REQUEST_ERROR;
    }

    return ( retState );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */
