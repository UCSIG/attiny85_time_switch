# ATtiny85 Time Switch

## Low-power 12/24V time switch with undervoltage protection using the internal oscillator of the ATtiny85

The aim of this project is to switch a load on and off in regular time intervals. The device itself consumes only very little sleep current (~19.6uA on average).  
The device also monitors the supply voltage and turns off the load if the supply drops lower than the defined threshold.

![PCB 3D View](pcb_project/attiny85_time_switch/attiny85_time_switch.png)

## Features

- Clock calibration algorithm with values stored in the internal eeprom
- Supply voltage monitoring
- Jumper configuration for feature selection (supply voltage monitoring + time switch/only supply voltage monitoring)
- Jumper configuration for undervoltage threshold selection (12/24V)
- Low current consumption (~19.6uA)

## Specifications

| Type                        | Value        |
| --------------------------- | ------------ |
| Average Current Consumption | 19.6uA       |
| Maximum Load Current        | 5.7A (cont.) |
| Supply Voltages             | 12/24V       |
| Switch Type                 | Low-Side     |

## Detailed Description

There are three jumpers which must be set to either select the 12V or the 24V configuration. This changes the on/off times as well as the undervoltage thresholds.  
Every time the jumper configuration is changed, a reset must be performed for the changes to take effect.  

Upon coming out of reset, the device reads its clock calibration values stored in the internal eeprom to provide accurate timings for the time switch.

The device wakes up periodically and immediately goes back to sleep. That way, very long deep sleep periods can be reached.  
The ATtiny85 runs off of the internal 128kHz low speed oscillator during deep sleep. The watchdog timer wakes up the device every 8.192s (the maximum watchdog timeout) and the ATtiny increments a counter variable.
If it surpasses a pre-defined threshold, the load switch changes state from on->off or the other way round.  

Every 15 minutes it also measures the supply voltage and compares it to a pre-defined threshold. If the supply is lower than that threshold, the load is switched off until the device is reset.  
The maximum load current is determined by the sizing of the n-fet Q1. With the n-fet PMV20XNER one can switch up to 5/7A. The load is switched with a low side switch.  

The internal 8MHz oscillator of the ATtiny85 is used as the primary clock source. However, the clock frequency can vary by up to ±15%. In order to get accurate on/off times from the time switch,
this internal oscillator must be calibrated. A detailed description of how to do that can be found [here](clock_calibrations/clock_calibrations.md).

## Mechanical

The pcb is designed to fit in the [EL111](https://ide.es/eng/products/junction-boxes-and-mechanisms/ip65-ip67-junction-boxes/ref_EL111) enclosure by IDE electric.
It is screwed into the case using four M4 screws. The case itself is rated IP65-67. Its dimensions are 113x113x68 mm (Height x Width x Depth).

## Project structure

The code resides in the folder `src` and `include`. PlatformIO is chosen as the build environment. The best way to program the ATtiny85 is to use PlatformIO and VS Code as the IDE.  
The pcb project can be found under `pcb_project/attiny85_time_switch`. The relevant code snippets for changing the timing values and the undervoltage thresholds are at the top of `main.cpp`.
The gerber files are in `pcb_project/attiny85_time_switch/fabrication_outputs/`.
