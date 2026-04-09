# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this project.

## Project Overview

Roz embedded controller firmware (`roz_firmware`) for the **NUCLEO-L031K6** board (STM32L031K6Tx, Cortex-M0+, 32KB Flash, 8KB RAM). Generated with STM32CubeMX 6.14.1 and built with STM32CubeIDE.

**Note:** CubeMX project files and build outputs still use the legacy name `servo_controller_1` (e.g., `.ioc`, `.elf`, `.launch`). The project has been renamed to `roz_firmware`.

## Build

```bash
make -C Debug        # build (output: Debug/servo_controller_1.elf)
make -C Debug clean  # clean build artifacts
```

Toolchain: `arm-none-eabi-gcc` (GNU Tools for STM32 13.3.rel1). Compiler flags include `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft`. Linker script: `STM32L031K6TX_FLASH.ld`.

## Architecture

This is a CubeMX-generated HAL project. Peripheral configuration lives in `servo_controller_1.ioc` and should be modified through STM32CubeMX to regenerate boilerplate code.

**Application code goes in `/* USER CODE BEGIN/END */` blocks only.** Code outside these blocks will be overwritten by CubeMX code generation.

### Peripheral Map

| Peripheral | Purpose | Pins |
|---|---|---|
| TIM2 CH1-3 | Servo PWM (3 channels) | PA0, PA1, PB0 |
| TIM22 CH2 | Servo PWM (4th channel) | PA7 |
| USART2 | Serial (VCP), 115200 8N1 | PA2 (TX), PA15 (RX) |
| GPIO | Green LED (LD3) | PB3 |
| SWD | Debug | PA13, PA14 |

### Clock Configuration

System clock: 32 MHz (HSI 16 MHz, PLL x4 /2). All bus clocks (AHB, APB1, APB2) run at 32 MHz.

### PWM Timing

- TIM2: prescaler=32, period=20000-1 in code (gives ~50 Hz servo signal at 32 MHz)
- TIM22: prescaler=32, period=49999 (~20 Hz)
- Servo position set via `TIMx->CCRy` register (1000-2000 range for typical servos)

**Note:** The `.ioc` file has `TIM2.Period=50000` but `Src/main.c` has `Period = 20000-1`. These are out of sync -- regenerating from CubeMX would overwrite the code change.

### Key Source Files

- `Src/main.c` -- main loop, peripheral init, application logic
- `Src/stm32l0xx_hal_msp.c` -- clock enable and GPIO pin mapping for peripherals
- `Inc/main.h` -- pin label defines (LD3, VCP_TX/RX, TMS, TCK)
- `Src/stm32l0xx_it.c` -- interrupt handlers

### Current Main Loop Behavior

The main loop polls USART2 for a single byte with 10ms timeout and echoes it back. Servo PWM start/control code is present but commented out.
