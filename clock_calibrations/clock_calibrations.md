# Clock Calibration Values

The internal 128kHz rc oscillator is very unprecise (up to +-15%) and therefore requires some calibration.
By calculating the frequency compensation values and writing them to the internal eeprom, the error can be reduced.  
This document describes how this is done.

## Calibration procedure

- Remove feature select jumper JMP1
- Hook up an oscilloscope probe on pin 2 (middle pin) of feature select jumper JMP1 (PB4 of attiny), ground is on pin 3, right of middle pin
- Program the attiny with the `CLOCK_CALIBRATION_MODE` defined
- Program the fuse bits `L: 0x94`, `H: 0xD7`, `E: 0xFF`
- Measure the frequency on pin 2 of JMP1
- Write the chip id (just increasing numbers) on a sticky note and stick it to the attiny
- Write the chip id in the table [below](#actual-calibration-values)
- Write the frequency in the table [below](#actual-calibration-values)
- Run the script [generate_bin_data.py](generate_bin_data.py) in this folder
- Write the produced [clock_calibration_xxx.bin](clock_calibration_001.bin) file with avrdudess to the eeprom (settings in [avr_dudess_eeprom_settings.png](../docu/avr_dudess_eeprom_settings.png)) (check for output of avr dudess!)

## Actual calibration Values

The files to be programmed with avrdudess into the eeprom can be generated with the script `generate_bin_data.py`.

| Chip ID | Frequency (Hz) | Clock Calibration Value in eeprom (hex value of frequency column) |
| ------- | -------------- | ----------------------------------------------------------------- |
| 1       | 110481         | 0x0001af91                                                        |
| 2       | 111976         | 0x0001b568                                                        |
| 3       | 114584         | 0x0001bf98                                                        |
| 4       | 108488         | 0x0001a7c8                                                        |
| 5       | 111978         | 0x0001b56a                                                        |
| 6       | 113371         | 0x0001badb                                                        |
| 7       | 113827         | 0x0001bca3                                                        |
| 8       | 109239         | 0x0001aab7                                                        |
| 9       | 112813         | 0x0001b8ad                                                        |
| 10      | 109103         | 0x0001aa2f                                                        |
| 11      | 110534         | 0x0001afc6                                                        |
| 12      | 116008         | 0x0001c528                                                        |
| 13      | 111055         | 0x0001b1cf                                                        |
| 14      | 115564         | 0x0001c36c                                                        |
| 16      | 107913         | 0x0001a589                                                        |

## Fuse settings

- L: 94
  - 128kHz. CKOUT: Clock out on PB4 feature select (no jumper!). Requires slow SCK programming clock.
- H: D7
  - EESAVE: EEPROM memory is preserved through the chip erase. SPIEN: enable serial program and data downloading.
- E: FF
  - Self-programming disabled.

128kHz main clock requires slow SCK programming clock (maximum 26kHz).
