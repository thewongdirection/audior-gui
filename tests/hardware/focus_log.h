/*
 * focus_log.h - Remembers when the application's window held the foreground during the hardware
 * tests, so that a failing T14 says when it happened and whether a person was at the PC.
 *
 * The harness looks at the foreground window every 100 ms while it records and hands each look to
 * Sample().  Looks in a row at which the application held the foreground make one episode; each
 * episode keeps where it began, how long it lasted, how long ago the last keyboard or mouse input
 * was when it began, and whether a mouse button was down.  That tells a click (input a moment
 * before, button down) from a window that came forward by itself (no input for seconds).  Input
 * that another program injects counts as input, as for Windows.
 *
 * No Windows headers: the harness fetches the observations, tests/test_focus_log.cpp checks the
 * bookkeeping.
 */
#pragma once

#include <cstdio>
#include <string>
#include <vector>

struct FocusEpisode {
    std::string where;              /* the test it began in: "T1"                                 */
    double      start = 0.0;        /* seconds into that recording                                */
    double      seconds = 0.0;      /* how long the application kept the foreground               */
    long long   idleMs = -1;        /* ms since the last keyboard or mouse input at its start; -1 unknown */
    bool        buttonDown = false; /* a mouse button was down at its start                       */
    int         samples = 0;
};

class FocusLog {
public:
    /* Input this recent before a takeover counts as a person acting; this long without any, as not. */
    static constexpr long long kPersonWithinMs = 1500, kNoInputMs = 5000;
    static constexpr std::size_t kListed = 3;           /* episodes spelled out in a summary */

    explicit FocusLog(double interval = 0.1) : interval_(interval) {}

    /* One look: `held` says whether the application's window owned the foreground. */
    void Sample(const std::string &where, double at, bool held, long long idleMs, bool buttonDown)
    {
        ++samples_;
        if (!held) {
            open_ = false;
            return;
        }
        ++held_;
        if (!open_) {
            FocusEpisode e;
            e.where = where;
            e.start = at;
            e.idleMs = idleMs;
            e.buttonDown = buttonDown;
            episodes_.push_back(e);
            open_ = true;
        }
        FocusEpisode &e = episodes_.back();
        ++e.samples;
        e.seconds = e.samples * interval_;
    }

    /* A recording is over (its window is gone): an episode still open ends here. */
    void EndRecording() { open_ = false; }

    int Samples() const { return samples_; }
    int Held() const { return held_; }
    const std::vector<FocusEpisode> &Episodes() const { return episodes_; }

    /* What the start of an episode says about the cause. */
    static std::string Cause(const FocusEpisode &e)
    {
        if (e.buttonDown)
            return "a mouse button was down: a click";
        if (e.idleMs < 0)
            return "when the last input was is unknown";
        const double seconds = static_cast<double>(e.idleMs) / 1000.0;
        if (e.idleMs <= kPersonWithinMs)
            return Fmt("input %.1f s earlier: probably someone clicking or typing", seconds);
        if (e.idleMs >= kNoInputMs)
            return Fmt("no input for %.1f s: not a click, something else brought it forward", seconds);
        return Fmt("input %.1f s earlier: unclear", seconds);
    }

    /* "308 of 5885 samples, in 2 episodes: T1 at 3.4 s for 29.6 s (no input for ...); ...". */
    std::string Summary() const
    {
        std::string text = Fmt("%d of %d samples", held_, samples_);
        if (episodes_.empty())
            return text;
        text += Fmt(", in %d episode%s: ", static_cast<int>(episodes_.size()), episodes_.size() == 1 ? "" : "s");
        for (std::size_t i = 0; i < episodes_.size() && i < kListed; ++i) {
            const FocusEpisode &e = episodes_[i];
            text += (i ? "; " : "") + e.where + Fmt(" at %.1f s for %.1f s (", e.start, e.seconds) + Cause(e) + ")";
        }
        if (episodes_.size() > kListed)
            text += Fmt("; and %d more", static_cast<int>(episodes_.size() - kListed));
        return text;
    }

private:
    template <typename... Args>
    static std::string Fmt(const char *format, Args... args)
    {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    double                    interval_;
    std::vector<FocusEpisode> episodes_;
    bool                      open_ = false;
    int                       samples_ = 0, held_ = 0;
};
