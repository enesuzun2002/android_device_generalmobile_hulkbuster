/*
 * Minimal legacy Wi-Fi vendor HAL for the MT6592 combo chip (hulkbuster).
 *
 * Pie's android.hardware.wifi@1.x service refuses to start when
 * init_wifi_vendor_hal_func_table() fails, so returning NOT_SUPPORTED (like
 * the framework's fallback) leaves Wi-Fi dead. The MTK driver has no vendor
 * nl80211 commands we need, so implement only the lifecycle the HIDL service
 * depends on and leave every optional feature to the framework's stubs:
 *
 *   wifi_initialize   allocate a handle and note the wlan/p2p interfaces
 *   wifi_event_loop   block until wifi_cleanup() asks us to stop, then call
 *                     the cleaned-up handler from this thread (the caller of
 *                     wifi_cleanup() holds the HAL lock while it waits)
 *   wifi_cleanup      wake the event loop
 *   wifi_get_ifaces / wifi_get_iface_name / wifi_get_supported_feature_set
 *
 * Everything else (gscan, RTT, link stats, logging, ...) stays NOT_SUPPORTED.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hardware_legacy/wifi_hal.h"

namespace {

constexpr int kMaxIfaces = 4;

struct MtIface {
    char name[IFNAMSIZ];
};

struct MtHal {
    int wake_fds[2] = {-1, -1};
    volatile bool exit_requested = false;
    wifi_cleaned_up_handler cleaned_up_handler = nullptr;
    int num_ifaces = 0;
    MtIface ifaces[kMaxIfaces];
    wifi_interface_handle handles[kMaxIfaces];
};

void discoverIfaces(MtHal* hal) {
    DIR* dir = opendir("/sys/class/net");
    if (!dir) return;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr && hal->num_ifaces < kMaxIfaces) {
        const char* n = de->d_name;
        if (strncmp(n, "wlan", 4) != 0 && strncmp(n, "p2p", 3) != 0 &&
            strncmp(n, "ap", 2) != 0)
            continue;
        MtIface* iface = &hal->ifaces[hal->num_ifaces];
        strlcpy(iface->name, n, sizeof(iface->name));
        hal->handles[hal->num_ifaces] = reinterpret_cast<wifi_interface_handle>(iface);
        hal->num_ifaces++;
    }
    closedir(dir);
}

wifi_error mtInitialize(wifi_handle* handle) {
    if (!handle) return WIFI_ERROR_INVALID_ARGS;
    MtHal* hal = new MtHal();
    if (pipe2(hal->wake_fds, O_CLOEXEC) != 0) {
        delete hal;
        return WIFI_ERROR_UNKNOWN;
    }
    discoverIfaces(hal);
    *handle = reinterpret_cast<wifi_handle>(hal);
    return WIFI_SUCCESS;
}

void mtEventLoop(wifi_handle handle) {
    MtHal* hal = reinterpret_cast<MtHal*>(handle);
    if (!hal) return;
    char c;
    while (!hal->exit_requested) {
        ssize_t r = read(hal->wake_fds[0], &c, 1);
        if (r < 0 && errno != EINTR) break;
    }
    wifi_cleaned_up_handler cb = hal->cleaned_up_handler;
    close(hal->wake_fds[0]);
    close(hal->wake_fds[1]);
    if (cb) cb(handle);  // only compares the handle value
    delete hal;
}

void mtCleanup(wifi_handle handle, wifi_cleaned_up_handler handler) {
    MtHal* hal = reinterpret_cast<MtHal*>(handle);
    if (!hal) return;
    hal->cleaned_up_handler = handler;
    hal->exit_requested = true;
    char c = 'x';
    (void)!write(hal->wake_fds[1], &c, 1);
}

wifi_error mtGetIfaces(wifi_handle handle, int* num, wifi_interface_handle** ifaces) {
    MtHal* hal = reinterpret_cast<MtHal*>(handle);
    if (!hal || !num || !ifaces) return WIFI_ERROR_INVALID_ARGS;
    if (hal->num_ifaces == 0) discoverIfaces(hal);  // driver may have been slow
    *num = hal->num_ifaces;
    *ifaces = hal->handles;
    return WIFI_SUCCESS;
}

wifi_error mtGetIfaceName(wifi_interface_handle iface, char* name, size_t size) {
    if (!iface || !name || size == 0) return WIFI_ERROR_INVALID_ARGS;
    strlcpy(name, reinterpret_cast<MtIface*>(iface)->name, size);
    return WIFI_SUCCESS;
}

wifi_error mtGetSupportedFeatureSet(wifi_interface_handle /* iface */, feature_set* set) {
    if (!set) return WIFI_ERROR_INVALID_ARGS;
    // 2.4 GHz station, Wi-Fi Direct and soft AP through the nl80211 driver.
    *set = WIFI_FEATURE_INFRA | WIFI_FEATURE_P2P | WIFI_FEATURE_SOFT_AP;
    return WIFI_SUCCESS;
}

}  // namespace

wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn* fn) {
    if (!fn) return WIFI_ERROR_UNKNOWN;
    fn->wifi_initialize = mtInitialize;
    fn->wifi_cleanup = mtCleanup;
    fn->wifi_event_loop = mtEventLoop;
    fn->wifi_get_ifaces = mtGetIfaces;
    fn->wifi_get_iface_name = mtGetIfaceName;
    fn->wifi_get_supported_feature_set = mtGetSupportedFeatureSet;
    return WIFI_SUCCESS;
}
