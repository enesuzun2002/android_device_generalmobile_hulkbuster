# kernel 3.10 unlzma needs a small dictionary (it mallocs min(dict,size) for
# initramfs); xz's lzma-alone output (EOS marker) is handled by the kernel.
RECOVERY_RAMDISK_COMPRESSOR := xz --format=lzma --lzma1=preset=9e,dict=16MiB -c

# Recovery image with MTK headers (KERNEL / RECOVERY) via mtkbootimg.
# mtkbootimg names the ramdisk RECOVERY when the output file is called
# recovery.img.
# The stock rules are skipped when BOARD_CUSTOM_BOOTIMG_MK is set, but the
# recovery ramdisk still depends on boot.img, so it needs a rule too.
$(INSTALLED_BOOTIMAGE_TARGET): $(BOARD_CUSTOM_MKBOOTIMG) $(INTERNAL_BOOTIMAGE_FILES)
	$(call pretty,"Target boot image: $@")
	$(hide) $(BOARD_CUSTOM_MKBOOTIMG) $(INTERNAL_BOOTIMAGE_ARGS) $(BOARD_MKBOOTIMG_ARGS) --output $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE),raw)

# The recovery partition is 10 MiB. With f2fs tools the image no longer fit,
# so pack a trimmed copy of the recovery root: sload.f2fs (not needed to
# format or check /data) and the ubsan runtime (nothing in the ramdisk links
# against it) are dropped. Same mkbootfs | compressor pipeline as the stock
# recovery ramdisk.
HULK_RECOVERY_TRIM := sbin/sload.f2fs sbin/libclang_rt.ubsan_standalone-arm-android.so
hulk_trimmed_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery-trimmed.img

$(hulk_trimmed_ramdisk): $(recovery_ramdisk) $(MKBOOTFS)
	$(hide) rm -rf $(PRODUCT_OUT)/recovery-trim && mkdir -p $(PRODUCT_OUT)/recovery-trim
	$(hide) cp -a $(TARGET_RECOVERY_ROOT_OUT) $(PRODUCT_OUT)/recovery-trim/root
	$(hide) cd $(PRODUCT_OUT)/recovery-trim/root && rm -f $(HULK_RECOVERY_TRIM)
	$(hide) $(MKBOOTFS) -d $(TARGET_OUT) $(PRODUCT_OUT)/recovery-trim/root | $(RECOVERY_RAMDISK_COMPRESSOR) > $@

$(INSTALLED_RECOVERYIMAGE_TARGET): $(BOARD_CUSTOM_MKBOOTIMG) \
		$(hulk_trimmed_ramdisk) \
		$(recovery_kernel)
	@echo "----- Making recovery image ------"${CL_RST}
	$(hide) $(BOARD_CUSTOM_MKBOOTIMG) $(filter-out --ramdisk $(recovery_ramdisk),$(INTERNAL_RECOVERYIMAGE_ARGS)) --ramdisk $(hulk_trimmed_ramdisk) $(BOARD_MKBOOTIMG_ARGS) --output $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE),raw)
	@echo "Made recovery image: $@"${CL_RST}
