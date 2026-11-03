#!/bin/sh
# Apply the hulkbuster lineage-16.0 patches to a LineageOS 16.0 checkout.
# Usage: device/generalmobile/hulkbuster/build-tools/patches/lineage-16.0/apply.sh [<android-root>]
# Safe to re-run: a patch that is already applied is skipped.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=${1:-$(cd "$here/../../../../../.." && pwd)}

for dir in "$here"/*/; do
    # frameworks-native -> frameworks/native, packages-apps-Backgrounds ->
    # packages/apps/Backgrounds: turn '-' into '/' from the left until the
    # project directory exists.
    proj=$(basename "$dir")
    while [ ! -d "$root/$proj" ]; do
        case "$proj" in
            *-*) proj=$(echo "$proj" | sed 's#-#/#') ;;
            *) break ;;
        esac
    done
    for p in "$dir"*.patch; do
        if git -C "$root/$proj" apply --reverse --check "$p" 2>/dev/null; then
            echo "already applied: $proj $(basename "$p")"
        else
            git -C "$root/$proj" apply --index "$p"
            echo "applied:         $proj $(basename "$p")"
        fi
    done
done
