#define LOG_TAG "power.hulkbuster"

#include "Power.h"

#include <log/log.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace hulkbuster {

namespace {

// Core and boost policy. With the interactive governor (default kernel
// governor now) the HAL owns hotplug: all eight Cortex-A7 cores online while
// the screen is on, only cpu0 with it off; touches pulse the governor boost
// and launches hold it. The kernel's MTK thermal/low-battery limiter can still
// take cores offline (onlining then fails with EPERM, which is fine). With
// the old MTK "hotplug" governor the same policy is expressed through its
// cpu_num_base floor instead.
constexpr const char* kCpuNumBase = "/sys/devices/system/cpu/cpufreq/hotplug/cpu_num_base";
constexpr int kScreenOffCores = 1;
constexpr int kScreenOnCores = 8;
constexpr int kBoostCores = 8;
constexpr auto kLaunchMaxBoost = std::chrono::seconds(3);       // LAUNCH(0) normally ends it
constexpr auto kInteractionBoost = std::chrono::milliseconds(1000);

// CPU undervolt on top of MTK PTP (per-chip closed-loop voltage). PTP adds
// mt_ptp_offset (units of 6.25 mV PMIC codes, clamped by the kernel to
// 0.90..1.225 V) to every OPP. -17 once oopsed after ~48 min because the
// middle OPPs had never been stress-tested at their lowered voltage. -15
// passed per-OPP checksum stress, but with the turbo below (1768 MHz at the
// top OPP's voltage) a memory-heavy verified fill (memstress) hit 3 bad pages
// in 11 runs; -15 alone and turbo alone were clean. -10 + turbo: 0 bad pages
// in 10 memstress runs plus combined GPU/memory/CPU load.
constexpr const char* kPtpOffset = "/proc/ptp/ptp_offset";
constexpr int kPtpOffset_ = -10;

// Turbo: +104 MHz on the top OPP (1664 -> 1768 MHz). This unit's eFuse only
// grants it with 1-2 cores online; the kernel's 5-8 core bonus (mt_cpufreq)
// extends it to all cores. Format: "<0=on> <3-4 cores> <1-2 cores> <5-8 cores>"
// in kHz. Validated with the offset above (8 cores, 1768 MHz measured).
constexpr const char* kTurboMode = "/proc/cpufreq/cpufreq_highest_turbo_mode";
constexpr const char* kTurboValue = "0 104000 104000 104000";

void applyUndervolt() {
    std::ofstream f(kPtpOffset);
    f << kPtpOffset_;
    if (!f) ALOGW("cannot write %s", kPtpOffset);
    std::ofstream t(kTurboMode);
    t << kTurboValue;
    if (!t) ALOGW("cannot write %s", kTurboMode);
}

std::mutex gLock;
std::condition_variable gCond;
int gFloor = kScreenOnCores;
bool gBoosted = false;
uint64_t gGeneration = 0;

bool writeFile(const char* path, const char* value) {
    std::ofstream f(path);
    f << value;
    return static_cast<bool>(f);
}

bool interactiveGovernor() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    std::string gov;
    f >> gov;
    return gov == "interactive";
}

void setOnlineCores(int cores) {
    for (int cpu = 1; cpu < kBoostCores; cpu++) {
        std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online";
        // EPERM while the MTK thermal/battery limiter caps the core count.
        writeFile(path.c_str(), cpu < cores ? "1" : "0");
    }
}

void writeCores(int cores) {
    if (interactiveGovernor()) {
        setOnlineCores(cores);
    } else if (!writeFile(kCpuNumBase, std::to_string(cores).c_str())) {
        ALOGW("cannot write %s", kCpuNumBase);
    }
}

constexpr const char* kInteractiveBoost = "/sys/devices/system/cpu/cpufreq/interactive/boost";

// Must hold gLock.
void applyLocked() {
    if (interactiveGovernor()) {
        setOnlineCores(gFloor);
        writeFile(kInteractiveBoost, gBoosted ? "1" : "0");
    } else {
        writeCores(gBoosted ? kBoostCores : gFloor);
    }
}

// Start (or extend) a boost that ends after `duration` unless renewed.
template <typename Duration>
void boostFor(Duration duration) {
    std::unique_lock<std::mutex> lock(gLock);
    uint64_t gen = ++gGeneration;
    if (!gBoosted) {
        gBoosted = true;
        applyLocked();
    }
    gCond.notify_all();
    std::thread([gen, duration]() {
        std::unique_lock<std::mutex> l(gLock);
        if (!gCond.wait_for(l, duration, [gen] { return gGeneration != gen; }) && gBoosted) {
            gBoosted = false;
            applyLocked();
        }
    }).detach();
}

void endBoost() {
    std::unique_lock<std::mutex> lock(gLock);
    ++gGeneration;
    gCond.notify_all();
    if (gBoosted) {
        gBoosted = false;
        applyLocked();
    }
}

void setFloor(int cores) {
    std::unique_lock<std::mutex> lock(gLock);
    gFloor = cores;
    if (cores == kScreenOffCores) {
        // screen off: drop any boost right away
        ++gGeneration;
        gBoosted = false;
        gCond.notify_all();
    }
    applyLocked();
}

}  // namespace

Power::Power() {
    applyUndervolt();
}

Return<void> Power::setInteractive(bool interactive) {
    ALOGV("setInteractive(%d)", interactive);
    if (interactive) applyUndervolt();
    setFloor(interactive ? kScreenOnCores : kScreenOffCores);
    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    ALOGV("powerHint(%d, %d)", static_cast<int>(hint), data);
    switch (hint) {
        case PowerHint::LAUNCH:
            if (data != 0) boostFor(kLaunchMaxBoost);
            else endBoost();
            break;
        case PowerHint::INTERACTION:
            boostFor(kInteractionBoost);
            break;
        default:
            break;
    }
    return Void();
}

Return<void> Power::setFeature(Feature feature, bool activate) {
    // POWER_FEATURE_DOUBLE_TAP_TO_WAKE is not supported by this hardware.
    ALOGV("setFeature(%d, %d) ignored", static_cast<int>(feature), activate);
    return Void();
}

Return<void> Power::getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) {
    hidl_vec<PowerStatePlatformSleepState> states;
    _hidl_cb(states, Status::SUCCESS);
    return Void();
}

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
