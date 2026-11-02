#pragma once

#include <android/hardware/light/2.0/ILight.h>
#include <hidl/Status.h>

#include <mutex>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace hulkbuster {

struct Light : public ILight {
    Light();

    Return<Status> setLight(Type type, const LightState& state) override;
    Return<void> getSupportedTypes(getSupportedTypes_cb _hidl_cb) override;

  private:
    struct Led {
        const char* path;      // .../brightness
        const char* maxPath;   // .../max_brightness
        int max;               // read at startup, 255 fallback
    };

    static int readMax(const char* path);
    static bool writeBrightness(const Led& led, int value);
    static uint32_t rgbToBrightness(uint32_t color);

    std::mutex mLock;
    Led mBacklight;
    Led mButtons;
};

}  // namespace hulkbuster
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
