/*
 * Lollipop-era MTK camera HAL ABI (camera.default.so and its libcam.* /
 * libcam_utils / libmtk_mmutils closure) on top of Pie.
 *
 * Kept in its own small library (libui + liblog only) and shimmed into just
 * the camera blobs through TARGET_LD_SHIM_LIBS.
 */
#include <stdint.h>
#include <new>
#include <string>

#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <utils/Looper.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <sensor/Sensor.h>
#include <sensor/SensorEventQueue.h>
#include <sensor/SensorManager.h>

using namespace android;

/*
 * The stock (N-era) camera blobs allocate GraphicBuffer with 128 bytes
 * (libcam.client, libcam_utils) or 136 bytes (libmtk_mmutils); Pie's is 144.
 * Every blob site that does `operator new(N)` + GraphicBuffer::GraphicBuffer(...)
 * has been binary patched to `operator new(144)` (`movs r0,#N` -> `movs r0,#144`,
 * immediate byte -> 0x90): libcam.client.so @0x1f99e, libcam_utils.so @0x12cd4
 * and @0x13b38, libmtk_mmutils.so @0x2bc2. libcam.utils.sensorlistener.so
 * allocates a Looper with 104 bytes, Pie's is 112: @0x2706 (0x68 -> 0x70).
 * If any of this changes the patched immediates have to follow.
 */
static_assert(sizeof(GraphicBuffer) == 144,
              "GraphicBuffer size changed: re-patch the blob allocation sizes (0x90)");
static_assert(sizeof(Looper) == 112,
              "Looper size changed: re-patch libcam.utils.sensorlistener @0x2706 (0x70)");

extern "C" {

/* L: GraphicBuffer(w, h, format, usage) -- no layerCount, no requestorName.
 * Constructs a Pie GraphicBuffer in the (patched, 144-byte) storage the blob
 * allocated. ARM EABI constructors return `this`. */
__attribute__((visibility("default")))
void* _ZN7android13GraphicBufferC1Ejjij(void* self, uint32_t w, uint32_t h,
                                        int32_t format, uint32_t usage)
{
    new (self) GraphicBuffer(w, h, static_cast<PixelFormat>(format), 1u,
                             static_cast<uint64_t>(usage), "<Unknown>");
    return self;
}

/* L: GraphicBuffer(w, h, format, usage, stride, handle, keepOwnership). */
__attribute__((visibility("default")))
void* _ZN7android13GraphicBufferC1EjjijjP13native_handleb(
        void* self, uint32_t w, uint32_t h, int32_t format, uint32_t usage,
        uint32_t stride, native_handle_t* handle, bool keepOwnership)
{
    new (self) GraphicBuffer(w, h, static_cast<PixelFormat>(format), 1u,
                             static_cast<uint32_t>(usage), stride, handle,
                             keepOwnership);
    return self;
}

/* L: GraphicBufferMapper::lock(handle, int usage, const Rect&, void**).
 * Pie takes uint32_t usage (mangled `j`). Same forwarder as libmtk_symbols,
 * duplicated so libcam.client does not need to pull that library in. */
__attribute__((visibility("default")))
int _ZN7android19GraphicBufferMapper4lockEPK13native_handleiRKNS_4RectEPPv(
        GraphicBufferMapper* self, buffer_handle_t handle, int usage,
        const Rect& bounds, void** vaddr)
{
    return self->lock(handle, static_cast<uint32_t>(usage), bounds, vaddr);
}

/* L: GraphicBufferMapper::lockYCbCr(handle, int usage, const Rect&,
 *                                   android_ycbcr*). */
__attribute__((visibility("default")))
int _ZN7android19GraphicBufferMapper9lockYCbCrEPK13native_handleiRKNS_4RectEP13android_ycbcr(
        GraphicBufferMapper* self, buffer_handle_t handle, int usage,
        const Rect& bounds, android_ycbcr* ycbcr)
{
    return self->lockYCbCr(handle, static_cast<uint32_t>(usage), bounds, ycbcr);
}

/* MtkCameraParameters::APP_MODE_NAME_MTK_S3D. A char array like its siblings
 * (APP_MODE_NAME_MTK_ATV = "MtkAtv", ..._STEREO = "MtkStereo" in
 * libcamera_client_mtk.so; the blobs also reference "createCam1Device_MtkS3d"). */
extern const char _ZN7android19MtkCameraParameters21APP_MODE_NAME_MTK_S3DE[];  /* const => internal linkage in C++ without this */
__attribute__((visibility("default")))
const char _ZN7android19MtkCameraParameters21APP_MODE_NAME_MTK_S3DE[] = "MtkS3d";

/* The blobs libcam.client/libcam.camadapter were built against a 2-argument
 * CamExif::appendDebugExif(unsigned char*, unsigned int*); libcam.exif only
 * exports the later 3-argument form (extra `unsigned` selects the debug-info
 * key set: 0 = keys 2/4..8, nonzero = keys 10..13). The pre-flag behaviour
 * is the 0 set. libcam.exif is in the same load group (NEEDED by both
 * callers), so a weak reference resolves at load time. */
extern uint32_t _ZN7CamExif15appendDebugExifEPhPjj(void*, unsigned char*,
                                                    unsigned int*, unsigned int)
        __attribute__((weak));

__attribute__((visibility("default")))
uint32_t _ZN7CamExif15appendDebugExifEPhPj(void* self, unsigned char* buf,
                                            unsigned int* size)
{
    return _ZN7CamExif15appendDebugExifEPhPjj(self, buf, size, 0);
}


/*
 * libcam.utils.sensorlistener (stock, N-era) uses the pre-O libgui SensorManager
 * API: `new SensorManager()` through the Singleton<SensorManager> statics, then
 * getDefaultSensor / createEventQueue() on it. Pie moved this to libsensor with
 * a per-package instance, a private SensorManager(const String16&) constructor
 * and createEventQueue(String8, int). The blob's allocation is patched from 36
 * to 64 bytes (sizeof Pie SensorManager; libcam.utils.sensorlistener.so
 * @0x2676, 0x24 -> 0x40), this constructs a real Pie SensorManager in it, and
 * the one call with a changed signature is forwarded. getDefaultSensor and the
 * SensorEventQueue calls (read/getFd/enableSensor/...) keep their mangled names
 * in libsensor and resolve to it directly (libsensor is NEEDED by this
 * library). The Singleton statics are only locked/stored by the blob's own
 * inlined getInstance(), so zero-initialised storage is enough.
 */
static_assert(sizeof(SensorManager) <= 64,
              "SensorManager size changed: re-patch libcam.utils.sensorlistener @0x2676");

/* SensorManager::SensorManager(const String16&) is private; use its symbol. */
extern "C" void* pie_SensorManager_ctor(void* self, const String16& opPackageName)
        __asm__("_ZN7android13SensorManagerC1ERKNS_8String16E");

__attribute__((visibility("default")))
uint32_t _ZN7android9SingletonINS_13SensorManagerEE5sLockE;
__attribute__((visibility("default")))
void* _ZN7android9SingletonINS_13SensorManagerEE9sInstanceE;

__attribute__((visibility("default")))
void* _ZN7android13SensorManagerC1Ev(void* self)
{
    pie_SensorManager_ctor(self, String16("camera"));
    return self;
}

/* sp<SensorEventQueue> is returned through a hidden first argument (ARM EABI). */
__attribute__((visibility("default")))
void _ZN7android13SensorManager16createEventQueueEv(sp<SensorEventQueue>* ret,
                                                     SensorManager* self)
{
    new (ret) sp<SensorEventQueue>(self->createEventQueue(String8("camera"), 0));
}

/*
 * camera.mt6592.so lists libmmsdkservice.so (MTK "media.mmsdk" client) as
 * DT_NEEDED, but the stock image does not ship that library's own
 * dependencies (libcam.sdkclient.so, libmmsdkservice.feature.so), so it cannot
 * load. camera.mt6592.so's DT_NEEDED "libmmsdkservice.so" is binary patched to
 * "libmtk_camera.so" and this provides its only import,
 * IMMSdkService::asInterface(). The HAL reaches it only after
 * checkService("media.mmsdk") returned a binder, which Pie never registers, and
 * handles a null sp ("Null mmsdk service"). Always return null; the matching
 * sp<IMMSdkService> destructors are instantiated inside camera.mt6592.so.
 */
__attribute__((visibility("default")))
void _ZN7android7NSMMSdk13IMMSdkService11asInterfaceERKNS_2spINS_7IBinderEEE(
        void** ret, const void* binder)
{
    (void)binder;
    *ret = nullptr;
}
}

/* libMtkOmxVdec (L): AwesomePlayer::mtk_omx_get_current_time(int64_t*) syncs
 * the decoder with the L-era AwesomePlayer, which Pie does not have. Report
 * "no clock" so the decoder runs free, as with any other Pie player. */
extern "C" int _ZN7android13AwesomePlayer24mtk_omx_get_current_timeEPx(int64_t* time) {
    if (time) *time = -1;
    return -1;
}

/*
 * libcam.paramsmgr builds the camera feature table (white balance, effects,
 * scene/focus/antibanding modes, ...) by dlopen()ing itself and dlsym()ing
 * queryCustCamfeature_<sensor drvname>. The stock blob was generated for
 * another product and only carries OV13850/OV8858/OV5670/GC2355 tables, so
 * our S5K4E1GA (5 MP main) and GC2235 (2 MP sub) get no features: white
 * balance stays 0 (OFF), 3A rejects it and startPreview fails. dlsym on that
 * handle also searches its NEEDED tree, which includes this shim, so forward
 * the two names to the closest tables the blob has. The main camera uses the
 * OV8858 table: the OV5670 one is fixed-focus and only offers "infinity", which
 * left the FM50AF lens parked. OV8858 also lists upscaled picture sizes; the
 * camera device HAL hides them through ro.vendor.camera0.max_picture_size.
 */
#include <dlfcn.h>

extern "C" {

typedef void* (*cust_feature_fn)(void*, void*, void*, void*);

static void* forward_cust_feature(const char* target, void* a, void* b, void* c, void* d)
{
    void* lib = dlopen("libcam.paramsmgr.so", RTLD_NOW | RTLD_NOLOAD);
    if (lib == nullptr) return nullptr;
    cust_feature_fn fn = reinterpret_cast<cust_feature_fn>(dlsym(lib, target));
    dlclose(lib);
    return fn != nullptr ? fn(a, b, c, d) : nullptr;
}

__attribute__((visibility("default")))
void* queryCustCamfeature_SENSOR_DRVNAME_S5K4E1GA_MIPI_RAW(void* a, void* b, void* c, void* d)
{
    return forward_cust_feature("queryCustCamfeature_SENSOR_DRVNAME_OV8858_MIPI_RAW", a, b, c, d);
}

__attribute__((visibility("default")))
void* queryCustCamfeature_SENSOR_DRVNAME_GC2235_MIPI_RAW(void* a, void* b, void* c, void* d)
{
    return forward_cust_feature("queryCustCamfeature_SENSOR_DRVNAME_GC2355_MIPI_RAW", a, b, c, d);
}

}  // extern "C" (feature forwarders)
