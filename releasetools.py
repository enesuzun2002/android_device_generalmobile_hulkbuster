# Copyright (C) 2026 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""OTA hooks for hulkbuster (MT6592).

The boot image lives behind MTK's dumchar character device /dev/bootimg (it
is not a real eMMC partition). Pie's package_extract_file() goes through
libziparchive, which ftruncate()s every target that is not a block device;
that fails on the character device, so the standard boot write in the
updater-script silently writes nothing. Extract boot.img to /tmp and copy
it with dd instead, which writes through dumchar without truncating.
"""


def FullOTA_InstallEnd(info):
  InstallBoot(info)


def IncrementalOTA_InstallEnd(info):
  InstallBoot(info)


def InstallBoot(info):
  info.script.AppendExtra('package_extract_file("boot.img", "/tmp/boot.img");')
  info.script.AppendExtra(
      'run_program("/sbin/dd", "if=/tmp/boot.img", "of=/dev/bootimg", "bs=65536") == "0" || '
      'abort("E3001: Failed to write boot.img to /dev/bootimg");')
  info.script.AppendExtra('delete("/tmp/boot.img");')
