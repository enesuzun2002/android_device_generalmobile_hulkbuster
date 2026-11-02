#define LOG_TAG "power.hulkbuster"

#include "Power.h"

#include <log/log.h>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace hulkbuster {

Return<void> Power::setInteractive(bool interactive) {
    // Screen on/off: the MTK hotplug cpufreq governor handles the idle
    // transition (offlines cores) by itself, nothing to do here.
    ALOGV("setInteractive(%d)", interactive);
    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t /* data */) {
    // No boost knobs are used: INTERACTION/LAUNCH/etc. are ignored, matching
    // the stock power.default.so stub this replaces.
    ALOGV("powerHint(%d)", static_cast<int>(hint));
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
