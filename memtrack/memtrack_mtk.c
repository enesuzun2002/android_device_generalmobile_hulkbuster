/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Memtrack HAL for the MT6592 (Mali-450 + MTK ion).
 *
 * Pie does not mount debugfs, so the per-process numbers come from two
 * procfs nodes added to the kernel (root:system 0440):
 *
 *   /proc/mali/pid_memory  "pid mali_bytes dma_buf_bytes ump_bytes ext_bytes"
 *   /proc/ion_pids         "pid total_bytes pss_bytes"
 *
 * Both have one line per session/client, so lines of the same pid are summed.
 *   GL       -> Mali OS/block memory (not part of any smaps mapping)
 *   GRAPHICS -> ion memory, buffers shared between clients split evenly
 * Every other type is unsupported.
 */

#define LOG_TAG "memtrack_mtk"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <hardware/memtrack.h>
#include <log/log.h>

#define MALI_PID_MEMORY "/proc/mali/pid_memory"
#define ION_PIDS        "/proc/ion_pids"

/*
 * Sum column 'col' (1-based, column 0 is the pid) of every line of 'path'
 * whose pid matches. Returns 0 or -errno.
 */
static int sum_pid_column(const char *path, pid_t pid, int col,
                          unsigned long long *sum)
{
    char line[256];
    FILE *fp = fopen(path, "re");

    *sum = 0;
    if (fp == NULL) {
        int err = errno;
        ALOGE("open %s: %s", path, strerror(err));
        return -err;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long long v[4] = { 0, 0, 0, 0 };
        int line_pid;
        int n = sscanf(line, "%d %llu %llu %llu %llu", &line_pid,
                       &v[0], &v[1], &v[2], &v[3]);

        if (n < 1 + col || line_pid != pid)
            continue;
        *sum += v[col - 1];
    }

    fclose(fp);
    return 0;
}

static int fill_record(const char *path, pid_t pid, int col, uint32_t flags,
                       struct memtrack_record *records, size_t *num_records)
{
    unsigned long long bytes;
    int ret;

    /* size query: we always report exactly one record */
    if (*num_records == 0) {
        *num_records = 1;
        return 0;
    }

    ret = sum_pid_column(path, pid, col, &bytes);
    if (ret < 0)
        return ret;

    records[0].size_in_bytes = (size_t)bytes;
    records[0].flags = flags;
    *num_records = 1;
    return 0;
}

static int mtk_memtrack_init(const struct memtrack_module *module)
{
    (void)module;
    return 0;
}

static int mtk_memtrack_get_memory(const struct memtrack_module *module,
                                   pid_t pid, int type,
                                   struct memtrack_record *records,
                                   size_t *num_records)
{
    (void)module;

    switch (type) {
    case MEMTRACK_TYPE_GL:
        return fill_record(MALI_PID_MEMORY, pid, 1,
                           MEMTRACK_FLAG_SMAPS_UNACCOUNTED |
                           MEMTRACK_FLAG_PRIVATE |
                           MEMTRACK_FLAG_NONSECURE,
                           records, num_records);
    case MEMTRACK_TYPE_GRAPHICS:
        return fill_record(ION_PIDS, pid, 2,
                           MEMTRACK_FLAG_SMAPS_UNACCOUNTED |
                           MEMTRACK_FLAG_SHARED |
                           MEMTRACK_FLAG_SYSTEM |
                           MEMTRACK_FLAG_NONSECURE,
                           records, num_records);
    default:
        return -ENODEV;
    }
}

static struct hw_module_methods_t memtrack_module_methods = {
    .open = NULL,
};

struct memtrack_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = MEMTRACK_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = MEMTRACK_HARDWARE_MODULE_ID,
        .name = "MT6592 Memory Tracker HAL",
        .author = "The Android Open Source Project",
        .methods = &memtrack_module_methods,
    },

    .init = mtk_memtrack_init,
    .getMemory = mtk_memtrack_get_memory,
};
