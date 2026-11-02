/*
 * Bluetooth Vendor Library for MTK's libbluetoothdrv blob (MT6592 / MT6630)
 *
 * The blob's mtk_bt_enable() opens /dev/stpbt, loads the NVRAM BD address and
 * downloads/initialises the combo-chip firmware, returning the fd that carries
 * H4 traffic.  mtk_bt_disable(fd) de-initialises the chip and close()s the fd.
 * Everything else the HIDL HAL asks for is a no-op or a plain callback.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "libbt_vendor_mtk"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <utils/Log.h>

#include <bt_vendor_lib.h>

/* The HAL only uses this to time LPM wake de-assertion; the controller has no
 * wake line, so the value is irrelevant.  Same value as the old blob. */
#define LPM_IDLE_TIMEOUT_MS 200

typedef int (*mtk_enable_fn)(int flag, void *func_cb);
typedef int (*mtk_disable_fn)(int bt_fd);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void *mtklib_handle = NULL;
static mtk_enable_fn mtk_bt_enable = NULL;
static mtk_disable_fn mtk_bt_disable = NULL;
static const bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
static int hci_tty_fd = -1; /* fd returned by mtk_bt_enable, -1 when closed */

/* Callback helpers: tolerate a missing callback table / short table. */
#define HAVE_CB(member)                                          \
  (bt_vendor_cbacks != NULL &&                                   \
   bt_vendor_cbacks->size >=                                     \
       offsetof(bt_vendor_callbacks_t, member) + sizeof(void *) && \
   bt_vendor_cbacks->member != NULL)

/* Must be called with g_lock held.  Closes the stpbt fd if one is open; always
 * leaves hci_tty_fd == -1.  Returns 0 on success. */
static int release_fd_locked(void) {
  int fd = hci_tty_fd;
  int ret = 0;

  if (fd < 0)
    return 0;
  hci_tty_fd = -1;

  if (mtk_bt_disable != NULL) {
    ret = mtk_bt_disable(fd);
    if (ret == 0)
      return 0;
    /* The blob refuses (returns -1) when its internal library handle is gone
     * (e.g. it was already disabled or its state was reset).  The kernel
     * device is exclusive-open, so never leave the fd behind. */
    ALOGW("mtk_bt_disable(%d) returned %d, closing fd directly", fd, ret);
  }
  if (close(fd) != 0 && errno != EBADF)
    ALOGW("close(%d) failed: %s", fd, strerror(errno));
  return ret ? ret : -1;
}

static int mtk_init(const bt_vendor_callbacks_t *p_cb,
                    unsigned char *local_bdaddr) {
  (void)local_bdaddr; /* the blob reads the address from NVRAM itself */

  ALOGI("libbt-vendor init for MTK blob");

  if (p_cb == NULL) {
    ALOGE("init failed with no user callbacks!");
    return -1;
  }

  pthread_mutex_lock(&g_lock);
  bt_vendor_cbacks = p_cb;

  /* Recover from an earlier session that was never closed. */
  release_fd_locked();

  if (mtklib_handle == NULL) {
    mtklib_handle = dlopen("libbluetoothdrv.so", RTLD_LAZY);
    if (mtklib_handle == NULL) {
      ALOGE("Failed to open libbluetoothdrv library: %s", dlerror());
      goto fail;
    }
  }

  mtk_bt_enable = (mtk_enable_fn)dlsym(mtklib_handle, "mtk_bt_enable");
  mtk_bt_disable = (mtk_disable_fn)dlsym(mtklib_handle, "mtk_bt_disable");
  if (mtk_bt_enable == NULL || mtk_bt_disable == NULL) {
    ALOGE("Errors while opening symbols from mtk blob: %s", dlerror());
    goto fail;
  }

  pthread_mutex_unlock(&g_lock);
  return 0;

fail:
  if (mtklib_handle != NULL) {
    dlclose(mtklib_handle);
    mtklib_handle = NULL;
  }
  mtk_bt_enable = NULL;
  mtk_bt_disable = NULL;
  bt_vendor_cbacks = NULL;
  pthread_mutex_unlock(&g_lock);
  return -1;
}

static void mtk_cleanup(void) {
  ALOGI("vendor cleanup");

  pthread_mutex_lock(&g_lock);
  /* The HAL normally issued USERIAL_CLOSE already; if not (HAL shutting down
   * abnormally) do not leave /dev/stpbt open. */
  release_fd_locked();

  bt_vendor_cbacks = NULL;
  mtk_bt_enable = NULL;
  mtk_bt_disable = NULL;
  if (mtklib_handle != NULL) {
    dlclose(mtklib_handle);
    mtklib_handle = NULL;
  }
  pthread_mutex_unlock(&g_lock);
}

/* BT_VND_OP_USERIAL_OPEN: returns number of fds filled in (1 = all channels on
 * one H4 fd), or -1 on failure. */
/* Called with g_lock held. */
static int mtk_open(int *fd_array) {
  int fd, idx;

  if (fd_array == NULL || mtk_bt_enable == NULL) {
    ALOGE("USERIAL_OPEN before successful init");
    return -1;
  }

  /* A stale fd would make the exclusive-open /dev/stpbt fail below with
   * "Can't open serial port". */
  if (hci_tty_fd >= 0) {
    ALOGW("USERIAL_OPEN with fd %d still open, closing it first", hci_tty_fd);
    release_fd_locked();
  }

  fd = mtk_bt_enable(0, NULL);
  if (fd < 0) {
    ALOGE("Can't open mtk fd");
    return -1;
  }
  hci_tty_fd = fd;

  for (idx = 0; idx < CH_MAX; idx++)
    fd_array[idx] = fd;
  return 1; /* CMD/EVT/ACL/SCO on the same fd */
}

static int mtk_op(bt_vendor_opcode_t opcode, void *param) {
  int ret = 0;

  /* g_lock is NOT held across the switch: the HAL re-enters op() from inside
   * fwcfg_cb.  It only guards the fd/handle state below. */
  switch (opcode) {
    case BT_VND_OP_POWER_CTRL:
      /* Chip power is handled inside mtk_bt_enable()/mtk_bt_disable().  On
       * power-off make sure nothing is left open. */
      if (param != NULL && *(int *)param == BT_VND_PWR_OFF) {
        pthread_mutex_lock(&g_lock);
        release_fd_locked();
        pthread_mutex_unlock(&g_lock);
      }
      break;

    case BT_VND_OP_FW_CFG:
      /* Firmware was already downloaded by mtk_bt_enable(). */
      if (HAVE_CB(fwcfg_cb))
        bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
      break;

    case BT_VND_OP_SCO_CFG:
      /* SCO is routed over the combo chip's internal PCM/I2S, nothing to do. */
      if (HAVE_CB(scocfg_cb))
        bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS);
      break;

    case BT_VND_OP_USERIAL_OPEN:
      pthread_mutex_lock(&g_lock);
      ret = mtk_open((int *)param);
      pthread_mutex_unlock(&g_lock);
      break;

    case BT_VND_OP_USERIAL_CLOSE:
      pthread_mutex_lock(&g_lock);
      release_fd_locked();
      pthread_mutex_unlock(&g_lock);
      break;

    case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
      if (param != NULL)
        *(uint32_t *)param = LPM_IDLE_TIMEOUT_MS;
      break;

    case BT_VND_OP_LPM_SET_MODE:
      /* No host wake-up line on this controller: LPM is a no-op. */
      if (HAVE_CB(lpm_cb))
        bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS);
      break;

    case BT_VND_OP_LPM_WAKE_SET_STATE:
      break;

    case BT_VND_OP_SET_AUDIO_STATE:
      if (HAVE_CB(audio_state_cb))
        bt_vendor_cbacks->audio_state_cb(BT_VND_OP_RESULT_SUCCESS);
      break;

    case BT_VND_OP_EPILOG:
      if (HAVE_CB(epilog_cb))
        bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
      break;

    case BT_VND_OP_A2DP_OFFLOAD_START:
    case BT_VND_OP_A2DP_OFFLOAD_STOP:
      /* No a2dp offload on this controller. */
      if (HAVE_CB(a2dp_offload_cb))
        bt_vendor_cbacks->a2dp_offload_cb(
            BT_VND_OP_RESULT_FAIL, opcode,
            param != NULL
                ? ((bt_vendor_op_a2dp_offload_t *)param)->bta_av_handle
                : 0);
      ret = -1;
      break;

    default:
      ALOGW("Unknown opcode: %d", opcode);
      break;
  }

  return ret;
}

const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    .size = sizeof(bt_vendor_interface_t),
    .init = mtk_init,
    .op = mtk_op,
    .cleanup = mtk_cleanup,
};
