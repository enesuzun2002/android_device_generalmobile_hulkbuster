#define LOG_TAG "lights.hulkbuster"

#include <android/hardware/light/2.0/ILight.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Light.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::light::V2_0::ILight;

int main() {
    configureRpcThreadpool(1, true /* callerWillJoin */);

    android::sp<ILight> light = new android::hardware::light::V2_0::hulkbuster::Light();
    if (light->registerAsService() != android::OK) {
        ALOGE("Could not register ILight/default");
        return 1;
    }
    ALOGI("ILight/default registered");
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool never returns
}
