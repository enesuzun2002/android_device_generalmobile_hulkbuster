#define LOG_TAG "power.hulkbuster"

#include <android/hardware/power/1.0/IPower.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Power.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::power::V1_0::IPower;

int main() {
    configureRpcThreadpool(1, true /* callerWillJoin */);

    android::sp<IPower> power = new android::hardware::power::V1_0::hulkbuster::Power();
    if (power->registerAsService() != android::OK) {
        ALOGE("Could not register IPower/default");
        return 1;
    }
    ALOGI("IPower/default registered");
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool never returns
}
