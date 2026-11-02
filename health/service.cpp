/*
 * Health HAL 2.0 service for the MT6592 hulkbuster.
 *
 * Maps the MTK battery driver's non-standard attributes onto what
 * BatteryMonitor expects:
 *   batt_vol  - millivolts            -> batteryVoltage (mV)
 *   batt_temp - tenths of degree C    -> batteryTemperature (tenths of C)
 * status/health/present/capacity/technology and the ac/usb/wireless "online"
 * nodes already use the standard names.
 */
#define LOG_TAG "health@2.0-hulkbuster"

#include <health2/service.h>
#include <healthd/healthd.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <batteryservice/BatteryService.h>

#include <string>

#define BATTERY_DIR "/sys/class/power_supply/battery/"
#define BATT_VOL_PATH BATTERY_DIR "batt_vol"
#define BATT_TEMP_PATH BATTERY_DIR "batt_temp"

static bool readInt(const char* path, int* out) {
    std::string buf;
    if (!android::base::ReadFileToString(path, &buf)) return false;
    return android::base::ParseInt(android::base::Trim(buf), out);
}

void healthd_board_init(struct healthd_config* config) {
    // batt_temp is already in tenths of a degree C, like the standard "temp".
    if (access(BATT_TEMP_PATH, R_OK) == 0) {
        config->batteryTemperaturePath = BATT_TEMP_PATH;
    }
    // batt_vol is mV, the standard voltage_now is uV; BatteryMonitor divides
    // by 1000 and would give volts, so it is re-read in the update hook. The
    // path is still set so BatteryMonitor does not warn about a missing one.
    if (access(BATT_VOL_PATH, R_OK) == 0) {
        config->batteryVoltagePath = BATT_VOL_PATH;
    }
}

int healthd_board_battery_update(struct android::BatteryProperties* props) {
    int mv;
    if (readInt(BATT_VOL_PATH, &mv)) {
        props->batteryVoltage = mv;  // BatteryProperties wants mV
    }

    // The driver reports "Charging" even once the battery is full; tell the
    // framework so the UI/charging-complete logic behaves.
    if (props->batteryPresent && props->batteryLevel >= 100 &&
        props->batteryStatus == android::BATTERY_STATUS_CHARGING) {
        props->batteryStatus = android::BATTERY_STATUS_FULL;
    }
    return 0;
}

int main() {
    return health_service_main();
}
