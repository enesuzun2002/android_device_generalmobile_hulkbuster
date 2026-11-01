/*
 * Sensors HAL wrapper for the GM E-Tab5 (hulkbuster).
 *
 * The MediaTek sensors.mt6592.so blob only lists BMA056 accelerometer,
 * BMM056 magnetic/orientation and RPR410 light/proximity. The tablet also
 * has a Bosch BMG160 gyroscope, which the kernel drives through hwmsen
 * (ID_GYROSCOPE, /dev/hwmsensor) but which the blob never exposes.
 *
 * This module (sensors.hulkbuster.so, selected with ro.hardware.sensors)
 * loads the unmodified blob, passes its sensors through, and adds the
 * gyroscope. Gyro data is read with HWM_IO_GET_SENSORS_DATA on a separate
 * /dev/hwmsensor handle; the ioctls are per-sensor and read-only for data,
 * so they do not disturb the blob's own use of hwmsen.
 *
 * Events from the blob (polled on a helper thread) and from the gyroscope
 * thread are merged into one queue that poll() drains.
 */

#define LOG_TAG "SensorsWrapper"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>

#define VENDOR_HAL_PATH   "/system/lib/hw/sensors.mt6592.so"
#define HWMSENSOR_DEV     "/dev/hwmsensor"

/* ---- hwmsen ABI (kernel include/linux/hwmsensor.h, hwmsen_dev.c) ---- */
#define HWM_ID_GYROSCOPE        3       /* SENSOR_TYPE_GYROSCOPE - 1 */
#define HWM_MAX_SENSOR_NUM      22      /* ID_TILT_DETECTOR + 1 */

typedef struct {
    int sensor;
    int values[6];
    uint32_t value_divide;
    int8_t status;
    int update;
    int64_t time;
    uint32_t reserved;
} hwm_sensor_data;

typedef struct {
    hwm_sensor_data data[HWM_MAX_SENSOR_NUM];
    int date_type;
} hwm_trans_data;

struct hwm_sensor_delay {
    int handle;
    uint32_t delay;
};

#define HWM_IOC_MAGIC               0x91
#define HWM_IO_SET_DELAY            _IOW(HWM_IOC_MAGIC, 0x01, uint32_t)
#define HWM_IO_ENABLE_SENSOR        _IOW(HWM_IOC_MAGIC, 0x03, uint32_t)
#define HWM_IO_DISABLE_SENSOR       _IOW(HWM_IOC_MAGIC, 0x04, uint32_t)
#define HWM_IO_GET_SENSORS_DATA     _IOWR(HWM_IOC_MAGIC, 0x07, hwm_trans_data)

/* ---- gyroscope description ---- */
#define GYRO_HANDLE         4           /* unused by the blob (1,2,3,5,8) */
#define GYRO_MIN_DELAY_US   10000
#define GYRO_DEFAULT_NS     200000000LL

static struct sensor_t gyro_sensor = {
    .name = "BMG160 3-axis Gyroscope",
    .vendor = "Bosch",
    .version = 1,
    .handle = GYRO_HANDLE,
    .type = SENSOR_TYPE_GYROSCOPE,
    .maxRange = 34.906586f,             /* 2000 deg/s */
    .resolution = 0.0010642f,           /* 1 LSB at 16.4 LSB/(deg/s) */
    .power = 5.0f,
    .minDelay = GYRO_MIN_DELAY_US,
    .fifoReservedEventCount = 0,
    .fifoMaxEventCount = 0,
    .stringType = SENSOR_STRING_TYPE_GYROSCOPE,
    .requiredPermission = "",
    .maxDelay = 1000000,
    .flags = SENSOR_FLAG_CONTINUOUS_MODE,
};

/* ---- merged event queue ---- */
#define QUEUE_SIZE 256

static sensors_event_t queue[QUEUE_SIZE];
static int q_head, q_count;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;

static void queue_push(const sensors_event_t *ev, int n)
{
    int i;

    pthread_mutex_lock(&q_lock);
    for (i = 0; i < n; i++) {
        if (q_count == QUEUE_SIZE) {    /* drop the oldest on overflow */
            q_head = (q_head + 1) % QUEUE_SIZE;
            q_count--;
        }
        queue[(q_head + q_count) % QUEUE_SIZE] = ev[i];
        q_count++;
    }
    pthread_cond_broadcast(&q_cond);
    pthread_mutex_unlock(&q_lock);
}

/* ---- state ---- */
static struct sensors_module_t *vendor_module;
static struct sensors_poll_device_1 *vendor_dev;
static const struct sensor_t *vendor_list;
static int vendor_count;
static struct sensor_t *merged_list;
static int merged_count;

static int hwm_fd = -1;
static pthread_mutex_t gyro_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gyro_cond = PTHREAD_COND_INITIALIZER;
static int gyro_enabled;
static int64_t gyro_period_ns = GYRO_DEFAULT_NS;
static int gyro_flush_pending;

static pthread_t vendor_thread, gyro_thread;
static int threads_started;

static int64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int load_vendor(void)
{
    void *handle;

    if (vendor_module)
        return 0;

    handle = dlopen(VENDOR_HAL_PATH, RTLD_NOW);
    if (!handle) {
        ALOGE("dlopen %s failed: %s", VENDOR_HAL_PATH, dlerror());
        return -EINVAL;
    }
    vendor_module = (struct sensors_module_t *)dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR);
    if (!vendor_module) {
        ALOGE("no %s in %s", HAL_MODULE_INFO_SYM_AS_STR, VENDOR_HAL_PATH);
        dlclose(handle);
        return -EINVAL;
    }
    return 0;
}

static int build_list(void)
{
    if (merged_list)
        return 0;
    if (load_vendor())
        return -EINVAL;

    vendor_count = vendor_module->get_sensors_list(vendor_module, &vendor_list);
    if (vendor_count < 0)
        vendor_count = 0;

    merged_list = calloc(vendor_count + 1, sizeof(struct sensor_t));
    if (!merged_list)
        return -ENOMEM;
    if (vendor_count)
        memcpy(merged_list, vendor_list, vendor_count * sizeof(struct sensor_t));
    merged_list[vendor_count] = gyro_sensor;
    merged_count = vendor_count + 1;
    return 0;
}

/* ---- threads ---- */
static void *vendor_poll_loop(void *arg)
{
    sensors_event_t buf[32];
    int n;

    (void)arg;
    for (;;) {
        n = vendor_dev->poll(&vendor_dev->v0, buf, 32);
        if (n > 0)
            queue_push(buf, n);
        else if (n < 0 && n != -EINTR)
            usleep(10000);
    }
    return NULL;
}

static void *gyro_poll_loop(void *arg)
{
    hwm_trans_data td;
    int64_t last_time = -1;

    (void)arg;
    for (;;) {
        int64_t period;
        int flush;

        pthread_mutex_lock(&gyro_lock);
        while (!gyro_enabled && !gyro_flush_pending)
            pthread_cond_wait(&gyro_cond, &gyro_lock);
        period = gyro_period_ns;
        flush = gyro_flush_pending;
        gyro_flush_pending = 0;
        pthread_mutex_unlock(&gyro_lock);

        if (flush) {
            sensors_event_t ev;

            memset(&ev, 0, sizeof(ev));
            ev.version = META_DATA_VERSION;
            ev.type = SENSOR_TYPE_META_DATA;
            ev.meta_data.what = META_DATA_FLUSH_COMPLETE;
            ev.meta_data.sensor = GYRO_HANDLE;
            queue_push(&ev, 1);
        }

        if (gyro_enabled && hwm_fd >= 0) {
            memset(&td, 0, sizeof(td));
            td.date_type = 1 << HWM_ID_GYROSCOPE;
            if (ioctl(hwm_fd, HWM_IO_GET_SENSORS_DATA, &td) == 0) {
                const hwm_sensor_data *d = &td.data[HWM_ID_GYROSCOPE];

                if (d->value_divide && d->time != last_time) {
                    sensors_event_t ev;
                    float div = (float)d->value_divide;

                    last_time = d->time;
                    memset(&ev, 0, sizeof(ev));
                    ev.version = sizeof(sensors_event_t);
                    ev.sensor = GYRO_HANDLE;
                    ev.type = SENSOR_TYPE_GYROSCOPE;
                    ev.timestamp = now_ns();
                    ev.gyro.x = d->values[0] / div;
                    ev.gyro.y = d->values[1] / div;
                    ev.gyro.z = d->values[2] / div;
                    ev.gyro.status = SENSOR_STATUS_ACCURACY_MEDIUM;
                    queue_push(&ev, 1);
                }
            }
        }

        if (gyro_enabled)
            usleep((useconds_t)(period / 1000));
    }
    return NULL;
}

static void start_threads(void)
{
    if (threads_started)
        return;
    threads_started = 1;
    pthread_create(&vendor_thread, NULL, vendor_poll_loop, NULL);
    pthread_create(&gyro_thread, NULL, gyro_poll_loop, NULL);
}

/* ---- gyroscope control ---- */
static int gyro_set_delay(int64_t period_ns)
{
    struct hwm_sensor_delay dp;

    if (period_ns < GYRO_MIN_DELAY_US * 1000LL)
        period_ns = GYRO_MIN_DELAY_US * 1000LL;

    pthread_mutex_lock(&gyro_lock);
    gyro_period_ns = period_ns;
    pthread_mutex_unlock(&gyro_lock);

    if (hwm_fd < 0)
        return -ENODEV;
    dp.handle = HWM_ID_GYROSCOPE;
    dp.delay = (uint32_t)(period_ns / 1000000LL);
    if (ioctl(hwm_fd, HWM_IO_SET_DELAY, &dp) < 0) {
        ALOGE("gyro set delay failed: %s", strerror(errno));
        return -errno;
    }
    return 0;
}

static int gyro_activate(int enabled)
{
    uint32_t id = HWM_ID_GYROSCOPE;

    if (hwm_fd < 0)
        return -ENODEV;
    if (ioctl(hwm_fd, enabled ? HWM_IO_ENABLE_SENSOR : HWM_IO_DISABLE_SENSOR, &id) < 0) {
        ALOGE("gyro %s failed: %s", enabled ? "enable" : "disable", strerror(errno));
        return -errno;
    }
    pthread_mutex_lock(&gyro_lock);
    gyro_enabled = enabled;
    pthread_cond_broadcast(&gyro_cond);
    pthread_mutex_unlock(&gyro_lock);
    ALOGI("gyroscope %s", enabled ? "enabled" : "disabled");
    return 0;
}

/* ---- poll device ---- */
static int wrap_activate(struct sensors_poll_device_t *dev, int handle, int enabled)
{
    (void)dev;
    if (handle == GYRO_HANDLE)
        return gyro_activate(enabled);
    return vendor_dev->activate(&vendor_dev->v0, handle, enabled);
}

static int wrap_set_delay(struct sensors_poll_device_t *dev, int handle, int64_t ns)
{
    (void)dev;
    if (handle == GYRO_HANDLE)
        return gyro_set_delay(ns);
    return vendor_dev->setDelay(&vendor_dev->v0, handle, ns);
}

static int wrap_poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int count)
{
    int n = 0;

    (void)dev;
    pthread_mutex_lock(&q_lock);
    while (q_count == 0)
        pthread_cond_wait(&q_cond, &q_lock);
    while (q_count > 0 && n < count) {
        data[n++] = queue[q_head];
        q_head = (q_head + 1) % QUEUE_SIZE;
        q_count--;
    }
    pthread_mutex_unlock(&q_lock);
    return n;
}

static int wrap_batch(struct sensors_poll_device_1 *dev, int handle, int flags,
                      int64_t period_ns, int64_t timeout)
{
    (void)dev;
    if (handle == GYRO_HANDLE)
        return gyro_set_delay(period_ns);
    if (vendor_dev->common.version >= SENSORS_DEVICE_API_VERSION_1_0 && vendor_dev->batch)
        return vendor_dev->batch(vendor_dev, handle, flags, period_ns, timeout);
    return vendor_dev->setDelay(&vendor_dev->v0, handle, period_ns);
}

static int wrap_flush(struct sensors_poll_device_1 *dev, int handle)
{
    (void)dev;
    if (handle == GYRO_HANDLE) {
        pthread_mutex_lock(&gyro_lock);
        gyro_flush_pending = 1;
        pthread_cond_broadcast(&gyro_cond);
        pthread_mutex_unlock(&gyro_lock);
        return 0;
    }
    if (vendor_dev->common.version >= SENSORS_DEVICE_API_VERSION_1_1 && vendor_dev->flush)
        return vendor_dev->flush(vendor_dev, handle);
    return -EINVAL;
}

static int wrap_close(struct hw_device_t *dev)
{
    (void)dev;
    /* system_server never closes the sensors HAL; keep the threads alive. */
    return 0;
}

static int open_sensors(const struct hw_module_t *module, const char *id,
                        struct hw_device_t **device)
{
    struct sensors_poll_device_1 *dev;
    int err;

    if (strcmp(id, SENSORS_HARDWARE_POLL))
        return -EINVAL;
    if ((err = build_list()))
        return err;

    err = vendor_module->common.methods->open(&vendor_module->common, id,
                                               (struct hw_device_t **)&vendor_dev);
    if (err || !vendor_dev) {
        ALOGE("vendor sensors open failed: %d", err);
        return err ? err : -EINVAL;
    }

    hwm_fd = open(HWMSENSOR_DEV, O_RDWR);
    if (hwm_fd < 0)
        ALOGE("open %s failed: %s (gyroscope unavailable)", HWMSENSOR_DEV, strerror(errno));

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -ENOMEM;
    dev->common.tag = HARDWARE_DEVICE_TAG;
    /* Expose the vendor device's API level, never more. */
    dev->common.version = vendor_dev->common.version;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = wrap_close;
    dev->activate = wrap_activate;
    dev->setDelay = wrap_set_delay;
    dev->poll = wrap_poll;
    if (dev->common.version >= SENSORS_DEVICE_API_VERSION_1_0)
        dev->batch = wrap_batch;
    if (dev->common.version >= SENSORS_DEVICE_API_VERSION_1_1)
        dev->flush = wrap_flush;

    ALOGI("vendor HAL version 0x%x, %d vendor sensors + gyroscope",
          vendor_dev->common.version, vendor_count);

    start_threads();
    *device = &dev->common;
    return 0;
}

static int get_sensors_list(struct sensors_module_t *module, const struct sensor_t **list)
{
    (void)module;
    if (build_list()) {
        *list = NULL;
        return 0;
    }
    *list = merged_list;
    return merged_count;
}

static struct hw_module_methods_t sensors_module_methods = {
    .open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "E-Tab5 sensors wrapper (MTK blob + BMG160 gyroscope)",
        .author = "hulkbuster",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = get_sensors_list,
};
