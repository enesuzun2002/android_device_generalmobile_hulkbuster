#define LOG_TAG "lights.hulkbuster"

#include "Light.h"

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <log/log.h>

#include <stdio.h>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace hulkbuster {

static constexpr const char* kLcdBrightness = "/sys/class/leds/lcd-backlight/brightness";
static constexpr const char* kLcdMax = "/sys/class/leds/lcd-backlight/max_brightness";
static constexpr const char* kButtonBrightness = "/sys/class/leds/button-backlight/brightness";
static constexpr const char* kButtonMax = "/sys/class/leds/button-backlight/max_brightness";
static constexpr int kDefaultMax = 255;

Light::Light()
    : mBacklight{kLcdBrightness, kLcdMax, readMax(kLcdMax)},
      mButtons{kButtonBrightness, kButtonMax, readMax(kButtonMax)} {}

int Light::readMax(const char* path) {
    std::string s;
    int v = 0;
    if (base::ReadFileToString(path, &s) && sscanf(s.c_str(), "%d", &v) == 1 && v > 0) {
        return v;
    }
    return kDefaultMax;
}

bool Light::writeBrightness(const Led& led, int value) {
    if (!base::WriteStringToFile(base::StringPrintf("%d\n", value), led.path)) {
        ALOGE("failed to write %d to %s: %s", value, led.path, strerror(errno));
        return false;
    }
    return true;
}

// Standard luminance of an ARGB colour (alpha ignored), 0-255.
uint32_t Light::rgbToBrightness(uint32_t color) {
    uint32_t r = (color >> 16) & 0xff;
    uint32_t g = (color >> 8) & 0xff;
    uint32_t b = color & 0xff;
    return (77 * r + 150 * g + 29 * b) >> 8;
}

Return<Status> Light::setLight(Type type, const LightState& state) {
    const Led* led;
    switch (type) {
        case Type::BACKLIGHT:
            led = &mBacklight;
            break;
        case Type::BUTTONS:
            led = &mButtons;
            break;
        default:
            return Status::LIGHT_NOT_SUPPORTED;
    }

    // Brightness mode (user/sensor/low-persistence) is irrelevant for this
    // panel; flash modes do not apply to a backlight.
    int value = rgbToBrightness(state.color) * led->max / 255;

    std::lock_guard<std::mutex> lock(mLock);
    return writeBrightness(*led, value) ? Status::SUCCESS : Status::UNKNOWN;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    Type types[] = {Type::BACKLIGHT, Type::BUTTONS};
    hidl_vec<Type> vec;
    vec.setToExternal(types, sizeof(types) / sizeof(types[0]));
    _hidl_cb(vec);
    return Void();
}

}  // namespace hulkbuster
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
