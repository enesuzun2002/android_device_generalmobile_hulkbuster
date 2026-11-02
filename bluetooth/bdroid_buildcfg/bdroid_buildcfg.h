/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef _BDROID_BUILDCFG_H
#define _BDROID_BUILDCFG_H

#define BTM_DEF_LOCAL_NAME   "General Mobile E-Tab5"

#define BTA_DISABLE_DELAY 1000 /* in milliseconds */

/*
 * The MT6592 combo BT firmware does not implement the vendor "LE Get Vendor
 * Capabilities" command (0xFD53): it answers with Command Status 0x01
 * (Unknown HCI Command) and never a Command Complete, so the controller
 * module start-up hangs and Bluetooth hits the enable timeout. Needs
 * build-tools/patches/system-bt/0001-*.patch.
 */
#define BTM_BLE_SKIP_OFFLOAD_FEATURES_QUERY

#endif
