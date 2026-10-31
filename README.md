# DBGMCU Peripheral Driver

This module provides an abstraction layer for the **microcontroller debug unit** (`DBGMCU`) of STM32 MCUs: the identification of the
device and its revision, the **debug in the low-power modes** (the debugger stays connected in Sleep, Stop and Standby), the
**freezing of the peripherals while the core is halted** (the watchdogs, the timers, the RTC, the I2C timeout counters, the DMA
channels) and the enable of the trace pins. It is a low-level MCAL module: it owns the DBGMCU hardware, the application and the
other modules never touch the registers.

The public interface (`DbgMcu_Port.h`, `DbgMcu_Types.h`) is **identical for all supported families**. The implementation
for a family lives in its own branch (`Dev/<Family>` for development, `Releases/<Family>` for the released versions), so
a module or an application written against the interface does not change when the MCU family changes.

This README is the landing page and the **specification of the interface** (master). The family branches contain their
own README with everything that is specific to the family: the instances of the device, their settings and the status of
the implementation.

> **Status: STM32H5 implemented (version 0.1.0).** The interface below is implemented for STM32H5 in `DbgMcu_Port.h` /
> `DbgMcu_Types.h` (branch `Dev/STM32H5`), the registers are accessed only by the private `DbgMcu_Reg.c/h`. Verified: unit
> tests (Unity + RegMem, 54 test cases) on one preset of each of the 12 STM32H5 lines (H503, H523, H533, H543, H553, H562,
> H563, H573, H5E4, H5E5, H5F4, H5F5) and a compilation of the module for all 12 lines. The integration tests
> (`Tests/IntegrationTests`, 11 test cases) are only compiled (`STM32H503xB_IntegrationTest`,
> `STM32H563xI_IntegrationTest`), they have not been run on a board yet. Other families: stories and branches follow.
>
> Notes of the STM32H5 implementation (a difference from the common proposal is marked as *deviation*):
> - **Registers.** DBGMCU is at 0x44024000 for the software (0xE00E4000 for the debugger) on all 12 lines: `DBGMCU_IDCODE`,
>   `DBGMCU_CR`, the freeze registers `APB1LFZR` (CMSIS `APB1FZR1`), `APB1HFZR` (`APB1FZR2`), `APB2FZR`, `APB3FZR`, `AHB1FZR`,
>   `DBGMCU_SR` and `DBGMCU_DBG_AUTH_ACK`. The registers are reset only by a power-on reset, a system reset keeps the freeze
>   bits and the low-power debug bits.
> - **No Rcc, no Nvic (*deviation*).** The DBGMCU is clocked all the time (no enable bit in the RCC registers) and has no interrupt, so
>   the module does not request a clock from the Rcc module and uses no other module. The only dependency is the RAL (CMSIS).
> - **Identification.** `DbgMcu_Get_DeviceId` (12 bits) and `DbgMcu_Get_RevisionId` (16 bits) read `DBGMCU_IDCODE`, they work before
>   `DbgMcu_Init`. Known values are the macros `DBGMCU_DEVICE_ID_xxx` (H503 0x474, H523/H533 0x478, H543/H553 0x47C,
>   H562/H563/H573 0x484, H5Exx/H5Fxx 0x47A) and `DBGMCU_REVISION_ID_xxx` (A 0x1000, Z 0x1001, Y 0x1002, X 0x1007, W 0x100F).
> - **Initialization.** Every function except the identification, `DbgMcu_Get_ModuleVersion`, `DbgMcu_Get_DefaultConfig`,
>   `DbgMcu_Init`, `DbgMcu_Deinit` and `DbgMcu_Task` needs a successful `DbgMcu_Init`. `DbgMcu_Init` checks the whole configuration
>   before the first write and then writes the complete state of the module: the bits of `DBGMCU_CR` (Stop, Standby, trace) and
>   the freeze bits of all peripherals of the line (an inactive entry clears the bit), the bit `DCRT` and the reserved bits are never
>   written. Every write is verified by a read-back; on a mismatch the module is deinitialized and an error is returned.
>   `DbgMcu_Deinit` clears the bits owned by the module (also without a preceding init) and deinitializes the module.
>   `DbgMcu_Task` has nothing to do.
> - **Closed devices.** The access of the software to `DBGMCU_CR` and to the freeze registers is ignored on a device with the debug
>   closed (RM0481, RM0517: accessible after a successful authentication), and the freeze bit of a secure peripheral can not be
>   changed while the secure invasive debug is disabled. The read-back reports it as `DBGMCU_REQUEST_ERROR`.
> - **Low-power debug (*deviation*).** `DBGMCU_CR` has the bits `DBG_STOP` and `DBG_STANDBY` only: the core is clocked in the Sleep mode and
>   the debug connection is kept by the hardware. `DBGMCU_LP_SLEEP` stays in `dbgMcu_LowPower_t`, but `DbgMcu_Set_LowPowerDebug`
>   and `DbgMcu_Get_LowPowerDebug` return `DBGMCU_REQUEST_ERROR` for it and `DbgMcu_Init` refuses `LowPowerSleep = DBGMCU_FUNCTION_ACTIVE`
>   (nothing is written), so the example of this README (`LowPowerSleep` active) is not valid on STM32H5. The debug in the Standby mode
>   disables the IO state retention and the exit from the Standby mode is a system reset.
> - **Freeze list.** `dbgMcu_Periph_t` has the peripherals of the line only (the items exist if the CMSIS header has the freeze bit, which
>   follows the reference manuals): common items are `IWDG`, `WWDG`, `TIM1`, `TIM2`, `TIM3`, `TIM6`, `TIM7`, `LPTIM1`, `LPTIM2`, `RTC`,
>   `I2C1_TIMEOUT`, `I2C2_TIMEOUT`, `I3C1_TIMEOUT` and the channels 0 - 7 of `GPDMA1` and `GPDMA2`. Line specific items: `TIM4`, `TIM5`,
>   `TIM8`, `TIM12`, `TIM15`, `I2C3_TIMEOUT` (not H503), `TIM13`, `TIM14`, `TIM16`, `TIM17`, `I2C4_TIMEOUT`, `LPTIM3` - `LPTIM6` (H562, H563,
>   H573, H5Exx, H5Fxx), `I3C2_TIMEOUT` (H503, H523, H533, H543, H553, H5Exx, H5Fxx), `GPDMA1_CH8` - `GPDMA2_CH11` (H5Exx, H5Fxx). The list has
>   30 items on H503, 36 on H523, H533, H543, H553, 44 on H562, H563, H573 and 53 on H5E4, H5E5, H5F4, H5F5. The note of RM0481 Rev 5
>   names the I3C2 freeze bit for H543/H553 only, the module follows the CMSIS header and the securable peripheral table (I3C2 is on H523
>   and H533 too); TIM13 and TIM14 are not on H523 and H533 (datasheet).
> - **Functions without a signature in the proposal (*deviation*).** The short table lists `DbgMcu_Get_LowPowerDebug`, `DbgMcu_Get_PeriphFreeze` and
>   `DbgMcu_Set_TraceEnable`, the C block does not declare them. The signatures follow the declared ones:
>   `DbgMcu_Get_LowPowerDebug( dbgMcu_LowPower_t lowPower, dbgMcu_FunctionState_t * const state )`,
>   `DbgMcu_Get_PeriphFreeze( dbgMcu_Periph_t periph, dbgMcu_FunctionState_t * const freeze )`,
>   `DbgMcu_Set_TraceEnable( dbgMcu_TraceMode_t traceMode )`. The trace is a list (`DBGMCU_TRACE_DISABLED`, `_ASYNC`, `_SYNC_1BIT`, `_SYNC_2BIT`,
>   `_SYNC_4BIT`: the trace clock, the pin enable and the pin assignment of `DBGMCU_CR`) because the pin assignment is a hardware choice; the
>   configuration member is `TraceMode`.
> - **Debug state (*deviation*).** `dbgMcu_DebugState_t` is a bit mask of `DBGMCU_DEBUG_STATE_ACCESS_ENABLED` (a debug access port is enabled,
>   `DBGMCU_SR`), `DBGMCU_DEBUG_STATE_AUTH_HOST_MSG` and `DBGMCU_DEBUG_STATE_AUTH_DEVICE_MSG` (unread message in the debug authentication
>   mailbox, `DBGMCU_DBG_AUTH_ACK`). The DBGMCU has no indication that a debugger is connected (the enabled port is not a connection),
>   a bit for it is not offered.
> - **Trace.** The trace sources and the instrumentation (`Dwt`, `Itm`) stay in the Core module, which still writes the DBGMCU trace bits
>   itself; the module is not used by it yet.

---

## Intent and purpose

- When a debugger halts the core at a breakpoint, the rest of the chip keeps running: the **independent watchdog resets the MCU**
  after a few hundred milliseconds, the **PWM timers keep driving a motor** with the last duty, the DMA moves data into a buffer that
  nobody reads. Debugging such a system is a fight with the peripherals. The freeze registers of the DBGMCU stop the
  selected peripherals when the core stops. The module offers them as one list (`dbgMcu_Periph_t`) and one call.
- The low-power modes switch off the clocks of the debug logic, so the debugger loses the connection at the first `WFI`. The
  DBGMCU keeps the debug clocks alive in Sleep, Stop and Standby at the cost of a higher consumption; the module offers it as a
  **development-only** setting that is not left on in the released firmware (the configuration is a build option of the application).
- The identification (`DbgMcu_Get_DeviceId`, `DbgMcu_Get_RevisionId`) tells which device and which silicon revision the firmware runs on; it is the
  value the tools use to recognise the chip, and an application can use it to select a workaround of an erratum by the
  revision.
- The module hides the differences between the families: the names and the number of the freeze registers (per bus), the bits,
  the debug domain, the trace pins and the authentication state differ, the interface stays the same.

What the module is **not**: it does not configure the trace and the instrumentation macrocells of the core (the Core module with
`Dwt` and `Itm` does), it does not open the debug port of a closed product (the debug authentication is a protocol of the
product life cycle) and it does not detect the connection of a debugger for the product logic - the module only reports the
state that the hardware shows.

---

## Concept

| Item | Functions | Meaning |
|---|---|---|
| **Identification** | `DbgMcu_Get_DeviceId`, `DbgMcu_Get_RevisionId` | The 12-bit device identifier and the 16-bit revision of the silicon. |
| **Low-power debug** | `DbgMcu_Set_LowPowerDebug` | `DBGMCU_LP_SLEEP`, `DBGMCU_LP_STOP`, `DBGMCU_LP_STANDBY` - keep the debug connection in the selected modes. |
| **Peripheral freeze** | `DbgMcu_Set_PeriphFreeze`, `DbgMcu_Get_PeriphFreeze` | For a peripheral from `dbgMcu_Periph_t` (`DBGMCU_PERIPH_IWDG`, `_WWDG`, `_TIM1`, `_LPTIM1`, `_RTC`, `_I2C1_TIMEOUT`, `_GPDMA1_CH0` ...): stop it while the core is halted, or let it run. |
| **Trace** | `DbgMcu_Set_TraceEnable` | The trace pins and the trace clock (the debugger side of the trace; the sources are configured in the Core module). |
| **Debug state** | `DbgMcu_Get_DebugState` | The state the hardware reports: the debugger connected, the authentication state of the family. |

Rules:

1. The freeze is a property of **debugging**: it is active only while the core is halted by the debugger; in the free running
   firmware without a debugger it has no effect. It is therefore safe to leave in the code, but the module keeps it as a **debug build
   option** (`DbgMcu_Init` applies the table of the configuration).
2. `DbgMcu_Set_PeriphFreeze` of a peripheral the device does not have returns an error and writes no register.
3. The setting of the low-power debug is a **write to the register of the debug domain**; the clock of the DBGMCU registers is requested from
   the RCC module. Leaving the low-power debug enabled raises the consumption in the Stop and the Standby (a measurement of the
   current in the low-power modes is done without it).
4. The identification functions do not depend on the configuration and are usable before `DbgMcu_Init`.
5. All parameters are checked before a register is written; the registers are accessed with the privilege rules of the family.

---

## Supported families

Every family gets its own story, its own tasks and its own branches `Dev/<Family>` and `Releases/<Family>` cut from this
master.

| Family | Branch | Debug unit | Status |
|---|---|---|---|
| STM32H5 | `Dev/STM32H5` | `DBGMCU`: the device and revision identifier, the freeze registers of the AHB1 (GPDMA), APB1 (low and high), APB2 and APB3 peripherals, low-power debug, trace enable, the debug authentication state | implemented (0.1.0) |
| STM32U5 | `Dev/STM32U5` | `DBGMCU` | planned |
| STM32U3 | `Dev/STM32U3` | `DBGMCU` | planned |
| STM32G4, STM32L4 | `Dev/STM32G4`, `Dev/STM32L4` | `DBGMCU`: the freeze registers of the APB1 / APB2 peripherals, the low-power debug bits | planned |
| STM32H7 | `Dev/STM32H7` | `DBGMCU`: the freeze registers of the domains, the trace and the debug clock of the domains | planned |
| STM32F4, STM32F7 | `Dev/STM32F4`, `Dev/STM32F7` | `DBGMCU`: `DBGMCU_CR` (sleep / stop / standby), `APB1_FZ`, `APB2_FZ` | planned |

A function without a meaning in a family stays in the interface, returns `DBGMCU_REQUEST_ERROR` and suppresses the unused
parameters (for example a peripheral the device does not have, the authentication state on a family without it).

---

## Public interface in short (proposal)

| Group | Functions |
|---|---|
| Module management | `DbgMcu_Get_ModuleVersion`, `DbgMcu_Init`, `DbgMcu_Deinit`, `DbgMcu_Task`, `DbgMcu_Get_DefaultConfig` |
| Identification | `DbgMcu_Get_DeviceId`, `DbgMcu_Get_RevisionId` |
| Low-power debug | `DbgMcu_Set_LowPowerDebug`, `DbgMcu_Get_LowPowerDebug` |
| Peripheral freeze | `DbgMcu_Set_PeriphFreeze`, `DbgMcu_Get_PeriphFreeze`, `DbgMcu_Set_PeriphFreezeAll` |
| Trace and state | `DbgMcu_Set_TraceEnable`, `DbgMcu_Get_DebugState` |

```c
dbgMcu_ModuleVersion_t DbgMcu_Get_ModuleVersion  ( void );

dbgMcu_RequestState_t  DbgMcu_Init               ( const dbgMcu_Config_t * const dbgMcuConfig );
dbgMcu_RequestState_t  DbgMcu_Deinit             ( void );
void                   DbgMcu_Task               ( void );
dbgMcu_RequestState_t  DbgMcu_Get_DefaultConfig  ( dbgMcu_Config_t * const dbgMcuConfig );

dbgMcu_RequestState_t  DbgMcu_Get_DeviceId       ( dbgMcu_DeviceId_t * const deviceId );
dbgMcu_RequestState_t  DbgMcu_Get_RevisionId     ( dbgMcu_RevisionId_t * const revisionId );

dbgMcu_RequestState_t  DbgMcu_Set_LowPowerDebug  ( dbgMcu_LowPower_t lowPower, dbgMcu_FunctionState_t state );
dbgMcu_RequestState_t  DbgMcu_Set_PeriphFreeze   ( dbgMcu_Periph_t periph, dbgMcu_FunctionState_t freeze );
dbgMcu_RequestState_t  DbgMcu_Set_PeriphFreezeAll( dbgMcu_FunctionState_t freeze );
dbgMcu_RequestState_t  DbgMcu_Get_DebugState     ( dbgMcu_DebugState_t * const debugState );
```

Every function returns `DBGMCU_REQUEST_OK` / `DBGMCU_REQUEST_ERROR` and checks its parameters and the state of the module
before it touches a register.

| Type | Meaning |
|---|---|
| `dbgMcu_Config_t` | Low-power debug flags, the table of the frozen peripherals, the trace enable |
| `dbgMcu_Periph_t` | `DBGMCU_PERIPH_*` - only the freezable peripherals of the device |
| `dbgMcu_LowPower_t` | `DBGMCU_LP_SLEEP`, `DBGMCU_LP_STOP`, `DBGMCU_LP_STANDBY` |
| `dbgMcu_DeviceId_t`, `dbgMcu_RevisionId_t` | The 12-bit device identifier, the 16-bit revision |
| `dbgMcu_DebugState_t` | Bit mask: debugger connected, authentication state of the family |

---

## Usage

### A development build: watchdogs and PWM frozen at the breakpoint, debug alive in Stop mode

```c
#include "DbgMcu_Port.h"

dbgMcu_Config_t dbgMcuConfig;

(void)DbgMcu_Get_DefaultConfig( &dbgMcuConfig );

dbgMcuConfig.LowPowerSleep   = DBGMCU_FUNCTION_ACTIVE;
dbgMcuConfig.LowPowerStop    = DBGMCU_FUNCTION_ACTIVE;
dbgMcuConfig.Freeze[ DBGMCU_PERIPH_IWDG ] = DBGMCU_FUNCTION_ACTIVE;
dbgMcuConfig.Freeze[ DBGMCU_PERIPH_WWDG ] = DBGMCU_FUNCTION_ACTIVE;
dbgMcuConfig.Freeze[ DBGMCU_PERIPH_TIM1 ] = DBGMCU_FUNCTION_ACTIVE;

(void)DbgMcu_Init( &dbgMcuConfig );
```

### Selecting a workaround by the silicon revision

```c
dbgMcu_RevisionId_t revisionId = 0u;

(void)DbgMcu_Get_RevisionId( &revisionId );

if( APP_REVISION_WITH_ERRATUM == revisionId )
{
    /* apply the software workaround of the erratum */
}
else
{
    /* The silicon does not have the erratum */
}
```

---

## Behaviour and limits

- **Only with a debugger.** The freeze acts while the core is halted; a free running firmware is not affected. A freeze that is
  not enough to debug the problem (a peripheral with no freeze bit) is stopped by the application at the breakpoint.
- **Consumption.** The debug in the low-power modes keeps the clocks and the regulators of the debug domain on - never measure the
  low-power current with it enabled and do not ship the firmware with it.
- **Authentication and closed devices.** On a closed product the debug port is closed; the module reports the state but does
  not open it.
- **Core debug.** The trace sources and the instrumentation (`Dwt`, `Itm`) are configured in the Core module; this module only
  enables the pins and the clock on the chip side.
- **Not part of the first version:** the detection of a debugger as a product feature (a halting-debug read of the core), the dual-core
  synchronization of the debug of the STM32H7 cores, the cross-trigger interface.

---

## Dependencies

- **Rcc** - the clock of the DBGMCU registers
- **Core** - the trace and the instrumentation sources (`Dwt`, `Itm`) that use the enabled trace pins
- **RAL (Register Abstraction Layer)** - used internally to access the registers

All mandatory RCC configurations are handled internally.

---

## Testing

- **Unit tests** (host, Unity + CMock + RegMem): the decoding of the identifier and the revision, the mapping of every peripheral to its freeze bit
  per family line, the rejection of the missing peripherals, the table application, the low-power flags, the debug state decoding.
- **Integration tests** (target, probe-rs): the identifier and the revision read on the board match the known values, a halted core stops the
  frozen timer and the watchdog (the counter does not advance while halted), the debug connection survives the Stop mode with the
  low-power debug enabled.

---

## Interface conventions

The module follows the common rules of the MCAL layer, so that it behaves like the other modules:

- **Lists instead of free members.** Everything that the hardware restricts (the pins, the kernel clock sources, the DMA
  channels, the triggers and the other selectable sources) is a `typedef enum` list with exactly the items that are valid
  for the device line; a pin is an item `DBGMCU_<SIGNAL>_PIN_<INSTANCE>_P<port><pin>` and there is an `*_UNUSED` item. A choice
  that is not in the list cannot be written.
- **Own peripheral only.** The module calls only the registers / LL functions of its own peripheral. The clocks, the
  interrupts, the pins, the DMA and the EXTI lines go through the API of the Rcc, Nvic, Gpio, Gpdma and Exti modules; a
  missing function is added to that module, it is not bypassed.
- **Init verifies itself.** `DbgMcu_Init` checks the result at the end; when it is not as required, the module is
  deinitialized and an error is returned. The waiting for the hardware is a loop on the register state with a timeout,
  never a blind delay.
- **Transfer modes.** Where the module moves data, the POLL (served by `DbgMcu_Task`), ISR and DMA modes have the same
  configuration structure, the same request and the same callbacks (error, half transfer, transfer complete).
- **Flags and states are enums of the module** (`dbgMcu_FunctionState_t`, `dbgMcu_FlagState_t`), the limits and the constants are
  named, there are no magic numbers in the interface.
- **Unsupported functions stay.** A function without a meaning in a family keeps its signature, suppresses the unused
  parameters and returns `DBGMCU_REQUEST_ERROR`.
- **Documentation.** Every function and type has a Doxygen description, the main header has the `\defgroup`.

---

## Repository layout and branches

The master holds this README and the license. The family branches (`Dev/<Family>`, `Releases/<Family>`) hold:

| Path | Content |
|---|---|
| `README.md` | README of the family: the instances of the device, specifics, errata, status |
| `DbgMcu_Port.h`, `DbgMcu_Types.h` | Public interface (identical in all families) |
| `DbgMcu*.c`, `DbgMcu.h` | Implementation of the family |
| `CMakeLists.txt` | Library `DbgMcu_Lib` of the EmBi platform |
| `Tests/UnitTests` | Unit tests (Unity + CMock + RegMem) |
| `Tests/IntegrationTests` | Integration tests on the target (probe-rs) |

A change of the public interface is made in all families.

---

## CMake integration

```cmake
target_link_libraries( User_Lib PRIVATE DbgMcu_Lib )
```

1. Link `DbgMcu_Lib` to your CMake library.
2. Include `DbgMcu_Port.h` in your project.
3. Configure the module as needed for your hardware.

---

## License

This project is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.

You are free to use, modify, and share this work for **non-commercial purposes**, provided appropriate credit is given.

See [LICENSE.md](LICENSE.md) for full terms or visit [creativecommons.org/licenses/by-nc/4.0](https://creativecommons.org/licenses/by-nc/4.0/).

---

## Authors

- **Mr.Nobody** - [embedbits.com](https://embedbits.com)

Contributions are welcome! Please open a pull request.
