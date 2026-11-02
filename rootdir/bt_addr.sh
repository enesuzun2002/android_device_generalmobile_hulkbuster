#!/system/bin/sh
#
# Write the Bluetooth address for Pie's bluetooth@1.0 HAL (ro.bt.bdaddr_path).
# The HAL aborts with "No Bluetooth Address!" without one. The MTK
# libbluetoothdrv blob programs the controller from NVRAM itself, so use the
# same address: the first 6 bytes of the NVRAM BT_Addr record. Fall back to a
# random locally administered address that stays stable across boots.
#
OUT=/data/misc/bluetooth/bdaddr
NV=/data/nvram/APCFG/APRDEB/BT_Addr

hex6() { od -An -tx1 -N6 "$1" 2>/dev/null | tr -s ' ' | sed 's/^ //;s/ $//;s/ /:/g' | tr a-f A-F; }

addr=""
[ -r "$NV" ] && addr=$(hex6 "$NV")
case "$addr" in
    ??:??:??:??:??:??) ;;
    *) addr="" ;;
esac
case "$addr" in 00:00:00:00:00:00|FF:FF:FF:FF:FF:FF) addr="" ;; esac

if [ -z "$addr" ]; then
    [ -s "$OUT" ] && exit 0
    addr="02:$(od -An -tx1 -N5 /dev/urandom | tr -s ' ' | sed 's/^ //;s/ $//;s/ /:/g' | tr a-f A-F)"
fi

mkdir -p /data/misc/bluetooth
printf '%s' "$addr" > "$OUT.tmp" && mv "$OUT.tmp" "$OUT"
chown bluetooth:bluetooth "$OUT"
chmod 0644 "$OUT"
