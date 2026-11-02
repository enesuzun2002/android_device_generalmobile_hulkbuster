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

$(INSTALLED_RECOVERYIMAGE_TARGET): $(BOARD_CUSTOM_MKBOOTIMG) \
		$(recovery_ramdisk) \
		$(recovery_kernel)
	@echo "----- Making recovery image ------"${CL_RST}
	$(hide) $(BOARD_CUSTOM_MKBOOTIMG) $(INTERNAL_RECOVERYIMAGE_ARGS) $(BOARD_MKBOOTIMG_ARGS) --output $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE),raw)
	@echo "Made recovery image: $@"${CL_RST}
