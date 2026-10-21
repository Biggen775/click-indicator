#pragma once

#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

// Aim assist for one player.
//
// Real inputs come in through onPress / onRelease. Each one is either let straight
// through (returns false) or held back and queued (returns true). Queued inputs are
// sent from tick() in the exact order they happened, so a release is never lost or
// merged into the next press (that used to make the wave hold too long).
class AssistEngine {
public:
    // Smallest gap between two queued inputs: one game tick at 240 tps
    static constexpr double kGap = 1.0 / 240.0 + 0.0002;
    // A held-back release is never delayed more than this, even at high strength
    static constexpr double kMaxReleaseDelay = 0.1;
    // Inputs closer than this to their target just go through
    static constexpr double kSnapEpsilon = 0.002;

    void reset() {
        m_events.clear();
        m_gameHeld = false;
        m_active = -1;
        m_lastUsed = -1;
    }

    bool hasQueued() const { return !m_events.empty(); }

    bool onPress(std::vector<Press> const& presses, bool p2, double now, double window) {
        int match = this->findMatch(presses, p2, now, window);
        double target = now;
        if (match >= 0) {
            target = std::max(now, presses[match].start);
            m_lastUsed = match;
        }
        m_active = match;

        if (m_events.empty()) {
            if (target - now <= kSnapEpsilon) {
                m_gameHeld = true;
                return false; // on time, late, or nothing to line up with
            }
            m_events.push_back({target, true}); // early: wait for the macro's moment
            return true;
        }

        // Something is already queued: keep the order
        if (m_events.back().down) return true; // a press is already waiting
        target = std::max(target, m_events.back().t + kGap);
        m_events.push_back({target, true});
        return true;
    }

    bool onRelease(std::vector<Press> const& presses, double now, double window) {
        double macroEnd = -1.0;
        if (m_active >= 0 && m_active < static_cast<int>(presses.size())) {
            macroEnd = presses[m_active].end;
        }

        if (!m_events.empty()) {
            // The press is still waiting: release when the macro does (but always after the press)
            if (!m_events.back().down) return true; // a release is already waiting
            double target = m_events.back().t + kGap;
            if (macroEnd > 0.0) {
                target = std::max(target, macroEnd);
            }
            m_events.push_back({target, false});
            return true;
        }

        double releaseWindow = std::min(window, kMaxReleaseDelay);
        if (macroEnd > 0.0 && macroEnd - now > kSnapEpsilon && macroEnd - now <= releaseWindow) {
            m_events.push_back({macroEnd, false}); // released a little early: line it up
            return true;
        }

        m_gameHeld = false;
        m_active = -1;
        return false;
    }

    // Sends every queued input whose time has come. fire(down)
    template <class F>
    void tick(double now, F&& fire) {
        while (!m_events.empty() && m_events.front().t <= now) {
            auto ev = m_events.front();
            m_events.pop_front();
            m_gameHeld = ev.down;
            if (!ev.down && m_events.empty()) m_active = -1;
            fire(ev.down);
        }
    }

    // Sends everything immediately (used when the assist is turned off mid-level)
    template <class F>
    void flush(F&& fire) {
        this->tick(1e300, fire);
    }

private:
    struct Event {
        double t;
        bool down;
    };

    std::deque<Event> m_events;
    bool m_gameHeld = false;
    int m_active = -1;
    int m_lastUsed = -1;

    int findMatch(std::vector<Press> const& presses, bool p2, double now, double window) const {
        int best = -1;
        double bestDist = 0.0;
        auto it = std::lower_bound(presses.begin(), presses.end(), now - window, [](Press const& p, double t) {
            return p.start < t;
        });
        for (; it != presses.end() && it->start <= now + window; ++it) {
            int idx = static_cast<int>(it - presses.begin());
            if (it->p2 != p2 || idx <= m_lastUsed) continue;
            double dist = std::abs(it->start - now);
            if (best < 0 || dist < bestDist) {
                best = idx;
                bestDist = dist;
            }
        }
        return best;
    }
};
