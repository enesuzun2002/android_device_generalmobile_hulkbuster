#define LOG_TAG "audio_hw_hulkbuster"
#include <log/log.h>
#include <system/audio.h>
#include <utils/StrongPointer.h>
#include <hardware/audio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

extern "C"
{
	struct legacy_audio_module {
	    struct audio_module module;
	};

	/*
	 * MTK speaker loudness enhancement (MusicPlus / BesLoudness), on in the
	 * stock ROM. The HAL takes its on/off flags from the NVRAM file
	 * Audio_AudEnh_Control_Opt, which does not exist on this install, and
	 * (re)loads them when an output stream is opened, so they came up off
	 * (quiet speaker). Set them after the device and every output stream
	 * open. The HAL handles one key per set_parameters call.
	 */
	static int (*orig_open_output_stream)(struct audio_hw_device *, audio_io_handle_t,
			audio_devices_t, audio_output_flags_t, struct audio_config *,
			struct audio_stream_out **, const char *) = NULL;

	static void apply_speaker_enhancement(struct audio_hw_device *adev)
	{
		if (adev->set_parameters == NULL)
			return;
		int a = adev->set_parameters(adev, "SetMusicPlusStatus=1");
		int b = adev->set_parameters(adev, "SetBesLoudnessStatus=1");
		ALOGI("MusicPlus=1 (%d), BesLoudness=1 (%d)", a, b);
	}

	static int hulk_open_output_stream(struct audio_hw_device *dev,
			audio_io_handle_t handle, audio_devices_t devices,
			audio_output_flags_t flags, struct audio_config *config,
			struct audio_stream_out **stream_out, const char *address)
	{
		int ret = orig_open_output_stream(dev, handle, devices, flags, config,
				stream_out, address);
		if (ret == 0)
			apply_speaker_enhancement(dev);
		return ret;
	}

	static int legacy_adev_open(const hw_module_t *module, const char *name,
		                hw_device_t **device)
	{
		void *hdl;
		struct legacy_audio_module *hmi;

		hdl = dlopen("libaudio.primary.default.so", RTLD_NOW | RTLD_LOCAL);
		if (hdl == NULL) {
			ALOGE("MTK Audio loadLibrary() failed to open %s", dlerror());
			return -1;
		}

		hmi = (struct legacy_audio_module *)dlsym(hdl, HAL_MODULE_INFO_SYM_AS_STR);
		if (hmi == NULL) {
			ALOGE("MTK Audio loadLibrary() could not find symbol %s",HAL_MODULE_INFO_SYM_AS_STR);
			return -1;
		}

		int ret = hmi->module.common.methods->open(module, name, device);
		ALOGI("open(%s) -> %d", name ? name : "(null)", ret);
		if (ret == 0 && *device != NULL && name != NULL &&
		    strcmp(name, AUDIO_HARDWARE_INTERFACE) == 0) {
			struct audio_hw_device *adev = (struct audio_hw_device *)*device;
			if (adev->open_output_stream != NULL) {
				orig_open_output_stream = adev->open_output_stream;
				adev->open_output_stream = hulk_open_output_stream;
			}
			apply_speaker_enhancement(adev);
		}
		return ret;
	}

	static struct hw_module_methods_t legacy_audio_module_methods = {
	open:
		legacy_adev_open
	};

	struct legacy_audio_module HAL_MODULE_INFO_SYM = {
	    module: {
		common: {
		    tag: HARDWARE_MODULE_TAG,
		    module_api_version: AUDIO_MODULE_API_VERSION_0_1,
		    hal_api_version: HARDWARE_HAL_API_VERSION,
		    id: AUDIO_HARDWARE_MODULE_ID,
		    name: "MTK Audio HW HAL",
		    author: "The Android Open Source Project",
		    methods: &legacy_audio_module_methods,
		    dso : NULL,
		    reserved : {0},
		},
	    },
	};
}
