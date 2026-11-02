/*
 * android_atomic_* for the Lollipop-era MTK blobs.
 *
 * Pie's libcutils no longer exports these (cutils/atomic.h is inline-only),
 * so libMali, hwcomposer.mt6592, libgui_ext, the camera stack and others fail
 * to dlopen. Semantics follow the old cutils/atomic.h: inc/dec/add/and/or
 * return the previous value, the cas variants return 0 on success.
 */
#include <stdint.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT int32_t android_atomic_inc(volatile int32_t *addr)
{
    return __atomic_fetch_add(addr, 1, __ATOMIC_SEQ_CST);
}

EXPORT int32_t android_atomic_dec(volatile int32_t *addr)
{
    return __atomic_fetch_sub(addr, 1, __ATOMIC_SEQ_CST);
}

EXPORT int32_t android_atomic_add(int32_t value, volatile int32_t *addr)
{
    return __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST);
}

EXPORT int32_t android_atomic_and(int32_t value, volatile int32_t *addr)
{
    return __atomic_fetch_and(addr, value, __ATOMIC_SEQ_CST);
}

EXPORT int32_t android_atomic_or(int32_t value, volatile int32_t *addr)
{
    return __atomic_fetch_or(addr, value, __ATOMIC_SEQ_CST);
}

EXPORT int32_t android_atomic_acquire_load(volatile const int32_t *addr)
{
    return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
}

EXPORT int32_t android_atomic_release_load(volatile const int32_t *addr)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return __atomic_load_n(addr, __ATOMIC_RELAXED);
}

EXPORT void android_atomic_acquire_store(int32_t value, volatile int32_t *addr)
{
    __atomic_store_n(addr, value, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

EXPORT void android_atomic_release_store(int32_t value, volatile int32_t *addr)
{
    __atomic_store_n(addr, value, __ATOMIC_RELEASE);
}

EXPORT int android_atomic_acquire_cas(int32_t oldvalue, int32_t newvalue,
                                      volatile int32_t *addr)
{
    int32_t expected = oldvalue;
    return !__atomic_compare_exchange_n(addr, &expected, newvalue, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}

EXPORT int android_atomic_release_cas(int32_t oldvalue, int32_t newvalue,
                                      volatile int32_t *addr)
{
    int32_t expected = oldvalue;
    return !__atomic_compare_exchange_n(addr, &expected, newvalue, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

EXPORT int android_atomic_cas(int32_t oldvalue, int32_t newvalue,
                              volatile int32_t *addr)
{
    return android_atomic_release_cas(oldvalue, newvalue, addr);
}

EXPORT void android_memory_barrier(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
