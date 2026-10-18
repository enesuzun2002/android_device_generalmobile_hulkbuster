#!/system/bin/sh
#
# Boot diagnostics for the hulkuster/hulkbuster "stuck on logo" investigation.
#
# The device gives us no UART and (so far) no adb, so when a boot fails there
# is nothing to inspect. This script leaves a record on persistent storage so
# the state at the moment of failure can be read back afterwards (from TWRP or
# from a booting ROM).
#
# Usage:
#   mtkdiag.sh snapshot   one-shot dump of props/mounts/logcat
#   mtkdiag.sh pump       mirror the kernel ring buffer to disk until init dies
#   mtkdiag.sh watch      repeated late-boot dumps, tombstones included
#
DIR=/data/mtkdiag
if ! mkdir -p "$DIR" 2>/dev/null; then
    DIR=/cache/mtkdiag
    mkdir -p "$DIR" 2>/dev/null
fi

case "$1" in
snapshot)
    getprop                        > "$DIR/getprop.txt"        2>&1
    cat /proc/cmdline              > "$DIR/cmdline.txt"       2>&1
    cat /proc/mounts               > "$DIR/mounts.txt"        2>&1
    cat /proc/uptime               > "$DIR/uptime.txt"        2>&1
    cat /proc/partitions           > "$DIR/partitions.txt"    2>&1
    cat /proc/filesystems          > "$DIR/filesystems.txt"   2>&1
    ls -l /dev/block/              > "$DIR/blocks.txt"        2>&1
    ls -l /dev/                    > "$DIR/dev.txt"           2>&1
    logcat -b all -d -v threadtime > "$DIR/logcat.txt"        2>&1
    logcat -b crash -d -v threadtime > "$DIR/logcat_crash.txt" 2>&1
    ;;

pump)
    # /proc/kmsg blocks until new records are available, so this keeps
    # mirroring the kernel ring buffer for as long as init is alive. If the
    # hang happens later (SurfaceFlinger, zygote, ...), the tail of the
    # kernel log ends up here.
    #
    # Give up if /proc/kmsg cannot be read a few times in a row (e.g. another
    # process holds the exclusive reader lock) rather than spinning hot.
    miss=0
    while [ "$miss" -lt 5 ]; do
        if cat /proc/kmsg >> "$DIR/kmsg.log" 2>/dev/null; then
            miss=0
        else
            miss=$((miss + 1))
            sleep 2
        fi
    done
    ;;

watch)
    # `snapshot` runs at `on fs`, which is far too early: everything that has
    # gone wrong in this investigation (SurfaceFlinger segfaulting, zygote and
    # system_server dying behind it) happens after that point. logcat's in-memory
    # buffer also wraps, so a single dump cannot be trusted to still contain the
    # first crash.
    #
    # So: dump repeatedly, and keep every tombstone we can see. /data is
    # 27.5 GiB here, the files are small, and having the whole series is what
    # makes a crash-loop readable.
    #
    # logd comes up with the `main` class, which may be after this service
    # starts, so wait for its socket before the first dump instead of recording
    # a connect error as if it were the boot state.
    n=0
    while [ "$n" -lt 50 ]; do
        [ -S /dev/socket/logdw ] && break
        n=$((n + 1))
        sleep 1
    done

    mkdir -p "$DIR/tombstones"

    i=0
    while [ "$i" -lt 24 ]; do          # 24 x 10 s = 4 minutes
        n=$(printf "%02d" "$i")
        # A late getprop is the single best proof of which /system is actually
        # installed: /system/build.prop is not loaded yet when `snapshot` runs.
        getprop                            > "$DIR/getprop_late_$n.txt" 2>&1
        logcat -b all -d -v threadtime     > "$DIR/logcat_late_$n.txt" 2>&1
        logcat -b crash -d -v threadtime   > "$DIR/logcat_crash_$n.txt" 2>&1
        dmesg                              > "$DIR/dmesg_late_$n.txt" 2>&1
        ls -l /data/tombstones/            > "$DIR/tombstones_$n.txt" 2>&1
        # tombstoned reuses and eventually prunes these, so copy them out
        # rather than relying on them still being there afterwards.
        cp -a /data/tombstones/. "$DIR/tombstones/" 2>/dev/null
        i=$((i + 1))
        sleep 10
    done
    ;;

*)
    echo "usage: $0 {snapshot|pump|watch}" >&2
    exit 1
    ;;
esac
