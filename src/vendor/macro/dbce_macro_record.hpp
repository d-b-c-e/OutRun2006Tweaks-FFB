// Macro recording for unattended tests (docs/MACRO.md): while %LOCALAPPDATA%\dbce\<game>\record.on exists, the fence
// (dbce_fence.hpp, Settings::recorder) passes every standard-format read here, and each device's changes as the game
// receives them (button edges, hat changes, axis moves of 1/64 of the range or more) are appended, timed, to
// macro-<yyyyMMdd-HHmmss>.txt in the same folder. The flag is checked every 250 ms, so recording starts and stops
// while the game runs; each start opens a new file. tools/macro/Play-Macro.ps1 plays a file back through inject.txt.
// Header-only; x86 and x64.
#pragma once
#include <windows.h>
#include <errno.h>
#include <share.h>
#include <stdio.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "../controls/dbce_inject_table.hpp"

namespace dbce { namespace macro {

class Recorder {
public:
    // `game` names the %LOCALAPPDATA%\dbce folder, as for the other test switches.
    explicit Recorder(const char *game)
    {
        char d[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", d, MAX_PATH);
        if (n && n < MAX_PATH) dir_ = std::string(d) + "\\dbce\\" + game;
    }
    ~Recorder() { if (f_) fclose(f_); }
    std::string path() { std::lock_guard<std::mutex> g(m_); return path_; }   // the file being written, or the last one

    // One read of `dev` ("{...}" instance text). `range(axis, min, max)` reads the game's range for an axis.
    template <class RangeFn>
    void sample(const std::string &dev, const controls::StateView &s, uint64_t nowMs, RangeFn range)
    {
        std::lock_guard<std::mutex> g(m_);
        if (dir_.empty()) return;
        if (!checked_ || nowMs - checkedAt_ >= 250) { checked_ = true; checkedAt_ = nowMs; toggle(nowMs); }
        if (!f_) return;
        auto it = devs_.find(dev);
        if (it == devs_.end()) {   // first sight: its state is the baseline, nothing to replay
            Dev d;
            d.name = "d" + std::to_string(devs_.size());
            for (int a = 0; a < 8; ++a) { d.ok[a] = s.axes && range(a, d.mn[a], d.mx[a]) && d.mx[a] > d.mn[a]; d.axes[a] = s.axes ? s.axes[a] : 0; }
            for (int b = 0; b < s.buttonCount && b < 128; ++b) d.buttons[b] = (s.buttons[b] & 0x80) != 0;
            for (int p = 0; p < 4 && s.povs; ++p) d.povs[p] = pov(s.povs[p]);
            fprintf(f_, "dev %s %s\n", d.name.c_str(), dev.c_str());
            fflush(f_);
            devs_.emplace(dev, d);
            return;
        }
        Dev &d = it->second;
        const double t = (double)(nowMs - start_) / 1000.0;
        bool wrote = false;
        for (int b = 0; b < s.buttonCount && b < 128; ++b) {
            const bool down = (s.buttons[b] & 0x80) != 0;
            if (down == d.buttons[b]) continue;
            d.buttons[b] = down;
            fprintf(f_, "%.3f %s button %d %d\n", t, d.name.c_str(), b, down ? 1 : 0);
            wrote = true;
        }
        for (int p = 0; p < 4 && s.povs; ++p) {
            const long v = pov(s.povs[p]);
            if (v == d.povs[p]) continue;
            d.povs[p] = v;
            fprintf(f_, "%.3f %s hat %d %ld\n", t, d.name.c_str(), p, v);
            wrote = true;
        }
        for (int a = 0; a < 8 && s.axes; ++a) {
            const long v = s.axes[a], step = (d.mx[a] - d.mn[a]) / 64;
            if (!d.ok[a] || v < d.mn[a] || v > d.mx[a] || (v > d.axes[a] ? v - d.axes[a] : d.axes[a] - v) < (step ? step : 1)) continue;
            d.axes[a] = v;
            fprintf(f_, "%.3f %s axis %d %ld range=%ld..%ld\n", t, d.name.c_str(), a, v, d.mn[a], d.mx[a]);
            wrote = true;
        }
        if (wrote) fflush(f_);
    }

private:
    struct Dev {
        std::string name;
        bool ok[8] = {}, buttons[128] = {};
        long axes[8] = {}, mn[8] = {}, mx[8] = {}, povs[4] = {-1, -1, -1, -1};
    };
    static long pov(unsigned long v) { return (v & 0xFFFF) == 0xFFFF ? -1 : (long)v; }   // DirectInput: centred is LOWORD 0xFFFF
    void toggle(uint64_t nowMs)
    {
        const bool on = GetFileAttributesA((dir_ + "\\record.on").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (on && !f_) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char name[64];
            // A new file every time, never an existing one: "x" is exclusive create; a taken name gets -2, -3, ...
            // _fsopen keeps it readable while it is written (fopen_s would lock it).
            for (int k = 1; !f_ && k <= 99; ++k) {
                sprintf_s(name, k == 1 ? "\\macro-%04u%02u%02u-%02u%02u%02u.txt" : "\\macro-%04u%02u%02u-%02u%02u%02u-%d.txt", st.wYear, st.wMonth,
                          st.wDay, st.wHour, st.wMinute, st.wSecond, k);
                path_ = dir_ + name;
                f_ = _fsopen(path_.c_str(), "wx", _SH_DENYNO);
                if (!f_ && errno != EEXIST) break;
            }
            if (!f_) return;
            fprintf(f_, "# dbce macro, recorded %04u-%02u-%02u %02u:%02u:%02u through the fence (record.on); seconds from the start\n", st.wYear,
                    st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fflush(f_);
            start_ = nowMs;
            devs_.clear();
        } else if (!on && f_) {
            fclose(f_);
            f_ = nullptr;
        }
    }
    std::mutex m_;
    std::string dir_, path_;
    FILE *f_ = nullptr;
    bool checked_ = false;
    uint64_t checkedAt_ = 0, start_ = 0;
    std::map<std::string, Dev> devs_;
};

}} // namespace dbce::macro
