/*
 * Lollipop-era libgui ABI used by libgui_ext.so (and through it by
 * hwcomposer.mt6592.so).
 */
#include <unistd.h>
#include <gui/BufferQueue.h>
#include <ui/Fence.h>

using namespace android;

static_assert(sizeof(Fence) == 8,
              "Fence layout changed: [atomic<int32_t> mCount][unique_fd mFenceFd] expected");

extern "C" {

/* L: BufferQueue::createBufferQueue(sp<IGBP>*, sp<IGBC>*,
 *                                   const sp<IGraphicBufferAlloc>& = NULL)
 * Pie dropped IGraphicBufferAlloc; the allocator argument is ignored. */
void _ZN7android11BufferQueue17createBufferQueueEPNS_2spINS_22IGraphicBufferProducerEEEPNS1_INS_22IGraphicBufferConsumerEEERKNS1_INS_19IGraphicBufferAllocEEE(
        sp<IGraphicBufferProducer>* outProducer,
        sp<IGraphicBufferConsumer>* outConsumer,
        const void* /* allocator */)
{
    BufferQueue::createBufferQueue(outProducer, outConsumer, false);
}

/* L exported Fence::~Fence(); in Pie it is private and inline (= default).
 * The blob calls it from its inlined sp<Fence>/decStrong, then frees the
 * memory itself, so all this has to do is close the fd at offset 4. */
void _ZN7android5FenceD1Ev(void* self)
{
    int* fd = reinterpret_cast<int*>(static_cast<char*>(self) + 4);
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}

}
