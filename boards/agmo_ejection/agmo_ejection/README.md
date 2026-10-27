# AGMO 12-channel ejection controller

Custom Zephyr board for the STM32G473RCT6-based 12-channel ejection controller.

The board definition is derived from the KiCad hierarchy under
`hardware/kicad/agmo/12-Channel_Ejection_Controller`.

## Fixed interfaces

- HSE: 8 MHz; system clock: 170 MHz
- USB FS: PA11/PA12, clocked from HSI48
- FDCAN1: PB8/PB9
- I2C3: PC8/PC9
- AT24C256C EEPROM: address 0x50
- Ejection outputs: PC0-PC3, PA4-PA7, PC4-PC5, PB0-PB1
- Isolated inputs: PA0-PA3, PB7-PB4, PA15, PC12-PC10, PC13-PC14
- Encoder connector: PC6/PC7/PA8

## Deliberately deferred

- OLED is an external module; its address and panel configuration are not yet fixed.
- No dedicated magnetic-impact sensor net is identifiable in the schematic.
- PC6/PC7 are exposed as GPIO during bring-up. TIM8 encoder mode is phase 4 work.
- Input and output polarity must be confirmed on the physical board.
- USB VID 0x2FE3 is Zephyr's development VID and must be replaced before a
  production release.

## Build

```sh
west build -p always -b agmo_ejection app/ejection_controller
```
