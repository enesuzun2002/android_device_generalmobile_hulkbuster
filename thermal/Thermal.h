#ifndef HULKBUSTER_THERMAL_H
#define HULKBUSTER_THERMAL_H

#include <android/hardware/thermal/1.0/IThermal.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace thermal {
namespace V1_0 {
namespace hulkbuster {

using ::android::hardware::Return;
using ::android::hardware::Void;

struct Thermal : public IThermal {
    Return<void> getTemperatures(getTemperatures_cb _hidl_cb) override;
    Return<void> getCpuUsages(getCpuUsages_cb _hidl_cb) override;
    Return<void> getCoolingDevices(getCoolingDevices_cb _hidl_cb) override;
};

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // HULKBUSTER_THERMAL_H
