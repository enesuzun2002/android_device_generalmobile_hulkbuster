/*
 * Smooth CPU thermal control for hulkbuster (MT6592, 8x Cortex-A7).
 *
 * MTK's adaptive thermal manager (cpu_adaptive_0) only acts at 79 C and then
 * drops straight to 728-845 MHz / 6 cores, so sustained load saw-tooths
 * between full speed and a crawl. This thread keeps the die below that trip
 * by stepping scaling_max_freq one OPP at a time:
 *   - above kHotC: one OPP down every kStepMs (never below kMinFreqKhz)
 *   - below kCoolC: one OPP up every kStepMs
 * MTK's 79 C adaptive cooling, the 100 C shutdown and the 117 C reset stay in
 * place underneath as the emergency layers.
 */
#define LOG_TAG "thermal.hulkbuster.gov"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <log/log.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kCpuZoneType = "mtktscpu";
constexpr const char* kMaxFreq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
constexpr const char* kAvailFreqs =
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies";
constexpr int kHotC = 75;
constexpr int kCoolC = 70;
constexpr int kMinFreqKhz = 1144000;
constexpr int kStepMs = 2000;
constexpr int kIdlePollMs = 2000;
constexpr int kWarmPollMs = 500;

std::string findCpuZone() {
    for (int i = 0; i < 32; i++) {
        std::string dir = "/sys/class/thermal/thermal_zone" + std::to_string(i);
        std::string type;
        if (!android::base::ReadFileToString(dir + "/type", &type)) continue;
        if (android::base::Trim(type) == kCpuZoneType) return dir + "/temp";
    }
    return "";
}

bool readInt(const std::string& path, long* out) {
    std::string s;
    if (!android::base::ReadFileToString(path, &s)) return false;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    *out = v;
    return true;
}

std::vector<int> availableFreqs() {
    std::vector<int> freqs;
    std::string s;
    if (!android::base::ReadFileToString(kAvailFreqs, &s)) return freqs;
    for (const std::string& f : android::base::Split(android::base::Trim(s), " ")) {
        if (!f.empty()) freqs.push_back(atoi(f.c_str()));
    }
    std::sort(freqs.begin(), freqs.end());  // ascending
    return freqs;
}

void governorLoop() {
    std::string zone = findCpuZone();
    std::vector<int> freqs = availableFreqs();
    if (zone.empty() || freqs.empty()) {
        ALOGW("thermal governor disabled (zone '%s', %zu freqs)", zone.c_str(), freqs.size());
        return;
    }
    size_t top = freqs.size() - 1;
    size_t floorIdx = 0;
    while (floorIdx < top && freqs[floorIdx] < kMinFreqKhz) floorIdx++;
    size_t cap = top;
    auto lastStep = std::chrono::steady_clock::now();
    ALOGI("thermal governor: %s, cap %d..%d kHz, %d/%d C", zone.c_str(), freqs[floorIdx],
          freqs[top], kCoolC, kHotC);

    for (;;) {
        long milliC;
        int pollMs = kIdlePollMs;
        if (readInt(zone, &milliC)) {
            long c = milliC / 1000;
            auto now = std::chrono::steady_clock::now();
            bool due = now - lastStep >= std::chrono::milliseconds(kStepMs);
            size_t want = cap;
            if (c >= kHotC && cap > floorIdx && due) want = cap - 1;
            else if (c < kCoolC && cap < top && due) want = cap + 1;
            if (want != cap) {
                cap = want;
                lastStep = now;
                if (!android::base::WriteStringToFile(std::to_string(freqs[cap]), kMaxFreq)) {
                    ALOGW("cannot write %s", kMaxFreq);
                } else {
                    ALOGI("die %ld C -> max %d kHz", c, freqs[cap]);
                }
            }
            if (c >= kCoolC || cap != top) pollMs = kWarmPollMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
}

}  // namespace

void startThermalGovernor() {
    std::thread(governorLoop).detach();
}
