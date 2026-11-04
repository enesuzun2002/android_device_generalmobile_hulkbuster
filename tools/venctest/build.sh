#!/bin/sh
# Standalone build with prebuilt clang + tree bionic (no soong). usage: build.sh out_binary
R=/home/emurat/Projects/android/e-tab-5/los-16.0
CL=$R/prebuilts/clang/host/linux-x86/clang-4691093/bin/clang
O=$R/out/target/product/hulkbuster/obj; B=$R/bionic/libc
G=$R/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9
exec $CL --target=armv7a-linux-androideabi -D__ANDROID_API__=27 -nostdinc \
 -isystem $($CL --target=armv7a-linux-androideabi -print-resource-dir)/include \
 -isystem $B/include -isystem $B/kernel/uapi -isystem $B/kernel/android/uapi -isystem $B/kernel/uapi/asm-arm -isystem $B/arch-arm/include \
 -O2 -fPIE -pie -nostdlib -Wall -Wno-unused-parameter $(dirname $0)/venctest.c -o ${1:-venctest} \
 $O/lib/crtbegin_dynamic.o $O/lib/libc.so $O/lib/libdl.so $O/lib/libm.so \
 $G/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a $O/lib/crtend_android.o \
 -Wl,--dynamic-linker=/system/bin/linker -B$G/bin -gcc-toolchain $G
