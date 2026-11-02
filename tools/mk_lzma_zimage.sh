#!/bin/bash
# Rebuild only the ARM zImage decompressor stub with LZMA (CONFIG_KERNEL_LZMA)
# around the existing, unmodified kernel Image. Nothing in the LOS KERNEL_OBJ
# is written to.
set -e
KO=/workspace/los-16.0/out/target/product/hulkbuster/obj/KERNEL_OBJ
KS=/workspace/los-16.0/kernel/mediatek/mt6592
OUT=/workspace/twrp-9.0/out/kz
export PATH=/workspace/los-16.0/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin:$PATH
rm -rf $OUT; mkdir -p $OUT
cd $KO
B=arch/arm/boot/compressed
SZ=$(stat -c %s arch/arm/boot/Image)
(xz --format=lzma -9 -c arch/arm/boot/Image; python3 -c "import struct,sys;sys.stdout.buffer.write(struct.pack('<I',$SZ))") > $OUT/piggy.lzma
ls -l $OUT/piggy.lzma
sed -e 's/CONFIG_KERNEL_GZIP/CONFIG_KERNEL_GZIP_UNUSED/' -e 's/^#ifdef CONFIG_KERNEL_LZMA/#if 1/' \
    -e "s#\"../../../../lib/#\"$KS/lib/#" $KS/$B/decompress.c > $OUT/decompress.c
grep -n "unlzma" $OUT/decompress.c
cc_cmd=$(head -1 $B/.decompress.o.cmd | sed 's/^cmd_[^:]*:= //')
cc_cmd=${cc_cmd//-Wp,-MD,arch\/arm\/boot\/compressed\/.decompress.o.d/-Wp,-MD,$OUT/dep.d}
cc_cmd=${cc_cmd%%-c -o *}
eval "$cc_cmd -c -o $OUT/decompress.o $OUT/decompress.c"
sed "s#arch/arm/boot/compressed/piggy.lzma#$OUT/piggy.lzma#" $KS/$B/piggy.lzma.S > $OUT/piggy.lzma.S
as_cmd=$(head -1 $B/.piggy.gzip.o.cmd | sed 's/^cmd_[^:]*:= //')
as_cmd=${as_cmd//-Wp,-MD,arch\/arm\/boot\/compressed\/.piggy.gzip.o.d/-Wp,-MD,$OUT/dep2.d}
as_cmd=${as_cmd%%-c -o *}
eval "$as_cmd -c -o $OUT/piggy.lzma.o $OUT/piggy.lzma.S"
ld_cmd=$(head -1 $B/.vmlinux.cmd | sed 's/^cmd_[^:]*:= //')
ld_cmd=${ld_cmd//$B\/piggy.gzip.o/$OUT/piggy.lzma.o}
ld_cmd=${ld_cmd//$B\/decompress.o/$OUT/decompress.o}
ld_cmd=${ld_cmd//-o $B\/vmlinux/-o $OUT/vmlinux}
echo "$ld_cmd"
eval "$ld_cmd"
arm-linux-androidkernel-objcopy -O binary -R .comment -S $OUT/vmlinux $OUT/zImage
ls -l $OUT/zImage
