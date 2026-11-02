#pragma once

#include <android/hardware/power/1.0/IPower.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace hulkbuster {

struct Power : public IPower {
    Power();
    Return<void> setInteractive(bool interactive) override;
    Return<void> powerHint(PowerHint hint, int32_t data) override;
    Return<void> setFeature(Feature feature, bool activate) override;
    Return<void> getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) override;
};

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
