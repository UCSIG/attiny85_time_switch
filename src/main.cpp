/// @file main.cpp
/// ATtiny85:
///   8kB Flash
///   512B RAM
///   512B EEPROM
/// @author JF
/// @date May 10, 2023
/// Unbrick an ATtiny85:
/// https://www.best-microcontroller-projects.com/unbrick-attiny.html
/// https://www.best-microcontroller-projects.com/attiny-ultra-low-power.html
/// Fuses:
/// Regular:
/// L: 0x62: 8MHz divided by 8 -> 1MHz main clock. No clock out.
/// H: 0xD7: EESAVE: EEPROM memory is preserved through the chip erase. SPIEN: enable serial program and data downloading.
/// E: 0xFF: Self-programming disabled.
///
/// Testing
/// L: 0x94: 128kHz. CKOUT: Clock out on PB4 feature select (no jumper!). Requires slow SCK programming clock (max 26kHz).
#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <EEPROM.h>

/// Define relevant pins.
#define VBAT_EN_PIN PB0
#define VDIV_EN_PIN PB1
#define ADC_BATTERY_PIN A1
#define SELECT_12_24V_PIN PB3
#define SELECT_FEATURE_PIN PB4

/// If defined, don't go to sleep but ensure the clock output can be measured on pin PB4 (feature select middle pin).
// #define CLOCK_CALIBRATION_MODE

/// 15min: Battery measurement period. The amount of wakeup cycles corresponding to 15min (8.192s per cycle). 15*60/8.192.
#define TIMING_CYCLES_BATTERY_MEASUREMENT 110u

/// The 10V equivalent raw adc value under which to disable the load for a 12V device (0-1023u).
/// R1: 100k, R2: 22k
/// (Vbat)*(22/122)*(1/2.56V)*1023
/// (10V)*(22/122)*(1/2.56V)*1023
#define ADC_BATTERY_THRESHOLD_12v 721u

/// The 20V equivalent raw adc value under which to disable the load for a 24V device ( 0-1023, (Vbat)*(10/110)*(1/2.56V)*1023).
/// R1: 100k, R2: 10k
/// (Vbat)*(10/110)*(1/2.56V)*1023 )
/// (20V)*(10/110)*(1/2.56V)*1023
#define ADC_BATTERY_THRESHOLD_24v 726u

/// 16h on: 12V. The amount of wakeup cycles corresponding to 8h/28800s for a 12V device (8.192s per cycle). 16*3600/8.192.
#define TIMING_CYCLES_LOAD_ON_12V 7031u
/// 8h off: 12V. The amount of wakeup cycles corresponding to 16h/57600s for a 12V device (8.192s per cycle). 8*3600/8.192.
#define TIMING_CYCLES_LOAD_OFF_12V 3516u

/// 20h on: 24V. The amount of wakeup cycles corresponding to 8h/28800s for a 24V device (8.192s per cycle). 20*3600/8.192.
#define TIMING_CYCLES_LOAD_ON_24V 8789u
/// 4h off: 24V. The amount of wakeup cycles corresponding to 16h/57600s for a 24V device (8.192s per cycle). 4*3600/8.192.
#define TIMING_CYCLES_LOAD_OFF_24V 1758u

/// Possibility to store calibration values in eeprom memory.
enum eeprom_addresses {
    EEPROM_ADDR_CLOCK_CALIB_3_MSB = 0x0u,
    EEPROM_ADDR_CLOCK_CALIB_2,
    EEPROM_ADDR_CLOCK_CALIB_1,
    EEPROM_ADDR_CLOCK_CALIB_0_LSB,
    EEPROM_ADDR_CLOCK_CALIB_MAGIC_NUMBER
};

// The magic number that must be present in the eeprom to apply the clock calibration algorithm.
#define EEPROM_CLOCK_CALIB_MAGIC_NUMBER 0xCDu

/// The value of the 128kHz clock used to clock the sleep watchdog timer.
#define SLEEP_CLOCK_VALUE_HZ 128000lu

/// The maximum deviation from @ref SLEEP_CLOCK_VALUE_HZ above or below which to discard clock calibration values.
#define SLEEP_CLOCK_DEVIATION_MAX_HZ 30000lu

/// The clock calibration gets read from eeprom at startup.
static uint32_t clock_calibration;

/// All features active or only the undervoltage protection is active. Selectable by a jumper.
static bool all_features_activated;

/// The timing for the load on/off feature with the selectable 12/24V jumper.
static uint16_t timing_cycles_load_on;
static uint16_t timing_cycles_load_off;

/// The raw adc threshold under which to disable the load.
static uint16_t undervoltage_adc_threshold;

/// Turn on the watchdog to wake the system from sleep.
/// The timeout value is 1048576 cycles @128kHz (8.192s).
static void enable_watchdog(void);

/// Enable the low power sleep mode.
static void go_to_sleep(void);

/// Initialize all used gpios with their respective driver level.
static void initialize_gpios(void);

/// Enable/disable the adc peripheral. Must be disabled before entering
/// sleep to save ~300uA. Uses the 2.56 V internal reference.
///
/// @param enable True to enable the adc.
static void enable_adc(bool enable);

/// Enable/disable the voltage divider for measuring the battery voltage.
///
/// @param enable True to enable the voltage divider, false to disable it.
static void enable_voltage_divider(bool enable);

/// Enable/disable the n-fet load switch.
///
/// @param enable True to enable the load, false to disable it.
static void enable_load(bool enable);

/// Check if the load is currently activatedd.
///
/// @return True if the load is activated.
static bool is_load_enabled(void);

/// Get the currently selected 12V/24V timing mode.
///
/// @return True: 12V timing, false: 24V timing.
static bool get_12V_24V_selection(void);

/// Get the currently selected feature mode.
///
/// @return True: all features, false: only undervoltage protection.
static bool get_feature_selection(void);

/// Check if the feature to enable/disable the load in regular intervals is currently active.
///
/// @return True if the load timing is active.
static bool load_timing_activated(void);

/// Read the current battery voltage.
///
/// @return The battery voltage as a 10 bit value.
static uint16_t read_battery_voltage(void);

/// Read the pre-programmed clock calibration value.
///
/// @return The clock calibration value.
static uint32_t read_clock_calibration(void);

/// Apply the clock calibration calculation to obtain a more precise sleep timing behaviour.
///
/// @param clock_calibration The clock calibration read from the eeprom.
/// @param uncalibrated_value The uncalibrated value.
/// @return The calibrated value.
static uint32_t apply_clock_calibration(uint32_t clock_calibration, uint32_t uncalibrated_value);

/// Checks whether the @ref EEPROM_CLOCK_CALIB_MAGIC_NUMBER is present in the eeprom.
/// If yes, the clock calibration algorithm will be run.
///
/// @return True if the clock calibration values are present.
static bool clock_calibration_present(void);

static void enable_voltage_divider(bool enable) {
    digitalWrite(VDIV_EN_PIN, enable ? LOW : HIGH);
}

static void enable_load(bool enable) {
    digitalWrite(VBAT_EN_PIN, enable ? HIGH : LOW);
}

static bool is_load_enabled(void) {
    return digitalRead(VBAT_EN_PIN) == HIGH;
}

static bool get_12V_24V_selection(void) {
    return digitalRead(SELECT_12_24V_PIN) == HIGH;
}

static bool get_feature_selection(void) {
    return digitalRead(SELECT_FEATURE_PIN) == LOW;
}

static bool load_timing_activated(void) {
    return all_features_activated == true;
}

static void enable_adc(bool enable) {
    if (enable) {
        bitSet(ADCSRA, ADEN);
        analogReference(INTERNAL2V56_NO_CAP);
    } else {
        bitClear(ADCSRA, ADEN);
    }
}

static uint16_t read_battery_voltage(void) {
    // The amount of averages to take from the adc reading. Must be a power of two.
#define ADC_AVERAGE_NUM 32u
#define ADC_DIVISION_SHIFT 5u // 2^ADC_DIVISION_SHIFT = ADC_AVERAGE_NUM
    uint16_t battery_voltage = 0u;

    // Enable the adc.
    enable_adc(true);

    // Quickly pulse the gate of the p-fet of the voltage divider to enable it.
    // Now the voltage divider is active for 230ms.
    enable_voltage_divider(true);

    // Wait 18ms to let all voltages stabilize.
    delay(18);

    // Read the ADC value (32*1023 = 32736 as a max value, fits in 16-bits).
    for (uint8_t adc_reading = 0u; adc_reading < ADC_AVERAGE_NUM; adc_reading++) {
        battery_voltage += analogRead(ADC_BATTERY_PIN);
    }

    // Divide the accumulated readings.
    battery_voltage = battery_voltage >> ADC_DIVISION_SHIFT;

    // Wait 2ms and turn off the voltage divider.
    delay(2);
    enable_voltage_divider(false);

    // Finally turn off the adc.
    enable_adc(false);

    return battery_voltage;
}

ISR(WDT_vect) {
    wdt_disable();
}

static void enable_watchdog(void) {
    //  MCU Status Register, clear reset cause (pg. 45).
    MCUSR = 0;

    // Watchdog Timer Control Register (pg. 45)
    // WDE:  Watchdog enable.
    // WDIF: Watchdog timeout interrupt flag.
    // WDCE: Watchdog change enable
    WDTCR = bit(WDCE) | bit(WDE) | bit(WDIF);

    // set WDIE ( Interrupt only, no Reset )
    // WDIE: Watchdog timeout interrupt enable.
    // WDP:  Prescaler: 1048576 cycles @32.768kHz: 32s | bit(WDP3) | bit(WDP0)
    WDTCR = bit(WDIE) | bit(WDP3) | bit(WDP0); // 8.192s
    // WDTCR = bit(WDIE) | bit(WDP2) | bit(WDP1) | bit(WDP0); // 2s.
    // WDTCR = bit(WDIE) | bit(WDP1) | bit(WDP0); // 125ms.

    // Finally reset the watchdog.
    wdt_reset();
}

static void go_to_sleep(void) {
    // Set the correct sleep mode.
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    cli();
    enable_watchdog();
    sleep_enable();
    sei();

    // Go to sleep.
    sleep_cpu();

    // Clear the SE (Sleep Enable) bit of the MCUCR ( MCU Control Register) immediately after wakeup.
    sleep_disable();
}

static void initialize_gpios(void) {
    // The fet for enabling/disabling the voltage divider.
    pinMode(VDIV_EN_PIN, OUTPUT);
    enable_voltage_divider(false);

    // The fet for enabling/disabling the load.
    pinMode(VBAT_EN_PIN, OUTPUT);
    enable_load(false);

    // The feature selection jumpers.
    pinMode(SELECT_12_24V_PIN, INPUT);
    pinMode(SELECT_FEATURE_PIN, INPUT);
}

static uint32_t read_clock_calibration(void) {
    uint32_t clock_calib;
    clock_calib = ((uint32_t)EEPROM.read(EEPROM_ADDR_CLOCK_CALIB_3_MSB) << 24u) |
                  ((uint32_t)EEPROM.read(EEPROM_ADDR_CLOCK_CALIB_2) << 16u) |
                  ((uint32_t)EEPROM.read(EEPROM_ADDR_CLOCK_CALIB_1) << 8u) |
                  (uint32_t)EEPROM.read(EEPROM_ADDR_CLOCK_CALIB_0_LSB);
    return (clock_calib == 0xFFFFFFFFlu) ? 0u : clock_calib;
}

static uint32_t apply_clock_calibration(uint32_t clock_calibration, uint32_t uncalibrated_value) {
    // Discard too high or too low calibration values.
    if ((clock_calibration < (SLEEP_CLOCK_VALUE_HZ - SLEEP_CLOCK_DEVIATION_MAX_HZ)) ||
        clock_calibration > (SLEEP_CLOCK_VALUE_HZ + SLEEP_CLOCK_DEVIATION_MAX_HZ)) {
        return uncalibrated_value;
    }

    return (clock_calibration * uncalibrated_value) / SLEEP_CLOCK_VALUE_HZ;
}

static bool clock_calibration_present(void) {
    return (EEPROM.read(EEPROM_ADDR_CLOCK_CALIB_MAGIC_NUMBER) == EEPROM_CLOCK_CALIB_MAGIC_NUMBER);
}

void setup() {
    // Set all gpios to their default level.
    initialize_gpios();

    // Only enable the adc if a battery voltage measurement is ongoing.
    enable_adc(false);

    // Read in the pre-programmed clock calibration value.
    clock_calibration = read_clock_calibration();

    // Read the feature selection jumper.
    all_features_activated = get_feature_selection();

    // Read the 12/24V selection jumper.
    bool _12_24V_selection = get_12V_24V_selection();

    // Apply the correct timing.
    timing_cycles_load_on = (_12_24V_selection == true) ? TIMING_CYCLES_LOAD_ON_12V : TIMING_CYCLES_LOAD_ON_24V;
    timing_cycles_load_off = (_12_24V_selection == true) ? TIMING_CYCLES_LOAD_OFF_12V : TIMING_CYCLES_LOAD_OFF_24V;
    undervoltage_adc_threshold = (_12_24V_selection == true) ? ADC_BATTERY_THRESHOLD_12v : ADC_BATTERY_THRESHOLD_24v;

    // Check whether to apply a clock calibration, if yes adjust the timing values accordingly.
    if (clock_calibration_present()) {
        timing_cycles_load_on = apply_clock_calibration(clock_calibration, timing_cycles_load_on);
        timing_cycles_load_off = apply_clock_calibration(clock_calibration, timing_cycles_load_off);
    }

    // Start with the load on.
    enable_load(true);
}

void loop() {
    static uint16_t wakeup_count_load_feature = 0u;
    static uint16_t wakeup_count_undervoltage_protection = 0u;
    static bool undervoltage_protection_triggered = false;
    typedef enum wake_states {
        STATE_LOAD_ON,
        STATE_LOAD_OFF,
    } wake_states_t;
    static wake_states_t wake_state = STATE_LOAD_ON;

    // Loop forever if clock calibration mode is selected.
#ifdef CLOCK_CALIBRATION_MODE
    while (true) {
    }
#endif

    go_to_sleep();
    wakeup_count_load_feature++;
    wakeup_count_undervoltage_protection++;

    // Periodically measure the battery voltage if the load is active.
    if (is_load_enabled() && (wakeup_count_undervoltage_protection >= TIMING_CYCLES_BATTERY_MEASUREMENT)) {
        wakeup_count_undervoltage_protection = 0u;

        // Disable the load if the battery voltage falls under the predefined threshold.
        if (read_battery_voltage() < undervoltage_adc_threshold) {
            enable_load(false);
            undervoltage_protection_triggered = true;
        }
    }

    // If the feature selection jumper is set to all features, enable/disable the load regularly.
    // If the battery voltage has once fallen under the threshold, never activate the load again.
    if (load_timing_activated() && !undervoltage_protection_triggered) {
        switch (wake_state) {
        case STATE_LOAD_ON: {
            if (wakeup_count_load_feature >= timing_cycles_load_on) {
                wakeup_count_load_feature = 0u;
                enable_load(false);
                wake_state = STATE_LOAD_OFF;
            }
            break;
        }

        case STATE_LOAD_OFF: {
            if (wakeup_count_load_feature >= timing_cycles_load_off) {
                wakeup_count_load_feature = 0u;
                enable_load(true);
                wake_state = STATE_LOAD_ON;
            }
            break;
        }

        default: {
            wake_state = STATE_LOAD_OFF;
            break;
        }
        }
    }
}
