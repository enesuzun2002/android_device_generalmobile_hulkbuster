# MT6592 (E-Tab5 / hulkbuster) undervolt + mild overclock: analysis and plan
Read-only analysis. Kernel root K = /home/emurat/Projects/android/e-tab-5/los-16.0/kernel/mediatek/mt6592
(NB: mt_ptp.c / mt_freqhopping.c contain non-UTF8 bytes: use `grep -a`, plain grep silently prints nothing.)

## 0. Facts that drive everything
- CPU DVFS, PTP, GPU "DVFS" and SoC/VCORE DVFS are all in K/arch/arm/mach-mt6592/mt_cpufreq.c (13k lines). mt_gpufreq.c is NOT built (Makefile line 72 commented out).
- Defconfig: CONFIG_MTK_MT6333_SUPPORT=y, IS_VCORE_USE_6333VCORE=y  => MT_DVFS_LOW_VOLTAGE_SUPPORT is defined (7 OPPs down to 0.90 V, PTP floor 0x20=0.90 V).
- External buck present (dmesg fan53555_vosel) => VPROC is the FAN53555 over I2C, NOT the PMIC-wrap path. Voltage actually applied = mt_cpufreq_pmic_volt[idx] (units 10 uV, 90000 = 0.90 V) -> ext_buck_vosel() -> fan53555_vosel(): reg = 0x80 | ((V_mV-600)/10), 10 mV steps, 0.60..1.23 V (K/drivers/misc/mediatek/power/mt6592/fan53555.c:257).
- The OP() table 'cpufreq_volt' column (1150, 1090...) is informational on the ext-buck path; the initial real values are set in mt_cpufreq_pdrv_probe (mt_cpufreq.c ~6340-6480), then overwritten by PTP if PTP runs.
- Chip bin ("ptp_level") comes from efuse devinfo[15] bits[30:28] (mt_ptp.c:199 PTP_get_ptp_level): 1 => 2.0G, 2/4 => 1.3G, 7 => 1.5G, else 0 => 1.7G (1.664). Your chip: level 0 (cpuinfo_max 1664000).

## 1. CPU OPP table (level 0, fan53555, LV support) and what PTP does
mt6592_freqs_fan53555_e1 (mt_cpufreq.c:560) + real voltages from probe (line ~6346):
| idx | freq kHz | stock Vproc (real, probe) |
| 0 | 1664000 | 1.15 V (115000) |
| 1 | 1495000 | 1.09 V |
| 2 | 1365000 | 1.06 V |
| 3 | 1248000 | 1.03 V |
| 4 | 1144000 | 1.00 V |
| 5 |  845000 | 0.95 V |
| 6 |  728000 | 0.90 V |
(Level-1 / MT6592T table mt6592_freqs_fan53555_e1_1, line 589: 2002000@1.15, 1664000@1.10, 1495000@1.09, 1365000@1.06, 1144000@1.00, 845000@0.95, 728000@0.90 -- no 1248000 step. ptp_level 1 => g_max_freq_by_ptp = DVFS_F0_1 = 2002000.)
=> VERIFIED: the 2.0 GHz part is the same die/driver; MTK validates 2.0 GHz at 1.15 V on the 2.0G-bin, and the 1.7G bin only at 1.15 V for 1.664. So +6..8% at 1.15 V is MTK-sanctioned headroom territory (see turbo below); 2.0 GHz at 1.15 V is NOT validated on your bin.
- MTK's own corner tables (cpufreq_fftt_test, mt_cpufreq.c:7878+), level 0 ext-buck: FF = 0.98/0.93/0.92/0.91/0.90/0.90/0.90 V, TT = the stock table above. So a fast-silicon floor is ~0.98 V at 1.664 GHz; a typical chip will NOT run there. Realistic undervolt for a TT part: 25-75 mV at the top OPPs, less/none below 1.0 V (0.90 V is the PTP/table floor).
- PTP (mt_ptp.c): efuse words devinfo 7/8/9/14 -> val_0..3 (line 1258). If val_0 bit0 == 0 PTP never starts (ptp_probe returns; freq_x stay 0, array size 0) and the static table above is used (ptp_offset then does nothing). If enabled: INIT1 (forces Vproc 1.15 V), INIT2, then monitor mode; HW returns per-OPP codes (700 mV + n*6.25 mV), clamped [0x20=0.90 V, 0x54=1.225 V], + mt_ptp_offset, mapped to 10 mV fan53555 steps rounding UP (mt_cpufreq_ext_buck_vproc_mapping). PTP is already closed-loop on silicon speed + temperature + aging (DVTFIXED/MTDES/AGEM), so the margin left is small: expect <= 20-25 mV extra (offset -2..-4). PTPOD_DOWNGRADE_FREQ also drops the top OPP at high temp when PTP is active.
- Cannot know from source alone whether PTP is enabled on this unit: check /proc/ptp/ptp_debug and ptp_dump (below).

## 2. GPU (Mali-450 MP4) frequency/voltage and sharing
- Not a real DVFS table: the Mali driver (K/drivers/misc/mediatek/gpu/mt6592/mali/mali/linux/mali_osk_pm.c:g3d_dvfs_enable) requests SOC_DVFS_TYPE_GPU_HP = 1 when utilization >= 80% and CPI under threshold, else 0. mt_soc_dvfs() (mt_cpufreq.c ~2600) then does:
  - HP: MMPLL 2782 MHz VCO (=700 MHz; efuse devinfo[3]>>30 == 2 -> 2405 = 601 MHz; ==3 -> 500) AND raises VCORE to 1.125 V (0x44), or 1.075 V (0x3C) if SoC-AVS efuse (devinfo[15] bit0) is set.
  - low: MMPLL 1796 MHz (=500 MHz) and VCORE 1.00 V (0x30). Also 250 MHz (1014) used for thermal limiting.
  - GPU_DVFS_Fx list: 700/600/500/250 MHz (mt_cpufreq.c:228).
- If the DRAM is LPDDR3 without DFS (mt_socfreq_ddr_detection), SoC DVFS is disabled and GPU is pinned at 700 MHz, VCORE pinned at 1.125/1.075 V from boot (set by preloader).
- GPU voltage is VCORE from the MT6333 buck (regs 0x6B/0x6C, 6.25 mV/step, 700 mV + n*6.25), SHARED with DRAM controller/EMI/MEMPLL (333 vs 266 MHz tied to the same 1.125/1.0 V states), VENCPLL/MM, display. There is NO separate GPU rail. DFS_phase_mt6333_config_interface BUG_ON()s if vcore code <= 0x10.
- GPU PLL: mt_dfs_mmpll() (mt_freqhopping.c:1779) only accepts 4 hard-coded VCO values (MMPLL_TARGETVCO_1..4 = 0x1AC000/0x130000/0x09C000/0x172000, i.e. 2782/2405/1796/1014); anything else FH_BUG_ON + return -1. MMPLL = 26 MHz * (DDS>>14) / 4: N=107 -> 695.5 (labelled 700). Adding e.g. N=108/109/110 (2808/2834/2860 VCO ~ 702/708/715 MHz) needs a kernel change. 700 MHz is MTK's max for MT6592 (no "T" GPU bump exists in the code; spare0 efuse only down-bins to 600).

## 3. Headroom estimates
CPU overclock:
- ARMPLL table in mt_freqhopping.c:126 holds 2002000, 1664000, 1495000, 1365000, 1248000, 1144000 and turbo steps 1690000, 1456000, 1716000, 1768000, 2054000, 2106000. ARMPLL = 26 MHz * N (DDS 0x100000 = N 64 = 1664). So the PLL/FHCTL path is proven to 2.1 GHz by MTK's own table; any N*26 just needs a DDS entry (1768=68, 1794=69, 1820=70, 1846=71, 1872=72 ... DDS = N<<14).
- Turbo mode already exists (CPUFREQ_HIGHEST_TURBO_MODE, mt_cpufreq.c:3053,5080-5170): top OPP + efuse bonus (26..182 MHz) when only 1-2 or 3-4 CPUs are online (5+ online: none), at the OPP0 voltage. Runtime knob can override. Only 26/52/104 MHz offsets map to a table entry (1690/1716/1768); other offsets hit the default: of mt_fh_hal_dfs_armpll => "BUGON" printk, PLL unchanged (cpufreq thinks it changed). So test offsets 26, 52, 104 only.
- Plausible: 1.77 GHz (+104) at 1.15 V with <=4 cores online is the MTK-design-intent turbo; 1.8-1.9 GHz probably needs ~1.18-1.20 V; 2.0 GHz at 1.15 V on a 1.7G-bin is not validated (try, expect failures on some chips); don't exceed 1.20 V (VMAX 1.225 is the PTP ceiling, buck HW ceiling 1.23 V).
- Undervolt and overclock fight each other on a 1.7G die: realistic combination = undervolt the LOWER OPPs (they are where the stock table has the most margin and where the tablet spends its time) and spend the top OPP voltage on frequency.
Risks: no source for CPU->CCI/bus ratio limits (TOP_CKDIV1_CPU toggled 0x00 above 1 GHz); unknown real silicon limit; thermal (stock config caps OPPs >79 C via cpu_adaptive_0; thermal.conf is obfuscated; mt_cpufreq_thermal_protect(power) picks OPP/ncpu from a power table, test with it ON); battery-low protection (lbat_* knobs) also caps; PTP downgrade; 8 cores at 1.7+ GHz draws far more than a 4-core turbo -> only <=4-core turbo is in-spec.
GPU: +0..5% maybe (MMPLL N=108-109), low reward, requires reflash; undervolting it means undervolting VCORE/DRAM.

## 4. Runtime knobs (all revert on reboot; need root for writes)
Root note: `adb root` hangs adbd (memory note). Use `adb shell` then `su 0 sh -c '...'` if su exists, else adb root and expect a USB replug.
/proc/cpufreq/ (mt_cpufreq.c:12745+)
- cpufreq_debug (r/w): shows g_cpufreq_get_ptp_level, pmic_external_buck_used (confirm =1). write 1 for verbose cpufreq logs.
- cpufreq_state (r/w 0|1): 0 pauses DVFS (mt_cpufreq_pause) -> freq/volt freeze.
- cpufreq_ptpod_freq_volt (READ ONLY): prints "default freq = , volt = " of the active table. Not a voltage knob.
- cpufreq_fftt_test (WRITE 0/1/2): loads FF/TT/SS corner tables into mt_cpufreq_pmic_volt[] (no immediate apply; takes effect at the next OPP voltage set). 0 = FF (0.98..0.90 V) is too aggressive; use only 1 (=TT stock) to restore. Compiled because MT_DVFS_FFTT_TEST is defined (line 71).
- cpufreq_ptpod_test (write 0/1): sets PMIC-WRAP values; NOT for the ext-buck path -> do not use.
- cpufreq_highest_turbo_mode (write "on_off f4core f2core", on_off 0 = enabled, kHz): the runtime overclock knob. e.g. `echo "0 104000 104000"`. Takes effect on the next top-OPP set. Read stock values: dmesg | grep turbo_mode.
- cpufreq_limited_max_freq_by_user (w kHz), cpufreq_limited_power (w power budget, same call the thermal manager uses; `cat` shows g_limited_max_freq/ncpu), cpufreq_over_max_cpu, cpufreq_downgrade_freq_enable / _info / _step_idx, cpufreq_ptpod_temperature_* (PTP downgrade tuning), cpufreq_lbat_* (low-battery caps), cpufreq_total_limited_{cpu,gpu}_power (r).
- socfreq_state (r/w 0/1: SoC DVFS pause), socfreq_enable_hp (w 5 = GPU_HP on [700 MHz + 1.125/1.075 V], 6 = off; 1/2 VENC, 11/12 return-default), socfreq_fixed_test ("0|1|2|3 state enable": pin voltage/MMPLL/VENCPLL/MEMPLL), socfreq_voltage (r: MT6333 reg 0x6C raw; V = 0.7 + code*0.00625), socfreq_dump_info, socfreq_gpu_power_dump (r), socfreq_debug.
/proc/ptp/ (mt_ptp.c:1869+)
- ptp_debug (r: "PTP enabled/disabled (ptp_level=..)"), ptp_dump (r: val_0..val_3 efuse words; val_0 bit0 = PTP init enable), ptp_status (r: current PTP voltages as 700mV+n*6.25 and per-OPP kHz; temperature first), ptp_cur_volt (r: Vproc now), ptp_log_en (w 1 enables 2 s logging to dmesg; DO NOT write 0 before 1: kthread_stop(NULL) oops), ptp_offset (r/w int, units of 6.25 mV PMIC codes added to every OPP, clamped 0x20..0x54; applied immediately; only effective if PTP running; note: ptp_offset_write has a non-__user prototype, works on this 3.10/ARM32 anyway).
/proc/freqhopping/dvfs : write "p1 p2 p3": p1 0 = ARMPLL, p3 = target kHz, ONLY from the 12-value list; p1 4 = MMPLL, p3 = VCO value from the 4 list. Voltage is NOT touched. (freqhopping_debug, dumpregs also exist.)
/proc/pm_init/{cpu_speed_dump,emi_speed_dump,bus_speed_dump,mmclk_speed_dump,mfgclk_speed_dump}: frequency-meter readbacks in MHz/kHz of REAL clocks. Use cpu_speed_dump to confirm turbo/overclock really happened (scaling_cur_freq lies: it reports g_cur_freq without the turbo offset).
/sys/devices/platform/fan53555-user/fan53555_access (r/w): raw FAN53555 I2C. `echo 01` then `cat` reads reg 0x01 (VSEL1) as decimal; `echo "01 a8"` writes VSEL1 = 0x80|n: V = 0.60 + 0.01*n (0x9e=0.90, 0xa5=0.97, 0xa8=1.00, 0xb2=1.10, 0xb7=1.15, 0xbf=1.23 MAX). Gets overwritten at the next OPP change, so combine with a locked OPP (userspace governor, scaling_setspeed) -- this is the ONLY arbitrary-voltage knob without a rebuild. fan53555_hw_init reprograms VSEL1 to 1.1 V at probe; reboot restores table.
/sys/devices/platform/mt6333-user?/mt6333_access (name per mt6333.c:9681): raw MT6333 regs incl. VCORE (0x6B/0x6C). DO NOT use: that is the DRAM/SoC rail.
/sys/devices/system/cpu/cpu0/cpufreq/{scaling_governor,scaling_max_freq,scaling_setspeed,scaling_available_freqs,stats/time_in_state}, /sys/devices/system/cpu/cpufreq/hotplug/* (up_threshold 80, cpu_num_base, cpu_input_boost_num 4 set by init.mt6592.rc) -- cap online cores with `echo 0 > /sys/devices/system/cpu/cpuN/online` for turbo tests.
Missing: no runtime knob for arbitrary per-OPP CPU voltage (only ptp_offset when PTP runs, fftt_test corners, raw fan53555_access). Suggested minimal kernel patch (stage b0, still reboot-reverting): add /proc/cpufreq/cpufreq_vproc_offset (int mV applied to mt_cpufreq_pmic_volt[0..6] with clamp 0.90-1.20 V) -- cheap and makes stage (a) a lot safer.

## 5. Staged plan
### Stage A (runtime only; nothing persists)
Prereqs: charged >60 %, on charger-free battery or stable charger, `adb shell` as non-root where possible, screen on, phone at room temp, panic-to-recovery hook per the pie-boot-debug memory in place, TWRP boot image ready.
A0 Baseline (no writes):
  cat /proc/cpufreq/cpufreq_debug            # ptp_level=0, ext buck=1
  cat /proc/ptp/ptp_debug /proc/ptp/ptp_dump /proc/ptp/ptp_status /proc/ptp/ptp_cur_volt
  cat /proc/cpufreq/cpufreq_ptpod_freq_volt /proc/cpufreq/cpufreq_limited_power /proc/cpufreq/socfreq_voltage /proc/cpufreq/socfreq_dump_info
  dmesg | grep -iE "turbo|ptp|soc-avs|DFS_Detection|LPDDR|fan53555"      # turbo efuse, DRAM type, AVS
  cat /proc/pm_init/cpu_speed_dump; cat /sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state
A1 Stress/validation harness (put in /data/local/tmp/stress.sh; sha256 of fixed zero buffer is deterministic -> any silent corruption = fail; crash = fail):
  #!/system/bin/sh
  REF=$(dd if=/dev/zero bs=1M count=32 2>/dev/null | sha256sum | cut -d' ' -f1)
  N=${1:-4}; T=${2:-600}; END=$(( $(date +%s) + T ))
  worker(){ while [ $(date +%s) -lt $END ]; do H=$(dd if=/dev/zero bs=1M count=32 2>/dev/null | sha256sum | cut -d' ' -f1); [ "$H" = "$REF" ] || echo "MISMATCH worker $1 $(date +%s)" >> /data/local/tmp/stress.err; done; }
  i=0; while [ $i -lt $N ]; do worker $i & i=$((i+1)); done; wait
  Run `sh stress.sh 4 600` (4 workers = 4 online cores) and `sh stress.sh 8 600`. Pass = no stress.err, no reboot, `dmesg | grep -iE "oops|Unable to handle|BUG|WDT|ptp error|I2C return fail"` empty, temps (cat /proc/mtktscpu/mtktscpu or thermal zone) stay < 75 C. Also run a second pass of a different mix (memcpy heavy: `dd if=/dev/zero of=/dev/null bs=4M count=4000`) and a real workload (Geekbench/GL app) since logic-vs-memory failures differ.
  (Without `sha256sum` on the ROM use md5sum/sha1sum; both exist in toybox.)
A2 CPU undervolt, PTP running (offset knob): lock one OPP first for a clean read: scaling_governor=userspace, echo 1664000 > scaling_setspeed. Then offsets in 6.25 mV codes (maps to 10 mV, rounds up so use even steps): `echo -2 > /proc/ptp/ptp_offset` (~ -12/-10 mV), run harness 10 min; then -4, -6; hard stop at -8 (~50 mV) -- that equals about the whole FF/TT gap on PTP-trimmed parts. After each step: cat ptp_status and ptp_cur_volt to see the real result. Failure => `echo 0 > /proc/ptp/ptp_offset` (or reboot).
A2' CPU undervolt, PTP disabled (val_0&1==0): use fan53555_access at a locked OPP:
  OPP0 1664 MHz stock 1.15 V: try 1.12 (0xb4), 1.10 (0xb2), 1.08 (0xb0), stop at 1.05 (0xad). Same for idx1 1.09 -> 1.06, idx2 1.06 -> 1.03, idx3 1.03 -> 1.00, idx4 1.00 -> 0.97 (floor 0.95), idx5 0.95/idx6 0.90: do NOT go lower than 0.90. 10 mV steps, 10 min harness per step, then +20 mV guard-band on whatever passes (undervolt ONLY as far as pass-minus-20 mV; temperature/battery/aging).
  Verify by readback: echo 01 > fan53555_access; cat fan53555_access (value-128)*10+600 mV.
  Note any OPP change (governor) overwrites it, hence lock freq first via userspace governor; set the other OPPs by locking each in turn.
A3 CPU overclock at stock voltage (turbo knob; no extra voltage; <=4 cores online):
  for c in 4 5 6 7; do echo 0 > /sys/devices/system/cpu/cpu$c/online; done
  echo performance > .../scaling_governor  (hotplug governor will bring cores up; use userspace governor+ setspeed 1664000 and keep 4 cores)
  echo "0 26000 26000" > /proc/cpufreq/cpufreq_highest_turbo_mode   # 1690 MHz
  cat /proc/pm_init/cpu_speed_dump  # must read ~1690000/1690
  stress 4x10 min, then "0 52000 52000" (1716), "0 104000 104000" (1768). Do NOT try other offsets (unmapped -> PLL stays, state desync). 
  If 1768 is stable at 1.15 V with margin: try +10/+20 mV (fan53555_access 0xb8/0xb9) for 1820+ only after adding DDS entries (stage B).
  Revert: echo "1 0 0" > cpufreq_highest_turbo_mode, or reboot.
A4 GPU: only observation + undervolt-by-proxy is NOT recommended. For stability of the stock 700 MHz run a GL benchmark with `echo 5 > /proc/cpufreq/socfreq_enable_hp`, watch `dmesg | grep -iE "mali|GP|PP|job timeout"` and `cat /proc/pm_init/mfgclk_speed_dump`. Reboot restores.
A5 Always after a test: reboot; confirm cat ptp_offset = 0 and cpufreq_ptpod_freq_volt unchanged.

### Stage B (persist; needs rebuild; only after A passes a 30+ min mixed run at the target settings and 24 h daily-use soak with the runtime values re-applied from a boot script)
Easiest safe persistence = a boot script (init.mt6592.rc `on property:sys.boot_completed=1` writes to /proc/ptp/ptp_offset and /proc/cpufreq/cpufreq_highest_turbo_mode) -- no kernel rebuild and a bad value is a one-line fix. (Edit device/generalmobile/hulkbuster/rootdir/init.mt6592.rc ~line 399, or a vendor init; permissions: add `chmod 0664`/ run as root.)
If a kernel change is wanted:
 1. Static voltages (PTP disabled case): K/arch/arm/mach-mt6592/mt_cpufreq.c mt_cpufreq_pdrv_probe ext-buck block, `is_fan53555_exist()==1`, level 0 lines ~6346-6352: mt_cpufreq_pmic_volt[0..6] = 115000,109000,106000,103000,100000,95000,90000 -> the tested values minus nothing (they already contain your +20 mV guard). Keep the same numbers in the fftt TT block (~8300) and in mt6592_freqs_fan53555_e1 OP() macros / DVFS_Vn_fan53555 (line 213-222) for consistency. Units for pmic_volt: 10 uV (1.12 V = 112000).
 2. PTP-enabled case: leave tables alone; set `static int mt_ptp_offset = -N;` in K/arch/arm/mach-mt6592/mt_ptp.c:130 (N from stage A, 6.25 mV units). Optional: raise nothing else.
 3. Overclock: preferred = fixed turbo: in mt_cpufreq_turbo_mode_check_efuse() (mt_cpufreq.c:3053) force g_cpufreq_turbo_mode_efuse_on_off=0, _4_core=104000 (1768 MHz, in FH table), _2_core=104000 -- OR to go beyond: add rows to the ARMPLL switch in K/arch/arm/mach-mt6592/mt_freqhopping.c:1706 (new ARMPLL_TARGETVCO_13 1820000 = 0x00118000 [N=70], _14 1872000 = 0x00120000 [N=72]; DDS = (N<<14)) and the #defines at line 126-151, then use offsets 156000/208000 over 1664. A no-VCO-code alternative: `#define MT_OFFICIAL_TURBO` (mt_cpufreq.c:72) forces ptp_level=1 => 2.002 GHz top OPP with the level-1 table (1.15 V). Use only if stage A proves 2.0 GHz at <=1.20 V with 8 cores stable; then raise DVFS_V0 fan53555 level-1 row at line ~6373 (115000 -> up to 120000). Remember ARMPLL DDS for 1.8 GHz must also exist in that table for any non-listed frequency; adding the new CPU OPP row to mt6592_freqs_fan53555_e1 also changes freq/ptp divisor math (freq/12000) -- keep OPP count <= 8.
 4. GPU (optional, low value): add MMPLL_TARGETVCO_5 2808000 (0x001B0000) to mt_fh_hal_dfs_mmpll() (mt_freqhopping.c:1779/190-194) and replace 2782000 calls in mt_cpufreq.c (the 700 MHz lines ~2790, 2380, 1560-1580); keep VCORE untouched.
 5. Optional but valuable: add /proc/cpufreq/cpufreq_vproc_offset knob (see sec 4) before baking anything.
Defconfig: nothing needed (CONFIG_MTK_PTP etc. are unconditional; just arch/arm/configs/hulkbuster_defconfig stays; CONFIG_CPU_FREQ_GOV_HOTPLUG=y default governor).
Rollback: keep known-good boot.img (/home/emurat/Projects/android/e-tab-5/boot.img), TWRP image twrp-known-good-recovery.img.

## 6. Red flags (do not do)
1. Never lower VCORE (MT6333 0x6B/0x6C, socfreq_* states) -- it is the DRAM/EMI/MEMPLL/display rail and shared with GPU (1.125/1.075 V @700 MHz, 1.0 V @500 MHz). DRAM corruption shows up as random filesystem/app corruption, not a clean crash. DFS_phase_mt6333_config_interface BUG_ON()s below code 0x10 and LPDDR3-no-DFS parts are pinned at the high state by the preloader.
2. Do not write fan53555_access below 0.90 V or above 1.20 V; do not leave it set across suspend/resume without verifying (deep idle uses mt_cpufreq_spm_volt/ptp entries).
3. Don't write ptp_log_en 0 first (NULL kthread_stop). Don't use cpufreq_ptpod_test (PMIC-WRAP values on an ext-buck board). Don't write turbo offsets other than 26/52/104 MHz. Don't write fftt_test 0 (FF corner).
4. Don't test with the thermal manager off for long runs (thermal.off.conf exists); keep protections. Sustained 8-core load above 1.5 GHz at 1.15 V: watch skin/case temp.
5. Silent-instability risk > crash risk: undervolting yields rare corruption; always keep +20 mV guard and run mixed workloads; revisit in summer/hot battery.
6. Voltage is bin-specific: values from another unit/forum (including the FF table) are unsafe; only stage-A results on this unit count.
