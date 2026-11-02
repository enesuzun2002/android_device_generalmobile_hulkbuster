#define LOG_TAG "sensors.hulkbuster"

#include "Sensors.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include <log/log.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace hulkbuster {

namespace {

/* ---- hwmsen kernel ABI (kernel include/linux/hwmsensor.h) ---- */
constexpr int kHwmMaxSensorNum = 22;  // ID_TILT_DETECTOR + 1

struct hwm_sensor_data {
    int sensor;
    int values[6];
    uint32_t value_divide;
    int8_t status;
    int update;
    int64_t time;  // ns, CLOCK_MONOTONIC (coarse)
    uint32_t reserved;
};

struct hwm_trans_data {
    hwm_sensor_data data[kHwmMaxSensorNum];
    int date_type;
};

struct hwm_sensor_delay {
    int handle;
    uint32_t delay;  // ms
};

constexpr unsigned kHwmIocMagic = 0x91;
#define HWM_IO_SET_DELAY _IOW(kHwmIocMagic, 0x01, uint32_t)
#define HWM_IO_ENABLE_SENSOR _IOW(kHwmIocMagic, 0x03, uint32_t)
#define HWM_IO_DISABLE_SENSOR _IOW(kHwmIocMagic, 0x04, uint32_t)
#define HWM_IO_ENABLE_SENSOR_NODATA _IOW(kHwmIocMagic, 0x05, uint32_t)
#define HWM_IO_DISABLE_SENSOR_NODATA _IOW(kHwmIocMagic, 0x06, uint32_t)
#define HWM_IO_GET_SENSORS_DATA _IOWR(kHwmIocMagic, 0x07, hwm_trans_data)

// hwmsen ids == Android sensor type - 1
constexpr int kIdAccel = 0;
constexpr int kIdMag = 1;
constexpr int kIdGyro = 3;
constexpr int kIdLight = 4;
constexpr int kIdProx = 7;

constexpr int kInvalid = -1;  // SENSOR_INVALID_VALUE

constexpr char kHwmDev[] = "/dev/hwmsensor";
constexpr char kInputName[] = "hwmdata";
constexpr char kMagSysfs[] = "/sys/devices/platform/msensor/driver/sensordata";
constexpr char kMagSysfsAlt[] = "/sys/bus/platform/drivers/msensor/sensordata";
constexpr char kWakeLockFile[] = "/sys/power/wake_lock";
constexpr char kWakeUnlockFile[] = "/sys/power/wake_unlock";
constexpr char kWakeLockName[] = "SensorsHAL_WAKEUP";

// Index order == handle - 1.
constexpr int kAccel = 0, kMag = 1, kGyro = 2, kLight = 3, kProx = 4;

// Kernel-side axis remap (cust_*.c direction 5: sign {+,+,-}, map {1,0,2}):
// out[map[i]] = sign[i] * raw[i]. The kernel applies it to accel/gyro but the
// raw magnetometer sysfs value is unmapped, so do it here.
constexpr int kMagMap[3] = {1, 0, 2};
constexpr int kMagSign[3] = {1, 1, -1};

constexpr int kMinDelayAccelUs = 10000;
constexpr int kMinDelayGyroUs = 10000;
constexpr int kMinDelayMagUs = 20000;  // bmm050 driver clamps to 20 ms
constexpr int64_t kLightMinPeriodNs = 100LL * 1000 * 1000;
constexpr int kInitialTries = 30;      // x100 ms to wait for a first on-change value

int64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int hwmIdFor(int idx) {
    switch (idx) {
        case kAccel: return kIdAccel;
        case kMag:   return kIdMag;
        case kGyro:  return kIdGyro;
        case kLight: return kIdLight;
        case kProx:  return kIdProx;
    }
    return -1;
}

bool isOnChange(int idx) { return idx == kLight || idx == kProx; }

int openInputByName(const char* name) {
    for (int i = 0; i < 32; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char buf[64] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(buf) - 1), buf) >= 0 && !strcmp(buf, name)) {
            ALOGI("using %s (%s) as data-ready source", path, name);
            return fd;
        }
        close(fd);
    }
    return -1;
}

SensorInfo makeInfo(int idx, const char* name, const char* vendor, SensorType type,
                    const char* typeStr, float maxRange, float res, float power, int32_t minDelay,
                    int32_t maxDelay, uint32_t flags) {
    SensorInfo si;
    si.sensorHandle = idx + 1;
    si.name = name;
    si.vendor = vendor;
    si.version = 1;
    si.type = type;
    si.typeAsString = typeStr;
    si.maxRange = maxRange;
    si.resolution = res;
    si.power = power;
    si.minDelay = minDelay;
    si.fifoReservedEventCount = 0;
    si.fifoMaxEventCount = 0;
    si.requiredPermission = "";
    si.maxDelay = maxDelay;
    si.flags = flags;
    return si;
}

}  // namespace

Sensors::Sensors() {
    mHwmFd = open(kHwmDev, O_RDWR | O_CLOEXEC);
    if (mHwmFd < 0) {
        ALOGE("open %s failed: %s - no hwmsen sensors", kHwmDev, strerror(errno));
    } else {
        mInputFd = openInputByName(kInputName);
        if (mInputFd < 0)
            ALOGW("input device '%s' not found, falling back to timed polling", kInputName);
        mState[kAccel].present = mState[kGyro].present = true;
        mState[kLight].present = mState[kProx].present = true;
    }

    mMagFd = open(kMagSysfs, O_RDONLY | O_CLOEXEC);
    if (mMagFd < 0) mMagFd = open(kMagSysfsAlt, O_RDONLY | O_CLOEXEC);
    if (mMagFd >= 0 && mHwmFd >= 0) {
        mState[kMag].present = true;
    } else {
        ALOGW("magnetometer raw data node unavailable (%s), magnetometer disabled",
              strerror(errno));
    }

    mWakeLockFd = open(kWakeLockFile, O_WRONLY | O_CLOEXEC);
    mWakeUnlockFd = open(kWakeUnlockFile, O_WRONLY | O_CLOEXEC);

    constexpr uint32_t kContinuous = static_cast<uint32_t>(SensorFlagBits::CONTINUOUS_MODE);
    constexpr uint32_t kOnChange = static_cast<uint32_t>(SensorFlagBits::ON_CHANGE_MODE);
    constexpr uint32_t kWakeUp = static_cast<uint32_t>(SensorFlagBits::WAKE_UP);

    mList.resize(kMaxSensors);
    mList[kAccel] = makeInfo(kAccel, "BMA255 3-axis Accelerometer", "Bosch",
                             SensorType::ACCELEROMETER, "android.sensor.accelerometer",
                             19.6133f /* 2 g */, 0.0096f, 0.13f, kMinDelayAccelUs, 1000000,
                             kContinuous);
    mList[kMag] = makeInfo(kMag, "BMM050 3-axis Magnetic field sensor", "Bosch",
                           SensorType::MAGNETIC_FIELD, "android.sensor.magnetic_field",
                           1300.0f, 1.0f, 0.17f, kMinDelayMagUs, 1000000, kContinuous);
    mList[kGyro] = makeInfo(kGyro, "BMG160 3-axis Gyroscope", "Bosch", SensorType::GYROSCOPE,
                            "android.sensor.gyroscope", 34.906586f /* 2000 dps */, 0.0010642f,
                            5.0f, kMinDelayGyroUs, 1000000, kContinuous);
    mList[kLight] = makeInfo(kLight, "CM36283 Light sensor", "Capella", SensorType::LIGHT,
                             "android.sensor.light", 10240.0f, 1.0f, 0.09f, 0, 0, kOnChange);
    mList[kProx] = makeInfo(kProx, "CM36283 Proximity sensor", "Capella", SensorType::PROXIMITY,
                            "android.sensor.proximity", 5.0f, 5.0f, 0.5f, 0, 0,
                            kOnChange | kWakeUp);

    mState[kAccel].periodNs = 200000000LL;
    mState[kMag].periodNs = 200000000LL;
    mState[kGyro].periodNs = 200000000LL;
    mState[kLight].periodNs = 200000000LL;
    mState[kProx].periodNs = 200000000LL;

    mWakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    mThread = std::thread(&Sensors::threadLoop, this);
}

Sensors::~Sensors() {
    // The service never exits normally; nothing to join.
    mThread.detach();
}

int Sensors::indexOf(int32_t handle) const {
    if (handle < 1 || handle > kMaxSensors) return -1;
    int idx = handle - 1;
    return mState[idx].present ? idx : -1;
}

/* ---- ISensors ---- */

Return<void> Sensors::getSensorsList(getSensorsList_cb _hidl_cb) {
    std::vector<SensorInfo> out;
    for (int i = 0; i < kMaxSensors; i++)
        if (mState[i].present) out.push_back(mList[i]);
    hidl_vec<SensorInfo> list;
    list.setToExternal(out.data(), out.size());
    _hidl_cb(list);
    return Void();
}

Return<Result> Sensors::setOperationMode(OperationMode mode) {
    return mode == OperationMode::NORMAL ? Result::OK : Result::BAD_VALUE;
}

Return<Result> Sensors::activate(int32_t sensorHandle, bool enabled) {
    int idx = indexOf(sensorHandle);
    if (idx < 0) return Result::BAD_VALUE;

    {
        std::lock_guard<std::mutex> g(mStateLock);
        State& st = mState[idx];
        if (st.enabled == enabled) return Result::OK;

        uint32_t id = (uint32_t)hwmIdFor(idx);
        unsigned long cmd;
        if (idx == kMag)  // only power the chip; no data through hwmsen
            cmd = enabled ? HWM_IO_ENABLE_SENSOR_NODATA : HWM_IO_DISABLE_SENSOR_NODATA;
        else
            cmd = enabled ? HWM_IO_ENABLE_SENSOR : HWM_IO_DISABLE_SENSOR;
        if (ioctl(mHwmFd, cmd, &id) < 0) {
            ALOGE("%s of sensor %d failed: %s", enabled ? "enable" : "disable", sensorHandle,
                  strerror(errno));
            return Result::INVALID_OPERATION;
        }
        st.enabled = enabled;
        st.lastTime = -1;
        st.initialTries = enabled ? kInitialTries : 0;
        st.nextReadNs = 0;
        ALOGI("sensor %d (%s) %s", sensorHandle, mList[idx].name.c_str(),
              enabled ? "enabled" : "disabled");
    }
    wake();
    return Result::OK;
}

Result Sensors::setHwmDelay(int idx, int64_t periodNs) {
    hwm_sensor_delay d;
    d.handle = hwmIdFor(idx);
    int64_t ms = periodNs / 1000000LL;
    d.delay = (uint32_t)std::max<int64_t>(ms, 1);
    if (ioctl(mHwmFd, HWM_IO_SET_DELAY, &d) < 0) {
        ALOGE("set delay of sensor %d failed: %s", idx + 1, strerror(errno));
        return Result::INVALID_OPERATION;
    }
    return Result::OK;
}

Return<Result> Sensors::batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                              int64_t /* maxReportLatencyNs */) {
    int idx = indexOf(sensorHandle);
    if (idx < 0 || samplingPeriodNs < 0) return Result::BAD_VALUE;

    const SensorInfo& si = mList[idx];
    int64_t period = samplingPeriodNs;
    if (si.minDelay > 0) {
        period = std::max<int64_t>(period, (int64_t)si.minDelay * 1000);
        period = std::min<int64_t>(period, (int64_t)si.maxDelay * 1000);
    } else if (idx == kLight) {
        period = std::max<int64_t>(period, kLightMinPeriodNs);
    }

    {
        std::lock_guard<std::mutex> g(mStateLock);
        mState[idx].periodNs = period;
    }
    Result r = Result::OK;
    if (idx != kMag && idx != kProx) r = setHwmDelay(idx, period);
    wake();
    return r;
}

Return<Result> Sensors::flush(int32_t sensorHandle) {
    int idx = indexOf(sensorHandle);
    if (idx < 0) return Result::BAD_VALUE;
    {
        std::lock_guard<std::mutex> g(mStateLock);
        if (!mState[idx].enabled) return Result::BAD_VALUE;
    }
    Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.timestamp = 0;
    ev.sensorHandle = sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    push(ev, false);
    return Result::OK;
}

Return<void> Sensors::poll(int32_t maxCount, poll_cb _hidl_cb) {
    std::vector<Event> out;
    // There is only ever one real poller, the newest one. An older poll()
    // can still be blocked here when its caller is gone: the previous
    // system_server died, or SensorDevice::poll() retried after a HIDL
    // transport error (it retries every 10 ms). Such a stale call would keep
    // a HIDL thread forever and could swallow events. So every poll(),
    // including SensorDevice::connectHidlService()'s poll(0) poke, starts a
    // new generation and wakes everyone; superseded waiters return empty.
    // (Exiting instead, like the stock HAL, turns a transport hiccup into a
    // SensorService abort and a framework restart.)
    uint64_t myGen;
    {
        std::lock_guard<std::mutex> lk(mLock);
        myGen = ++mPollGen;
    }
    mCond.notify_all();

    // The poke itself must return immediately (blocking would hang
    // system_server, since WindowManagerService waits for SensorService).
    if (maxCount <= 0) {
        _hidl_cb(Result::BAD_VALUE, hidl_vec<Event>(), hidl_vec<SensorInfo>());
        return Void();
    }
    {
        std::unique_lock<std::mutex> lk(mLock);
        mCond.wait(lk, [this, myGen] { return !mQueue.empty() || mPollGen != myGen; });
        int n = mPollGen != myGen ? 0 : std::min<int>(maxCount, (int)mQueue.size());
        out.reserve(n);
        for (int i = 0; i < n; i++) {
            Event ev = mQueue.front();
            mQueue.pop_front();
            if (ev.sensorType == SensorType::PROXIMITY) mWakeEventsQueued--;
            out.push_back(ev);
        }
        if (mWakeEventsQueued <= 0) {
            mWakeEventsQueued = 0;
            releaseWakeLock();
        }
    }
    hidl_vec<Event> data;
    data.setToExternal(out.data(), out.size());
    hidl_vec<SensorInfo> dynamicAdded;
    _hidl_cb(Result::OK, data, dynamicAdded);
    return Void();
}

Return<Result> Sensors::injectSensorData(const Event& /* event */) {
    return Result::INVALID_OPERATION;
}

Return<void> Sensors::registerDirectChannel(const SharedMemInfo& /* mem */,
                                            registerDirectChannel_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, -1);
    return Void();
}

Return<Result> Sensors::unregisterDirectChannel(int32_t /* channelHandle */) {
    return Result::INVALID_OPERATION;
}

Return<void> Sensors::configDirectReport(int32_t /* sensorHandle */, int32_t /* channelHandle */,
                                         RateLevel /* rate */,
                                         configDirectReport_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, -1);
    return Void();
}

/* ---- queue / wake lock ---- */

void Sensors::acquireWakeLock() {
    if (mWakeLockHeld || mWakeLockFd < 0) return;
    if (write(mWakeLockFd, kWakeLockName, strlen(kWakeLockName)) > 0) mWakeLockHeld = true;
}

void Sensors::releaseWakeLock() {
    if (!mWakeLockHeld || mWakeUnlockFd < 0) return;
    if (write(mWakeUnlockFd, kWakeLockName, strlen(kWakeLockName)) > 0) mWakeLockHeld = false;
}

void Sensors::push(const Event& ev, bool wakeUp) {
    {
        std::lock_guard<std::mutex> g(mLock);
        // Bound the queue; drop the oldest non-meta event on overflow.
        if (mQueue.size() >= 512) {
            for (auto it = mQueue.begin(); it != mQueue.end(); ++it) {
                if (it->sensorType != SensorType::META_DATA) {
                    if (it->sensorType == SensorType::PROXIMITY) mWakeEventsQueued--;
                    mQueue.erase(it);
                    break;
                }
            }
        }
        mQueue.push_back(ev);
        if (wakeUp) {
            mWakeEventsQueued++;
            acquireWakeLock();
        }
    }
    mCond.notify_all();
}

void Sensors::wake() {
    uint64_t one = 1;
    if (mWakeFd >= 0) (void)!write(mWakeFd, &one, sizeof(one));
}

/* ---- reader thread ---- */

void Sensors::readHwm(unsigned mask, bool fallback) {
    hwm_trans_data td;
    memset(&td, 0, sizeof(td));
    td.date_type = (int)mask;
    if (ioctl(mHwmFd, HWM_IO_GET_SENSORS_DATA, &td) < 0) {
        ALOGE("HWM_IO_GET_SENSORS_DATA failed: %s", strerror(errno));
        return;
    }

    for (int idx = 0; idx < kMaxSensors; idx++) {
        int id = hwmIdFor(idx);
        if (idx == kMag || id < 0 || !(mask & (1u << id))) continue;
        const hwm_sensor_data& d = td.data[id];

        bool valid;
        if (isOnChange(idx))
            valid = d.values[0] != kInvalid;
        else
            valid = !(d.values[0] == kInvalid && d.values[1] == kInvalid &&
                      d.values[2] == kInvalid);
        if (!valid) continue;

        {
            std::lock_guard<std::mutex> g(mStateLock);
            State& st = mState[idx];
            if (!st.enabled) continue;
            if (fallback && d.time == st.lastTime && st.initialTries == 0) continue;
            st.lastTime = d.time;
            st.initialTries = 0;
        }

        Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.timestamp = nowNs();
        ev.sensorHandle = idx + 1;
        ev.sensorType = mList[idx].type;
        bool wakeUp = false;
        switch (idx) {
            case kAccel: {  // value_divide 1000: mm/s^2 -> m/s^2
                float div = d.value_divide ? (float)d.value_divide : 1000.0f;
                ev.u.vec3.x = d.values[0] / div;
                ev.u.vec3.y = d.values[1] / div;
                ev.u.vec3.z = d.values[2] / div;
                ev.u.vec3.status = SensorStatus::ACCURACY_MEDIUM;
                break;
            }
            case kGyro: {  // value_divide DEGREE_TO_RAD: -> rad/s
                float div = d.value_divide ? (float)d.value_divide : (7506.0f * 164.0f);
                ev.u.vec3.x = d.values[0] / div;
                ev.u.vec3.y = d.values[1] / div;
                ev.u.vec3.z = d.values[2] / div;
                ev.u.vec3.status = SensorStatus::ACCURACY_MEDIUM;
                break;
            }
            case kLight: {  // already lux (kernel als_value table)
                float div = d.value_divide ? (float)d.value_divide : 1.0f;
                ev.u.scalar = d.values[0] / div;
                break;
            }
            case kProx:  // kernel: 0 = near, 1 = far. Android: cm.
                ev.u.scalar = d.values[0] == 0 ? 0.0f : mList[kProx].maxRange;
                wakeUp = true;
                break;
        }
        push(ev, wakeUp);
    }
}

void Sensors::readMag(int64_t now) {
    char buf[96];
    ssize_t n = pread(mMagFd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = 0;
    int raw[3];
    if (sscanf(buf, "%d %d %d", &raw[0], &raw[1], &raw[2]) != 3) return;
    // The chip returns zeros until the first forced measurement completed.
    if (raw[0] == 0 && raw[1] == 0 && raw[2] == 0) return;

    float v[3];
    for (int i = 0; i < 3; i++) v[kMagMap[i]] = (float)(kMagSign[i] * raw[i]);  // uT

    Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.timestamp = now;
    ev.sensorHandle = kMag + 1;
    ev.sensorType = SensorType::MAGNETIC_FIELD;
    ev.u.vec3.x = v[0];
    ev.u.vec3.y = v[1];
    ev.u.vec3.z = v[2];
    ev.u.vec3.status = SensorStatus::ACCURACY_MEDIUM;
    push(ev, false);
}

void Sensors::threadLoop() {
    const bool fallback = mInputFd < 0;
    unsigned evMask = 0;

    for (;;) {
        // Work out what is enabled and how long we may sleep.
        unsigned enabledMask = 0, initialMask = 0;
        bool magOn = false;
        int64_t now = nowNs();
        int64_t sleepNs = -1;  // -1 = forever
        auto limit = [&](int64_t ns) {
            if (ns < 0) ns = 0;
            if (sleepNs < 0 || ns < sleepNs) sleepNs = ns;
        };
        {
            std::lock_guard<std::mutex> g(mStateLock);
            for (int idx = 0; idx < kMaxSensors; idx++) {
                State& st = mState[idx];
                if (!st.present || !st.enabled) continue;
                if (idx == kMag) {
                    magOn = true;
                    if (st.nextReadNs == 0) st.nextReadNs = now;
                    limit(st.nextReadNs - now);
                    continue;
                }
                unsigned bit = 1u << hwmIdFor(idx);
                enabledMask |= bit;
                if (st.initialTries > 0) {
                    initialMask |= bit;
                    limit(100LL * 1000 * 1000);
                }
                if (fallback) limit(std::max<int64_t>(st.periodNs, 10LL * 1000 * 1000));
            }
        }

        struct pollfd pfd[2];
        int nfd = 0;
        pfd[nfd].fd = mWakeFd;
        pfd[nfd].events = POLLIN;
        nfd++;
        if (!fallback) {
            pfd[nfd].fd = mInputFd;
            pfd[nfd].events = POLLIN;
            nfd++;
        }
        int timeoutMs = sleepNs < 0 ? -1 : (int)((sleepNs + 999999) / 1000000);
        int r = ::poll(pfd, nfd, timeoutMs);
        if (r < 0 && errno != EINTR) {
            ALOGE("poll failed: %s", strerror(errno));
            usleep(100000);
            continue;
        }

        if (r > 0 && (pfd[0].revents & POLLIN)) {
            uint64_t junk;
            (void)!read(mWakeFd, &junk, sizeof(junk));
        }
        if (r > 0 && nfd > 1 && (pfd[1].revents & POLLIN)) {
            struct input_event ie[32];
            for (;;) {
                ssize_t n = read(mInputFd, ie, sizeof(ie));
                if (n <= 0) break;
                for (size_t i = 0; i < (size_t)n / sizeof(ie[0]); i++)
                    // hwmsen: REL_Y (EVENT_TYPE_SENSOR) carries a bitmask of updated sensors
                    if (ie[i].type == EV_REL && ie[i].code == REL_Y)
                        evMask |= (unsigned)ie[i].value;
            }
        }

        // Re-read what is enabled now (it may have changed while we slept).
        unsigned readMask;
        {
            std::lock_guard<std::mutex> g(mStateLock);
            enabledMask = 0;
            for (int idx = 0; idx < kMaxSensors; idx++)
                if (idx != kMag && mState[idx].present && mState[idx].enabled)
                    enabledMask |= 1u << hwmIdFor(idx);
        }
        if (fallback)
            readMask = enabledMask;
        else
            readMask = (evMask & enabledMask) | (initialMask & enabledMask);
        evMask = 0;
        if (readMask && mHwmFd >= 0) readHwm(readMask, fallback);

        if (magOn) {
            now = nowNs();
            bool due = false;
            {
                std::lock_guard<std::mutex> g(mStateLock);
                State& st = mState[kMag];
                if (st.enabled && now >= st.nextReadNs) {
                    due = true;
                    st.nextReadNs = now + st.periodNs;
                }
            }
            if (due) readMag(now);
        }

        // Count down the initial-value wait of on-change sensors.
        {
            std::lock_guard<std::mutex> g(mStateLock);
            for (int idx = 0; idx < kMaxSensors; idx++)
                if (mState[idx].initialTries > 0 && (initialMask & (1u << hwmIdFor(idx))))
                    mState[idx].initialTries--;
        }
    }
}

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
