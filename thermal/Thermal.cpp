#define LOG_TAG "thermal.hulkbuster"

#include "Thermal.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <log/log.h>

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace thermal {
namespace V1_0 {
namespace hulkbuster {

namespace {

const char kThermalDir[] = "/sys/class/thermal";
const int kNumCpus = 8;  // MT6592 octa Cortex-A7; MTK hotplug may take cores offline

// The MTK thermal driver (drivers/misc/mediatek/thermal/mt6592) exposes
// zones whose `type` is the name given to mtk_thermal_zone_device_register(),
// and whose `temp` and `trip_point_N_temp` are in millidegree Celsius
// (mtktscpu_get_temp() returns curr_temp in mC, battery is tbat*1000, ...).
// The zone numbers depend on module init order, so zones are found by type.
struct ZoneSpec {
    TemperatureType type;
    const char* name;        // name reported to the framework
    const char* zoneType;    // /sys/class/thermal/thermal_zoneN/type
    float defaultShutdown;   // kernel *_TEMP_CRIT, used when the zone has no trip points
};

// SKIN has no dedicated sensor on this tablet. It is approximated by the PMIC
// die sensor (board-level, tracks the back cover better than the CPU diode),
// falling back to the AP and ABB sensors if that zone is missing.
const ZoneSpec kZones[] = {
    {TemperatureType::CPU, "cpu", "mtktscpu", 120.0f},        // MTKTSCPU_TEMP_CRIT
    // The Mali-450 is on the same die and MTK throttles it from the same
    // sensor (mtktscpu reads the die thermal controller), so report it as GPU.
    {TemperatureType::GPU, "gpu", "mtktscpu", 120.0f},
    {TemperatureType::BATTERY, "battery", "mtktsbattery", NAN},
    {TemperatureType::SKIN, "skin", "mtktspmic", 150.0f},     // mtktspmic_TEMP_CRIT
    {TemperatureType::SKIN, "skin", "mtktsAP", NAN},
    {TemperatureType::SKIN, "skin", "mtktsabb", 120.0f},      // MTKTSabb_TEMP_CRIT
};

bool readFile(const std::string& path, std::string* out) {
    std::string s;
    if (!android::base::ReadFileToString(path, &s)) return false;
    *out = android::base::Trim(s);
    return true;
}

bool readLong(const std::string& path, long* out) {
    std::string s;
    if (!readFile(path, &s) || s.empty()) return false;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    *out = v;
    return true;
}

// Returns the thermal_zone directory whose type matches, or "" if absent.
std::string findZone(const std::string& wantedType) {
    for (int i = 0; i < 32; i++) {
        std::string dir = std::string(kThermalDir) + "/thermal_zone" + std::to_string(i);
        std::string type;
        if (!readFile(dir + "/type", &type)) continue;
        if (type == wantedType) return dir;
    }
    return "";
}

// Fills the thresholds from the kernel trip points. The MTK convention is that
// trip 0 is the HW protection point (bound to "<zone>-sysrst", 117 C on the
// CPU) and the following, lower trips are throttle steps, so shutdown is the
// highest trip and throttling the next distinct lower one.
void readThresholds(const std::string& dir, float defaultShutdown, float* throttling,
                    float* shutdown) {
    std::vector<long> trips;
    for (int i = 0; i < 10; i++) {
        long t;
        if (readLong(dir + "/trip_point_" + std::to_string(i) + "_temp", &t) && t > 0) {
            trips.push_back(t);
        }
    }
    std::sort(trips.begin(), trips.end(), [](long a, long b) { return a > b; });
    trips.erase(std::unique(trips.begin(), trips.end()), trips.end());

    *shutdown = defaultShutdown;
    *throttling = NAN;
    if (!trips.empty()) *shutdown = trips[0] / 1000.0f;
    if (trips.size() > 1) *throttling = trips[1] / 1000.0f;
}

}  // namespace

Return<void> Thermal::getTemperatures(getTemperatures_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    std::vector<Temperature> list;
    bool haveSkin = false;

    for (const ZoneSpec& z : kZones) {
        if (z.type == TemperatureType::SKIN && haveSkin) continue;
        std::string dir = findZone(z.zoneType);
        if (dir.empty()) continue;

        long mc;
        if (!readLong(dir + "/temp", &mc)) {
            ALOGW("cannot read %s/temp", dir.c_str());
            continue;
        }
        Temperature t;
        t.type = z.type;
        t.name = z.name;
        t.currentValue = mc / 1000.0f;
        readThresholds(dir, z.defaultShutdown, &t.throttlingThreshold, &t.shutdownThreshold);
        t.vrThrottlingThreshold = NAN;  // no VR mode on this device
        list.push_back(t);
        if (z.type == TemperatureType::SKIN) haveSkin = true;
    }

    hidl_vec<Temperature> temps;
    temps.resize(list.size());
    for (size_t i = 0; i < list.size(); i++) temps[i] = list[i];
    _hidl_cb(status, temps);
    return Void();
}

Return<void> Thermal::getCpuUsages(getCpuUsages_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;

    std::vector<CpuUsage> usages(kNumCpus);
    for (int i = 0; i < kNumCpus; i++) {
        usages[i].name = "CPU" + std::to_string(i);
        usages[i].active = 0;
        usages[i].total = 0;
        usages[i].isOnline = false;
    }

    // The kernel only prints online CPUs in /proc/stat, so cores taken down by
    // the MTK hotplug governor simply have no line and stay isOnline=false.
    std::string stat;
    if (!android::base::ReadFileToString("/proc/stat", &stat)) {
        status.code = ThermalStatusCode::FAILURE;
        status.debugMessage = "cannot read /proc/stat";
        _hidl_cb(status, hidl_vec<CpuUsage>());
        return Void();
    }
    for (const std::string& line : android::base::Split(stat, "\n")) {
        int cpu;
        unsigned long long user, nice, sys, idle, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (line.compare(0, 3, "cpu") != 0 || line.size() < 4 || line[3] < '0' || line[3] > '9') {
            continue;
        }
        int n = sscanf(line.c_str(), "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu", &cpu, &user,
                       &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
        if (n < 5 || cpu < 0 || cpu >= kNumCpus) continue;
        uint64_t active = user + nice + sys + irq + softirq + steal;
        usages[cpu].active = active;
        usages[cpu].total = active + idle + iowait;
        usages[cpu].isOnline = true;
    }

    hidl_vec<CpuUsage> out;
    out.resize(usages.size());
    for (size_t i = 0; i < usages.size(); i++) out[i] = usages[i];
    _hidl_cb(status, out);
    return Void();
}

// IThermal 1.0 can only describe fans (CoolingType::FAN_RPM). The MTK coolers
// in /sys/class/thermal/cooling_deviceN (cpufreq/hotplug steps, backlight,
// bcct, sysrst) are not fans, so only devices whose type mentions "fan" are
// reported; on this device the list is normally empty.
Return<void> Thermal::getCoolingDevices(getCoolingDevices_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    std::vector<CoolingDevice> list;

    for (int i = 0; i < 64; i++) {
        std::string dir = std::string(kThermalDir) + "/cooling_device" + std::to_string(i);
        std::string type;
        if (!readFile(dir + "/type", &type)) continue;
        if (type.find("fan") == std::string::npos) continue;
        long cur;
        if (!readLong(dir + "/cur_state", &cur)) continue;
        CoolingDevice d;
        d.type = CoolingType::FAN_RPM;
        d.name = type;
        d.currentValue = static_cast<float>(cur);
        list.push_back(d);
    }

    hidl_vec<CoolingDevice> out;
    out.resize(list.size());
    for (size_t i = 0; i < list.size(); i++) out[i] = list[i];
    _hidl_cb(status, out);
    return Void();
}

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
