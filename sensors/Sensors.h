/*
 * Native ISensors@1.0 for the GM E-Tab5 (MT6592, "hulkbuster").
 *
 * Kernel interfaces (3.10 MTK hwmsen framework):
 *   - /dev/hwmsensor ioctls: enable/disable, set delay and snapshot read of
 *     the latest value of every attached sensor (accelerometer BMA255,
 *     gyroscope BMG160, light/proximity CM36283).
 *   - input device "hwmdata": the kernel sends REL_Y with a bitmask of the
 *     hwmsen sensors that have new data; used as the data-ready signal.
 *   - magnetometer BMM050: the hwmsen calibrated data needs the closed
 *     bmm050d daemon, so the raw value is read from
 *     /sys/devices/platform/msensor/driver/sensordata (uT) on a timer.
 */
#ifndef HULKBUSTER_SENSORS_H
#define HULKBUSTER_SENSORS_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace hulkbuster {

using ::android::sp;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

class Sensors : public ISensors {
  public:
    Sensors();
    ~Sensors() override;

    Return<void> getSensorsList(getSensorsList_cb _hidl_cb) override;
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t sensorHandle, bool enabled) override;
    Return<void> poll(int32_t maxCount, poll_cb _hidl_cb) override;
    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override;
    Return<Result> flush(int32_t sensorHandle) override;
    Return<Result> injectSensorData(const Event& event) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override;
    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;
    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override;

  private:
    static constexpr int kMaxSensors = 5;

    // Run-time state of one sensor, protected by mStateLock.
    struct State {
        bool present = false;
        bool enabled = false;
        int64_t periodNs = 0;
        int initialTries = 0;   // >0: still waiting to emit a first value
        int64_t lastTime = -1;  // last hwmsen sample time emitted (fallback dedupe)
        int64_t nextReadNs = 0; // magnetometer: next sysfs read deadline
    };

    void threadLoop();
    void wake();
    void push(const Event& ev, bool wakeUp);
    void readHwm(unsigned mask, bool fallback);
    void readMag(int64_t nowNs);
    void releaseWakeLock();
    void acquireWakeLock();
    int indexOf(int32_t handle) const;
    Result setHwmDelay(int idx, int64_t periodNs);

    std::vector<SensorInfo> mList;
    State mState[kMaxSensors];

    // Protected by mLock (event queue)
    std::mutex mLock;
    std::condition_variable mCond;
    std::deque<Event> mQueue;
    int mWakeEventsQueued = 0;
    bool mWakeLockHeld = false;

    // Protected by mStateLock (sensor state shared with the reader thread)
    std::mutex mStateLock;

    int mHwmFd = -1;
    int mInputFd = -1;
    int mMagFd = -1;
    int mWakeFd = -1;  // eventfd used to kick the reader thread
    int mWakeLockFd = -1;
    int mWakeUnlockFd = -1;
    std::thread mThread;
};

}  // namespace hulkbuster
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif
