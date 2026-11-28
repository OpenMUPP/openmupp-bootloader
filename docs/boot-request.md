# Start the bootloader from the application

An application starts the bootloader with a boot request in a backup
register, then a system reset. Applications depend on this contract:

| Item | STM32F1 | CH32V307 (OpenMUPP v1) |
|---|---|---|
| Register | `BKP->DR1` | `BKP_DATAR1` |
| Boot request value | `0xB007` | `0xB007` |

On the STM32F1, the application does these steps:

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

After the reset, the bootloader reads the register. If the value is
`0xB007`, the bootloader clears the register and stays in bootloader
mode. On the STM32F1 boards, this is the same behavior as with the key
down (see [STM32F1 boards](stm32f1.md)). The next reset without a request starts the application again.

The bootloader checks the request immediately after the reset, before it
reads the key. The start-up order of the STM32F1 (see
[Start-up](stm32f1.md#start-up)) is also the order of the CH32V307,
without the key step. A system reset does not clear
the register. A power-up clears it only when the backup domain (VBAT)
had no power.

The bootloader also uses these backup registers. An application must not
use them:

| STM32F1 | CH32V307 (OpenMUPP v1) | Use |
|---|---|---|
| `BKP->DR2` | `BKP_DATAR2` | GO token (`0x60A7`): start the application after the reset, without the check of the application |
| `BKP->DR3` | - | The bootloader tried to protect its area before the last reset |

On the CH32V307, the bootloader writes the GO token after each DFU
download, before the reset.
