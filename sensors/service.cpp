#define LOG_TAG "sensors.hulkbuster"

#include <android/hardware/sensors/1.0/ISensors.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Sensors.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::sensors::V1_0::ISensors;

int main() {
    // The sensors framework needs two threads: one blocks in poll(), the
    // other serves activate/batch/flush.
    configureRpcThreadpool(2, true /* callerWillJoin */);

    android::sp<ISensors> sensors = new android::hardware::sensors::V1_0::hulkbuster::Sensors();
    if (sensors->registerAsService() != android::OK) {
        ALOGE("Could not register ISensors/default");
        return 1;
    }
    ALOGI("ISensors/default registered");
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool never returns
}
