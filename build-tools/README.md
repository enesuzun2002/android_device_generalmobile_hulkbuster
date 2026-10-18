Post-build verification gates used to validate the hulkbuster ROM.

Run after `mka bacon` against out/target/product/hulkbuster/:

    python3 check_boot_ramdisk.py    # ramdisk content: mtkdiag, /dev/ion 0666, fstab, adb.secure
    python3 verify_build.py          # OTA sanity + diff vs last flashed build (old.list baseline)
    python3 verify_ota.py            # blob integrity vs proprietary/
    python3 check_deps.py            # every DT_NEEDED resolves against the installed /system
    python3 check_syms.py            # graphics closure symbol check

Paths inside are workspace-absolute (/workspace/los-14.1, /tmp/opencode) —
adjust OLD_LIST / ZIP paths for a new environment. The diff-vs-previous
baseline file (old.list) is generated with:

    unzip -Z1 out/target/product/hulkbuster/<zip>.zip > old.list
