#define LOG_TAG "power.hulkbuster"

#include "Power.h"

#include <log/log.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace hulkbuster {

namespace {

// MTK hotplug governor: minimum number of online cores. The governor adds
// cores on load and input by itself, but it reacts in 150-300 ms, which is
// most of a cold app start or a fling on these Cortex-A7 cores. While the
// screen is on all eight cores stay online (cpufreq still scales their clock
// and the MTK thermal policy can still cap cores if the die heats up); with
// the screen off the governor is free to take them down to one. The boosts
// only matter if the screen-on floor is ever lowered again.
constexpr const char* kCpuNumBase = "/sys/devices/system/cpu/cpufreq/hotplug/cpu_num_base";
constexpr int kScreenOffCores = 1;
constexpr int kScreenOnCores = 8;
constexpr int kBoostCores = 8;
constexpr auto kLaunchMaxBoost = std::chrono::seconds(3);       // LAUNCH(0) normally ends it
constexpr auto kInteractionBoost = std::chrono::milliseconds(1000);

std::mutex gLock;
std::condition_variable gCond;
int gFloor = kScreenOnCores;
bool gBoosted = false;
uint64_t gGeneration = 0;

void writeCores(int cores) {
    std::ofstream f(kCpuNumBase);
    f << cores;
    if (!f) ALOGW("cannot write %s", kCpuNumBase);
}

// Must hold gLock.
void applyLocked() {
    writeCores(gBoosted ? kBoostCores : gFloor);
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

Return<void> Power::setInteractive(bool interactive) {
    ALOGV("setInteractive(%d)", interactive);
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
