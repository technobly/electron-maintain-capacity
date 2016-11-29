/*
 ******************************************************************************
 *  Copyright (c) 2016 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************
 */

/*
 * Particle Electron - Maintain a Minimum Battery Capacity App
 *
 * - Designed for use with the 2000mAh LiPo battery that ships with the Electron.
 * - Electron will deep sleep if battery capacity falls below LOW_BATT_CAPACITY (default 20.0%)
 * - Electron sleeps long enough to charge up well past the LOW_BATT_CAPACITY but will
 *   power back on if above LOW_BATT_CAPACITY.
 * - Sleep duration will increase exponentially starting at 24 minutes, increasing to 51.2 hours max.
 * - Please read through the comments to understand logic.
 */

#include "Particle.h"
#include <algorithm> // std::min

SYSTEM_THREAD(ENABLED);
// SYSTEM_MODE(SEMI_AUTOMATIC);	// prevent load of modem occurring automatically
SYSTEM_MODE(MANUAL);    // prevent load of modem occurring automatically

// If we lose power completely, expect these to reinitialize.
// Hey User! Try to characterize the worst case scenario to prevent complete power loss.
retained float last_battery_capacity = 0;
retained uint32_t low_batt_sleep_attempts = 0;

const float LOW_BATT_CAPACITY = 20.0; // 20.0 is lowest it should be set at
#define MY_SERIAL Serial1
#define SERIAL_DEBUGGING

uint32_t lastBlink = 0;

using std::min;

STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));

/**
 * Series In: 1, 2, 3, 4, 5...n
 * Series Out: 1 (2 times), 2, 4, 8, 16, 32, 64, 128 seconds (3 times each) thereafter
 * @param attempt_num The current attempt number.
 * @return The number of milliseconds to backoff.
 */
uint32_t sleep_backoff(uint32_t attempt_num)
{
    if (attempt_num == 0)
        return 0;
    uint32_t exponent = min(7u, attempt_num/3);
    return 1000*(1<<exponent);
}

/*
 * @param capacity The value to compare current battery to.
 * @return If battery is lower than `capacity`, return `true`.
 */
bool battery_lower_than(float capacity)
{
    return (FuelGauge().getSoC() < capacity) ? 1 : 0;
}

void reset_battery_capacity() {
    FuelGauge().quickStart();
    // must delay at least 175ms after quickstart, before calling
    // getSoC(), or reading will not have updated yet.
    delay(200);
}

void publish_pmic_stats_event(String eventname) {
    String stats = String(FuelGauge().getSoC()) + "(\%)," + String(FuelGauge().getVCell()) + "(V)";
    Particle.publish(eventname, stats);
    #ifdef SERIAL_DEBUGGING
        stats = eventname + " " + stats;
        MY_SERIAL.println(stats.c_str());
        delay(100);
    #endif
}

void publish_pmic_stats(void) {
    publish_pmic_stats_event(String("UPDATE"));
}

int get_soc(String c) {
    return (int)(FuelGauge().getSoC());
}

int get_battv(String c) {
    return (int)(100 * FuelGauge().getVCell());
}

/*
 * Make sure we are at minimum hibernating the system for long enough to charge up past 30%
 * battery capacity.  If we normally charge at a 512mA average with the supplied 2000mAh battery,
 * a 512mA charging rate would ideally charge the battery within 2*60 minutes for 100% of the
 * battery(it's more like 2.5 hours due to the top off phase, but we can just consider the bulk
 * constant current charge rate), or 120/10 for 10% of the battery.  Be safe and go with double
 * that, or 24 minutes.
 */
void qualify_battery_and_hibernate() {
    if (battery_lower_than(LOW_BATT_CAPACITY)) {
        uint32_t sleep_time = 144 * sleep_backoff(++low_batt_sleep_attempts) / 100;
        if (Particle.connected()) {
            publish_pmic_stats_event("SLEEP " + String(sleep_time));
            delay(5000); // should not need this after 0.6.1 is released
        }
        #ifdef SERIAL_DEBUGGING
            String stats = "SLEEP " + String(sleep_time) + " " + String(FuelGauge().getSoC()) + "(\%)," + String(FuelGauge().getVCell()) + "(V)";
            MY_SERIAL.println(stats.c_str());
            delay(100);
        #endif
        System.sleep(SLEEP_MODE_SOFTPOWEROFF, sleep_time);
    }
    low_batt_sleep_attempts = 0; // reset if we don't hibernate
}

/*
 * Make sure we are polling the battery capacity with enough frequency to ensure it doesn't get to 10%.
 * If our threshold is 20%, then we need to be polling at least as frequently as it takes to go from
 * 20% to 10% during the worst case load.  Let's assume 250mA average with the supplied 2000mAh battery.
 * A 0.2C discharge rate (400mA) should last 5*60 minutes per battery spec, so 250mA should last 8*60 minutes
 * for 100% of the battery, or 480/10 for 10% of the battery.  Be safe and go with half, or 24 minutes.
 */
Timer batt_monitor(24*60*1000, qualify_battery_and_hibernate);

/*
 * Publish data every minute to give the Electron a test workout
 */
Timer publish_data(1*60*1000, publish_pmic_stats);

void showHelp() {
    Serial1.println("\r\nPress a key to run a command:"
                   "\r\n[q] run Fuel Gauge [q]uickStart and read SoC and BattV"
                   "\r\n[b] run qualify_[b]attery_and_hibernate"
                   "\r\n[v] get Fuel Gauge hardware [v]ersion"
                   "\r\n[h] show this [h]elp menu\r\n");
}

void toggleD7() {
    if (millis()-lastBlink > 100) {
        lastBlink = millis();
        digitalWrite(D7, !digitalRead(D7));
    }
}

void processSerial() {
    if (MY_SERIAL.available() > 0)
    {
        char c = MY_SERIAL.read();
        if (c == 'q') {
            reset_battery_capacity();
            String stats = String(FuelGauge().getSoC()) + "(\%)," + String(FuelGauge().getVCell()) + "(V)";
            MY_SERIAL.printlnf("Quickstart and Battery stats: %s", stats.c_str());
        }
        else if (c == 'Q') {
            String stats = String(FuelGauge().getSoC()) + "(\%)," + String(FuelGauge().getVCell()) + "(V)";
            MY_SERIAL.printlnf("Battery stats: %s", stats.c_str());
        }
        else if (c == 'b') {
            MY_SERIAL.println("Running qualify_battery_and_hibernate()");
            qualify_battery_and_hibernate();
        }
        else if (c == 'v') {
            MY_SERIAL.printlnf("Fuel Gauge hardware version: %d", FuelGauge().getVersion());
        }
        else if (c == 'h') {
            showHelp();
        }
        else {
            MY_SERIAL.println("Bad command! Press [h] for help menu.");
        }
        while (MY_SERIAL.available()) MY_SERIAL.read(); // Flush the input buffer
    }
    //if (Particle.connected()) Particle.process(); // Required for MANUAL mode
}

void setup()
{
    pinMode(D7, OUTPUT);
    MY_SERIAL.begin(9600);
    Particle.function("soc", get_soc);
    /* Currently FuelGauge().getVCell() will report about 0.1V lower than actual
     * due to software bug that will be fixed in 0.6.1.  This does not affect getSoC().
     * See https://github.com/spark/firmware/pull/1147 */
    Particle.function("battv", get_battv);

    /* reset SoC with battery in a resting state,
     * before cellular is enabled which loads the battery down */
    reset_battery_capacity();
    qualify_battery_and_hibernate();

    Particle.connect();
    waitFor(Particle.connected, 120000); // this won't be necessary when 0.6.1 is released
    publish_pmic_stats_event("WAKE");

    batt_monitor.start();
    publish_data.start(); // Optional, this drains the battery for testing and also uses data

#ifdef SERIAL_DEBUGGING
    showHelp();
#endif
}

void loop()
{
    /* Optional, this helps us visually see that the loop is running. */
    toggleD7();

    /* Optional, this just help us poke at the battery readings and display them on Serial1 (TX) */
    processSerial();
}

