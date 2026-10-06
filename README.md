# Versaloon bootloader

USB bootloader for Versaloon boards with an STM32F103 microcontroller.
The bootloader shows a USB CDC (Communication Device Class) serial port
with the ID `0483:5740`. It uses the STM32 serial bootloader protocol
(ST AN3155), so `stm32flash` can program the application.

## Boards

| Board | `BOARD` | USB D+ pull-up | LEDs | Key |
|---|---|---|---|---|
| Versaloon Mini (MiniRelease1) | `mini` | PC13 | PA15, PB5 | PB9 |
| OtherUse | `otheruse` | PA2 | PA15, PB5 | PB9 |

Both boards have a 12 MHz crystal.
The pull-up pin is high when the USB device is connected.

## Memory layout

| Area | Start | Size |
|---|---|---|
| Bootloader | `0x08000000` | 10 KB |
| Application | `0x08002800` | rest of the flash |

The layout is the same for all boards.

The flash size is different on each part. For example, the STM32F103C8
has 64 KB and the STM32F103CB has 128 KB. The bootloader reads the flash
size from the flash size register (`0x1FFFF7E0`) at run time. It sends a
NACK (negative acknowledge) for a write or an erase outside the flash.

## Build

You need the Arm GNU toolchain (`arm-none-eabi-gcc`) and GNU make.

To build the bootloader for all boards:

```sh
make
```

To build the bootloader for one board:

```sh
make BOARD=mini
```

The build writes `build/<board>/versaloon-bootloader-<board>.{elf,hex,bin}`.
To use a different output directory, set `BUILD_ROOT`.
To use a different toolchain prefix, set `CROSS_COMPILE`.

The Eclipse project (`.cproject`) builds the OtherUse board only.

## Use

1. Hold the key (PB9) down and reset the board.
2. The bootloader starts and the red LED (PA15) comes on.
3. Write the application with `stm32flash`:

   ```sh
   stm32flash -w app.bin -v -S 0x08002800 /dev/ttyACM0
   ```

When the key is not down at reset and no boot request is set, the
bootloader starts the application.

## Start the bootloader from the application

An application starts the bootloader with a boot request in the backup
register `BKP->DR1`. Applications depend on this contract:

| Item | Value |
|---|---|
| Register | `BKP->DR1` |
| Boot request value | `0xB007` |

The application does these steps:

1. Enable the `PWR` and `BKP` clocks.
2. Set `PWR_CR.DBP` to enable write access to the backup domain.
3. Write `0xB007` to `BKP->DR1`.
4. Call `NVIC_SystemReset()`.

```c
RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
PWR->CR |= PWR_CR_DBP;
BKP->DR1 = 0xB007;
NVIC_SystemReset();
```

After the reset, the bootloader reads `BKP->DR1`. If the value is
`0xB007`, the bootloader clears the register and stays in bootloader mode.
This is the same behavior as with the key down. The next reset without a
request starts the application again.

The bootloader checks the request after the clock and USB setup, before
it reads the key. A system reset does not clear `BKP->DR1`. A power-up
clears `BKP->DR1` only when the backup domain (VBAT) had no power.

### Deprecated: `entryFromProgram()`

An application can also call `entryFromProgram()` in the bootloader. This
function is deprecated. Its address changes with the linker script and the
device density. It also starts with the hardware state of the application,
so the host does not always see a USB disconnect. Use the boot request in
`BKP->DR1` instead.
