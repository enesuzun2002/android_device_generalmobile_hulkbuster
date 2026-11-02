#define LOG_TAG "thermal.hulkbuster"

#include <android/hardware/thermal/1.0/IThermal.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Thermal.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::thermal::V1_0::IThermal;

int main() {
    configureRpcThreadpool(1, true /* callerWillJoin */);

    android::sp<IThermal> thermal = new android::hardware::thermal::V1_0::hulkbuster::Thermal();
    if (thermal->registerAsService() != android::OK) {
        ALOGE("Could not register IThermal/default");
        return 1;
    }
    ALOGI("IThermal/default registered");
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool never returns
}
