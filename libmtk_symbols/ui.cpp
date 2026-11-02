#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

using android::GraphicBufferMapper;
using android::Rect;

/* L: status_t GraphicBufferMapper::lock(buffer_handle_t, int usage,
 *                                        const Rect&, void**)
 * Pie takes uint32_t usage. This is a member function, so the old symbol
 * receives `this` first; forward it (an earlier version declared it as a free
 * function without `this`, shifting every argument). */
extern "C" int
_ZN7android19GraphicBufferMapper4lockEPK13native_handleiRKNS_4RectEPPv(
        GraphicBufferMapper* self, buffer_handle_t handle, int usage,
        const Rect& bounds, void** vaddr) {
    return self->lock(handle, static_cast<uint32_t>(usage), bounds, vaddr);
}
