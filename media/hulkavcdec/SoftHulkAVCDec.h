/*
 * H.264 decoder OMX component backed by the MediaTek MT6592 hardware decoder
 * (libvcodecdrv.so), loaded at runtime.
 */
#ifndef SOFT_HULK_AVC_DEC_H_
#define SOFT_HULK_AVC_DEC_H_

#include <media/stagefright/omx/SoftVideoDecoderOMXComponent.h>

#include <stdio.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace android {

struct SoftHulkAVC : public SoftVideoDecoderOMXComponent {
    SoftHulkAVC(const char *name, const OMX_CALLBACKTYPE *callbacks,
                OMX_PTR appData, OMX_COMPONENTTYPE **component);

    // Called by the plugin's SendCommand wrapper before every OMX command.
    void noteCommand();

protected:
    virtual ~SoftHulkAVC();

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onReset();
    virtual int getColorAspectPreference();
    virtual OMX_ERRORTYPE getExtensionIndex(const char *name, OMX_INDEXTYPE *index);
    virtual OMX_ERRORTYPE internalGetParameter(OMX_INDEXTYPE index, OMX_PTR params);
    virtual OMX_ERRORTYPE internalSetParameter(OMX_INDEXTYPE index, const OMX_PTR params);

private:
    enum { kNumBuffers = 8, kNumOutBuffers = 16, kNumOutBuffersAnwMin = 6 };
    static const size_t kMaxJobs = 4;

    // ---- driver-visible structures (see vdectest for the protocol) ----
    struct ValMem {
        uint32_t type, size;
        void *va;
        uint32_t mva, align;
        void *alignVa;
        uint32_t alignMva, codec, r32, r36;
        void *m4u;
        uint32_t r44;
    };
    struct Bs { uint32_t va, mva, size, rd, wr, r20, orgva, r28; };
    struct Fb { uint32_t w[32]; };

    struct FbEntry {
        Fb fb;
        ValMem mem;
        bool allocated;
        bool pending;   // displayed by driver, not yet converted
        bool released;  // driver no longer references it
    };

    struct PendingFrame {
        int idx;
        int64_t ts;
        int w, h, stride, sliceH;
    };

    struct Job {
        int idx;
        bool anw;   // hdr->pBuffer is a gralloc native_handle_t, convert with MDP
        OMX_BUFFERHEADERTYPE *hdr;
        int w, h, stride, sliceH;
        int outW, outH;
        uint32_t gen;
    };

    enum Result { kConsume, kKeep, kStop, kStopConsumed };

    struct NalScan {
        bool hasSlice = false, hasIdr = false, hasSps = false, hasPps = false;
        bool spsOk = false;
        int w = 0, h = 0, codedW = 0, codedH = 0;
    };

    // driver handle
    void *mH;
    bool mDecInit;
    int mDecW, mDecH, mCodedW, mCodedH;
    ValMem mBs;
    bool mBsAlloc;
    std::vector<FbEntry *> mFbs;
    uint32_t mFbSize;
    std::vector<int> mFree;
    std::mutex mFbLock;

    std::vector<uint8_t> mSps, mPps;
    int mSpsW, mSpsH, mSpsCodedW, mSpsCodedH;
    std::multiset<int64_t> mTs;
    int64_t mLastTs;
    std::deque<PendingFrame> mPending;
    std::vector<uint8_t> mAuBuf;

    bool mReceivedEOS, mEosFlushed, mSignalledError;
    bool mNeedReinit;
    int mNewW, mNewH;
    unsigned mFramesOut, mDecodeErrors;

    // detile worker
    std::thread mWorker;
    std::mutex mWMutex;
    std::condition_variable mWCv;
    std::deque<Job> mJobs;
    bool mWBusy;
    bool mWExit;
    std::atomic<uint32_t> mGen;
    bool mANW;                    // output port uses native-window (gralloc YV12) buffers
    void *mMdp;                   // DpBlitStream instance, owned by the worker thread
    unsigned mMdpFails;           // consecutive MDP failures
    bool mMdpDisabled;
    FILE *mDumpFile;
    int mDumpLeft;
    FILE *mDumpIn;
    unsigned mInCount, mDecCount;

    bool loadDriver();
    NalScan scanNals(const uint8_t *d, size_t n);
    bool createDecoder(const uint8_t *au, size_t len);
    void destroyDecoder();
    bool allocPool(uint32_t size, int count);
    void freePool();
    bool decodeAu(const uint8_t *au, size_t len);
    void flushDriver();
    void drain();
    void resetState();
    Result processInput(OMX_BUFFERHEADERTYPE *hdr);
    bool deliverFrame(List<BufferInfo *> &outQ);
    void waitWorkerIdle();
    void workerLoop();
    bool convertToGralloc(const Job &j);
    bool convertToGrallocCpu(const Job &j);
    int findFb(const void *p);

    DISALLOW_EVIL_CONSTRUCTORS(SoftHulkAVC);
};

}  // namespace android

#endif  // SOFT_HULK_AVC_DEC_H_
