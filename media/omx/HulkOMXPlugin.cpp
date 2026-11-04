/*
 * OMX plugin (libstagefrighthw.so) exposing the MT6592 hardware H.264 decoder
 * and encoder. OMXMaster in the omx service dlopens this library and calls
 * createOMXPlugin(). The components are compiled into this library; they are
 * the usual SoftOMXComponent subclasses, driven through their OMX_COMPONENTTYPE.
 */
#define LOG_TAG "HulkOMXPlugin"
#include <utils/Log.h>

#include <media/hardware/OMXPluginBase.h>
#include <media/stagefright/omx/SoftOMXComponent.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include <string.h>

#include "../hulkavcdec/SoftHulkAVCDec.h"
#include "../hulkavcenc/SoftHulkAVCEnc.h"

// Defined (at global scope) next to the component classes.
android::SoftOMXComponent *createHulkAVCDecoder(const char *, const OMX_CALLBACKTYPE *, OMX_PTR,
                                                OMX_COMPONENTTYPE **);
android::SoftOMXComponent *createHulkAVCEncoder(const char *, const OMX_CALLBACKTYPE *, OMX_PTR,
                                                OMX_COMPONENTTYPE **);

namespace android {

namespace {

typedef SoftOMXComponent *(*CreateFunc)(const char *, const OMX_CALLBACKTYPE *, OMX_PTR,
                                        OMX_COMPONENTTYPE **);

const struct {
    const char *name;
    const char *role;
    CreateFunc create;
} kComponents[] = {
    { "OMX.hulk.h264.decoder", "video_decoder.avc", ::createHulkAVCDecoder },
    { "OMX.hulk.h264.encoder", "video_encoder.avc", ::createHulkAVCEncoder },
};
const size_t kNumComponents = sizeof(kComponents) / sizeof(kComponents[0]);

// The decoder must invalidate frames still being converted as soon as a
// command is issued, before the command is queued. Wrap the component's
// SendCommand entry point instead of overriding the (protected) virtual.
OMX_ERRORTYPE (*gDecSendCommand)(OMX_HANDLETYPE, OMX_COMMANDTYPE, OMX_U32, OMX_PTR);

OMX_ERRORTYPE decSendCommand(OMX_HANDLETYPE h, OMX_COMMANDTYPE cmd, OMX_U32 param, OMX_PTR data) {
    OMX_COMPONENTTYPE *c = (OMX_COMPONENTTYPE *)h;
    static_cast<SoftHulkAVC *>(static_cast<SoftOMXComponent *>(c->pComponentPrivate))
            ->noteCommand();
    return gDecSendCommand(h, cmd, param, data);
}

struct HulkOMXPlugin : public OMXPluginBase {
    OMX_ERRORTYPE makeComponentInstance(const char *name, const OMX_CALLBACKTYPE *callbacks,
                                        OMX_PTR appData, OMX_COMPONENTTYPE **component) override {
        for (size_t i = 0; i < kNumComponents; ++i) {
            if (strcmp(name, kComponents[i].name)) continue;
            sp<SoftOMXComponent> codec = kComponents[i].create(name, callbacks, appData, component);
            if (codec == NULL) return OMX_ErrorInsufficientResources;
            OMX_ERRORTYPE err = codec->initCheck();
            if (err != OMX_ErrorNone) return err;
            if (i == 0) {
                gDecSendCommand = (*component)->SendCommand;
                (*component)->SendCommand = decSendCommand;
            }
            codec->incStrong(this);  // released in destroyComponentInstance
            return OMX_ErrorNone;
        }
        return OMX_ErrorInvalidComponentName;
    }

    OMX_ERRORTYPE destroyComponentInstance(OMX_COMPONENTTYPE *component) override {
        SoftOMXComponent *me = (SoftOMXComponent *)component->pComponentPrivate;
        me->prepareForDestruction();
        me->decStrong(this);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE enumerateComponents(OMX_STRING name, size_t size, OMX_U32 index) override {
        if (index >= kNumComponents) return OMX_ErrorNoMore;
        strlcpy(name, kComponents[index].name, size);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE getRolesOfComponent(const char *name, Vector<String8> *roles) override {
        for (size_t i = 0; i < kNumComponents; ++i) {
            if (strcmp(name, kComponents[i].name)) continue;
            roles->clear();
            roles->push(String8(kComponents[i].role));
            return OMX_ErrorNone;
        }
        return OMX_ErrorInvalidComponentName;
    }
};

}  // namespace
}  // namespace android

// OMXMaster looks up "createOMXPlugin", then falls back to the C++ symbol
// android::createOMXPlugin() (the header declares the global-namespace form,
// which cannot be given C linkage here).
namespace android {
OMXPluginBase *createOMXPlugin() {
    return new HulkOMXPlugin();
}
}  // namespace android
