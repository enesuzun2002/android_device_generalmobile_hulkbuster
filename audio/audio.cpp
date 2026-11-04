#define LOG_TAG "audio_hw_hulkbuster"
#include <log/log.h>
#include <system/audio.h>
#include <utils/StrongPointer.h>
#include <hardware/audio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <math.h>
#include <cutils/properties.h>

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

	/*
	 * The MTK volume controller programs the analog output gain (headphone
	 * buffer feeding the speaker amp) only inside setMasterVolume, for the
	 * device routed at that moment. AudioFlinger sets master volume once at
	 * load, before any routing (devices 0x0), and MTK's AudioPolicy that
	 * re-applied it on every route change is not there, so the speaker gain
	 * was never programmed. Re-send master volume after each routing change.
	 */
	static struct audio_hw_device *g_adev = NULL;

	/*
	 * Speaker digital boost. With the analog buffer already at its +9 dB
	 * maximum (kernel hp_gain_floor) the speaker is still quiet, so scale the
	 * PCM going to the speaker by persist.vendor.audio.spk_boost_mb (millibel,
	 * e.g. 200 = +2 dB; +6 dB clipped audibly) with saturation. Only while
	 * routed to the speaker. Off by default: even +2 dB distorted on loud
	 * content.
	 */
	static volatile int g_spk_routed = 1;
	static int32_t g_boost_q16 = 65536;
	static ssize_t (*orig_out_write)(struct audio_stream_out *, const void *, size_t) = NULL;
	static void *g_boost_buf = NULL;
	static size_t g_boost_len = 0;

	static void update_boost(void)
	{
		int mb = property_get_int32("persist.vendor.audio.spk_boost_mb", 0);
		if (mb < 0) mb = 0;
		if (mb > 1200) mb = 1200;
		g_boost_q16 = (int32_t)(65536.0 * pow(10.0, mb / 2000.0));
		ALOGI("speaker boost %d mB (q16 %d)", mb, g_boost_q16);
	}

	static ssize_t hulk_out_write(struct audio_stream_out *out, const void *buf, size_t bytes)
	{
		if (!g_spk_routed || g_boost_q16 <= 65536 || bytes == 0)
			return orig_out_write(out, buf, bytes);
		if (g_boost_len < bytes) {
			void *n = realloc(g_boost_buf, bytes);
			if (n == NULL)
				return orig_out_write(out, buf, bytes);
			g_boost_buf = n;
			g_boost_len = bytes;
		}
		audio_format_t fmt = out->common.get_format(&out->common);
		const int64_t g = g_boost_q16;
		if (fmt == AUDIO_FORMAT_PCM_16_BIT) {
			const int16_t *src = (const int16_t *)buf;
			int16_t *dst = (int16_t *)g_boost_buf;
			for (size_t i = 0; i < bytes / 2; i++) {
				int64_t v = ((int64_t)src[i] * g) >> 16;
				dst[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
			}
		} else if (fmt == AUDIO_FORMAT_PCM_32_BIT || fmt == AUDIO_FORMAT_PCM_8_24_BIT) {
			const int32_t lim = fmt == AUDIO_FORMAT_PCM_32_BIT ? INT32_MAX : 0x7fffff;
			const int32_t *src = (const int32_t *)buf;
			int32_t *dst = (int32_t *)g_boost_buf;
			for (size_t i = 0; i < bytes / 4; i++) {
				int64_t v = ((int64_t)src[i] * g) >> 16;
				dst[i] = v > lim ? lim : (v < -(int64_t)lim - 1 ? -lim - 1 : (int32_t)v);
			}
		} else {
			return orig_out_write(out, buf, bytes);
		}
		return orig_out_write(out, g_boost_buf, bytes);
	}
	static int (*orig_out_set_parameters)(struct audio_stream *, const char *) = NULL;

	static int hulk_out_set_parameters(struct audio_stream *stream, const char *kv)
	{
		int ret = orig_out_set_parameters(stream, kv);
		const char *r = kv != NULL ? strstr(kv, "routing=") : NULL;
		if (r != NULL) {
			int dev = atoi(r + 8);
			g_spk_routed = (dev & AUDIO_DEVICE_OUT_SPEAKER) != 0 &&
				(dev & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) == 0;
			update_boost();
		}
		if (kv != NULL && strstr(kv, "routing=") != NULL && g_adev != NULL &&
		    g_adev->set_master_volume != NULL) {
			float v = 1.0f;
			if (g_adev->get_master_volume == NULL ||
			    g_adev->get_master_volume(g_adev, &v) != 0 || v <= 0.0f)
				v = 1.0f;
			int m = g_adev->set_master_volume(g_adev, v);
			ALOGI("%s -> master volume %.2f reapplied (%d)", kv, v, m);
		}
		return ret;
	}

	static int hulk_open_output_stream(struct audio_hw_device *dev,
			audio_io_handle_t handle, audio_devices_t devices,
			audio_output_flags_t flags, struct audio_config *config,
			struct audio_stream_out **stream_out, const char *address)
	{
		int ret = orig_open_output_stream(dev, handle, devices, flags, config,
				stream_out, address);
		if (ret == 0) {
			apply_speaker_enhancement(dev);
			struct audio_stream_out *out = *stream_out;
			if (out != NULL && out->common.set_parameters != NULL &&
			    out->common.set_parameters != hulk_out_set_parameters) {
				orig_out_set_parameters = out->common.set_parameters;
				out->common.set_parameters = hulk_out_set_parameters;
			}
			if (out != NULL && out->write != NULL && out->write != hulk_out_write &&
			    (flags & AUDIO_OUTPUT_FLAG_PRIMARY)) {
				orig_out_write = out->write;
				out->write = hulk_out_write;
				update_boost();
			}
		}
		return ret;
	}

	/*
	 * Input streams. The MTK blob allocates struct legacy_stream_in as
	 * { struct audio_stream_in stream; AudioStreamIn *legacy_in; } with the
	 * pre-Android-6 audio_stream_in, so the framework's get_capture_position
	 * slot (added to the struct later) overlaps legacy_in. The audio HAL
	 * client calls it whenever it is non-NULL and jumped into the heap object
	 * on the first capture read (SIGSEGV in ReadThread), killing the audio HAL
	 * on any recording. Hand out a full-size zeroed audio_stream_in that
	 * forwards to the blob's stream instead.
	 */
	struct hulk_in {
		struct audio_stream_in stream;	/* must stay first */
		struct audio_stream_in *orig;
	};
	static int (*orig_open_input_stream)(struct audio_hw_device *, audio_io_handle_t,
			audio_devices_t, struct audio_config *, struct audio_stream_in **,
			audio_input_flags_t, const char *, audio_source_t) = NULL;
	static void (*orig_close_input_stream)(struct audio_hw_device *, struct audio_stream_in *) = NULL;

	#define HIN(s) (((struct hulk_in *)(s))->orig)
	static uint32_t hin_get_sample_rate(const struct audio_stream *s) { return HIN(s)->common.get_sample_rate(&HIN(s)->common); }
	static int hin_set_sample_rate(struct audio_stream *s, uint32_t r) { return HIN(s)->common.set_sample_rate(&HIN(s)->common, r); }
	static size_t hin_get_buffer_size(const struct audio_stream *s) { return HIN(s)->common.get_buffer_size(&HIN(s)->common); }
	static audio_channel_mask_t hin_get_channels(const struct audio_stream *s) { return HIN(s)->common.get_channels(&HIN(s)->common); }
	static audio_format_t hin_get_format(const struct audio_stream *s) { return HIN(s)->common.get_format(&HIN(s)->common); }
	static int hin_set_format(struct audio_stream *s, audio_format_t f) { return HIN(s)->common.set_format(&HIN(s)->common, f); }
	static int hin_standby(struct audio_stream *s) { return HIN(s)->common.standby(&HIN(s)->common); }
	static int hin_dump(const struct audio_stream *s, int fd) { return HIN(s)->common.dump(&HIN(s)->common, fd); }
	static audio_devices_t hin_get_device(const struct audio_stream *s) { return HIN(s)->common.get_device(&HIN(s)->common); }
	static int hin_set_device(struct audio_stream *s, audio_devices_t d) { return HIN(s)->common.set_device(&HIN(s)->common, d); }
	static int hin_set_parameters(struct audio_stream *s, const char *kv) { return HIN(s)->common.set_parameters(&HIN(s)->common, kv); }
	static char *hin_get_parameters(const struct audio_stream *s, const char *keys) { return HIN(s)->common.get_parameters(&HIN(s)->common, keys); }
	static int hin_add_audio_effect(const struct audio_stream *s, effect_handle_t e) { return HIN(s)->common.add_audio_effect(&HIN(s)->common, e); }
	static int hin_remove_audio_effect(const struct audio_stream *s, effect_handle_t e) { return HIN(s)->common.remove_audio_effect(&HIN(s)->common, e); }
	static int hin_set_gain(struct audio_stream_in *s, float g) { return HIN(s)->set_gain(HIN(s), g); }
	static ssize_t hin_read(struct audio_stream_in *s, void *buf, size_t bytes) { return HIN(s)->read(HIN(s), buf, bytes); }
	static uint32_t hin_get_input_frames_lost(struct audio_stream_in *s) { return HIN(s)->get_input_frames_lost(HIN(s)); }

	static int hulk_open_input_stream(struct audio_hw_device *dev,
			audio_io_handle_t handle, audio_devices_t devices,
			struct audio_config *config, struct audio_stream_in **stream_in,
			audio_input_flags_t flags, const char *address, audio_source_t source)
	{
		struct audio_stream_in *orig = NULL;
		int ret = orig_open_input_stream(dev, handle, devices, config, &orig,
				flags, address, source);
		if (ret != 0 || orig == NULL) {
			*stream_in = orig;
			return ret;
		}
		struct hulk_in *w = (struct hulk_in *)calloc(1, sizeof(*w));
		if (w == NULL) {
			orig_close_input_stream(dev, orig);
			return -ENOMEM;
		}
		w->orig = orig;
		w->stream.common.get_sample_rate = hin_get_sample_rate;
		w->stream.common.set_sample_rate = hin_set_sample_rate;
		w->stream.common.get_buffer_size = hin_get_buffer_size;
		w->stream.common.get_channels = hin_get_channels;
		w->stream.common.get_format = hin_get_format;
		w->stream.common.set_format = hin_set_format;
		w->stream.common.standby = hin_standby;
		w->stream.common.dump = hin_dump;
		w->stream.common.get_device = hin_get_device;
		w->stream.common.set_device = hin_set_device;
		w->stream.common.set_parameters = hin_set_parameters;
		w->stream.common.get_parameters = hin_get_parameters;
		w->stream.common.add_audio_effect = hin_add_audio_effect;
		w->stream.common.remove_audio_effect = hin_remove_audio_effect;
		w->stream.set_gain = hin_set_gain;
		w->stream.read = hin_read;
		w->stream.get_input_frames_lost = hin_get_input_frames_lost;
		*stream_in = &w->stream;
		return 0;
	}

	static void hulk_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *stream)
	{
		if (stream == NULL)
			return;
		struct hulk_in *w = (struct hulk_in *)stream;
		orig_close_input_stream(dev, w->orig);
		free(w);
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
			g_adev = adev;
			if (adev->open_output_stream != NULL) {
				orig_open_output_stream = adev->open_output_stream;
				adev->open_output_stream = hulk_open_output_stream;
			}
			if (adev->open_input_stream != NULL && adev->close_input_stream != NULL) {
				orig_open_input_stream = adev->open_input_stream;
				orig_close_input_stream = adev->close_input_stream;
				adev->open_input_stream = hulk_open_input_stream;
				adev->close_input_stream = hulk_close_input_stream;
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
