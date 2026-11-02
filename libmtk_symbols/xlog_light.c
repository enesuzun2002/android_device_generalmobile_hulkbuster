/*
 * __xlog_buf_printf for the old MTK blobs (libdpframework, the OMX codecs,
 * the camera stack, thermal, mnld, ...). Kept in its own library that only
 * needs liblog, because it is shimmed into libMali's dependency chain and into
 * a lot of other processes.
 */
#include <stdarg.h>
#include <android/log.h>

struct xlog_record {
    const char *tag_str;
    const char *fmt_str;
    int prio;
};

__attribute__((visibility("default")))
int __xlog_buf_printf(int bufid, const struct xlog_record *rec, ...)
{
    va_list args;
    (void)bufid;
    va_start(args, rec);
    __android_log_vprint(rec->prio, rec->tag_str, rec->fmt_str, args);
    va_end(args);
    return 0;
}
