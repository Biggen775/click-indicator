#pragma once

#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

// Aim assist for one player.
//
// Real inputs come in through onPress / onRelease. Each one is either let straight
// through (returns false) or held back / swallowed (returns true). Queued inputs are
// sent from tick() in order.
//
// Spam assist (for wave / ship) adds three behaviours:
//  * extra taps while the macro is holding are absorbed (the game keeps holding)
//  * while you physically hold, the game follows the macro's press/release pattern
//    (so holding through a spam section produces the spam) for up to `followMax` seconds
//  * taps that match nothing in the macro are swallowed instead of sent
class AssistEngine {
public:
    struct Params {
        double pressWindow = 0.02;   // how early a press may be and still be lined up
        double releaseWindow = 0.02; // same for releases
        bool followHold = false;     // early release waits for the macro's release
        bool spam = false;           // spam assist on/off
        double followMax = 2.0;      // seconds the pattern-follow may run per physical hold
        double followGap = 0.4;      // max gap between macro presses to keep following
    };

    static constexpr double kGap = 1.0 / 240.0 + 0.0002;
    static constexpr double kMaxReleaseDelay = 0.1;
    static constexpr double kSnapEpsilon = 0.002;

    void reset() {
        m_events.clear();
        m_gameDown = false;
        m_physDown = false;
        m_swallowedPress = false;
        m_active = -1;
        m_lastUsed = -1;
    }

    bool hasQueued() const { return !m_events.empty(); }
    bool needsTick() const { return !m_events.empty() || (m_active >= 0 && m_gameDown); }

    // returns true if the input was held back or swallowed
    bool onPress(std::vector<Press> const& presses, bool p2, double now, Params const& prm) {
        m_physDown = true;
        m_physDownAt = now;
        m_swallowedPress = false;

        // Inside an active macro hold (the game is holding, release queued or still ahead)
        if (m_active >= 0 && m_gameDown) {
            double end = m_active < static_cast<int>(presses.size()) ? presses[m_active].end : now;
            if (prm.spam && end > now + kSnapEpsilon) {
                // a re-press while the macro still holds: absorb it, the game keeps holding
                this->dropQueuedReleases();
                return true;
            }
        }

        if (!m_events.empty()) {
            if (m_events.back().down) return true; // a press is already waiting
            // re-pressing for the macro press that is already queued/active: absorb it
            int same = this->findMatch(presses, p2, now, prm.pressWindow, false);
            if (same >= 0 && same == m_active) return true;
            // a release is waiting: keep order, press after it
            int match = this->findMatch(presses, p2, now, prm.pressWindow, true);
            double target = m_events.back().t + kGap;
            if (match >= 0) {
                target = std::max(target, presses[match].start);
                m_lastUsed = match;
                m_active = match;
            }
            m_events.push_back({target, true});
            return true;
        }

        int match = this->findMatch(presses, p2, now, prm.pressWindow, true);
        if (match >= 0) {
            m_lastUsed = match;
            m_active = match;
            double start = presses[match].start;
            if (start - now > kSnapEpsilon) {
                m_events.push_back({start, true}); // early: wait for the macro's moment
                return true;
            }
            m_gameDown = true;
            return false; // on time or late: through now
        }

        // Late into a hold that is still going? (common in ship/wave) press now and follow its end
        int inside = this->findHoldCovering(presses, p2, now);
        if (inside >= 0) {
            m_lastUsed = std::max(m_lastUsed, inside);
            m_active = inside;
            m_gameDown = true;
            return false;
        }

        if (prm.spam) {
            // nothing in the macro here: an extra tap, swallow it
            m_swallowedPress = true;
            m_active = -1;
            return true;
        }

        m_active = -1;
        m_gameDown = true;
        return false;
    }

    bool onRelease(std::vector<Press> const& presses, double now, Params const& prm) {
        m_physDown = false;

        if (m_swallowedPress) {
            m_swallowedPress = false;
            return true; // the press never reached the game
        }

        double macroEnd = -1.0;
        if (m_active >= 0 && m_active < static_cast<int>(presses.size())) macroEnd = presses[m_active].end;

        if (!m_events.empty()) {
            if (!m_events.back().down) return true; // a release is already waiting
            if (m_events.back().chained) {
                // the follow predicted a press the player didn't make: cancel it
                m_events.pop_back();
                if (m_active > 0) m_lastUsed = m_active - 1;
                m_active = -1;
                if (m_events.empty() && !m_gameDown) return true;
                if (m_events.empty()) { m_gameDown = false; return false; }
                return true;
            }
            double target = m_events.back().t + kGap;
            if (macroEnd > 0.0) target = std::max(target, macroEnd);
            m_events.push_back({target, false});
            return true;
        }

        if (!m_gameDown) return true; // game isn't holding (absorbed earlier), nothing to release

        double releaseWindow = prm.followHold ? 1e300 : std::min(prm.releaseWindow, kMaxReleaseDelay);
        if (macroEnd > 0.0 && macroEnd - now > kSnapEpsilon && macroEnd - now <= releaseWindow) {
            m_events.push_back({macroEnd, false}); // released early: line it up
            return true;
        }

        m_gameDown = false;
        m_active = -1;
        return false;
    }

    // Sends queued inputs whose time has come, and runs the pattern-follow. fire(down)
    template <class F>
    void tick(std::vector<Press> const& presses, bool p2, double now, Params const& prm, F&& fire) {
        while (!m_events.empty() && m_events.front().t <= now) {
            auto ev = m_events.front();
            m_events.pop_front();
            m_gameDown = ev.down;
            fire(ev.down);
            if (ev.down && prm.spam && m_physDown && m_active >= 0 && m_events.empty()) {
                // a follow-press just went out: its release comes at the macro's end
                m_events.push_back({std::max(presses[m_active].end, ev.t + kGap), false});
            }
            if (!ev.down && m_events.empty()) {
                if (!this->chainNext(presses, p2, now, prm)) m_active = -1;
            }
        }

        // Still physically holding through the macro's release? follow the pattern.
        if (prm.spam && m_physDown && m_gameDown && m_active >= 0 && m_events.empty() &&
            m_active < static_cast<int>(presses.size()) && presses[m_active].end <= now &&
            now - m_physDownAt <= prm.followMax) {
            m_gameDown = false;
            fire(false);
            if (!this->chainNext(presses, p2, now, prm)) m_active = -1;
        }
    }

    template <class F>
    void flush(F&& fire) {
        while (!m_events.empty()) {
            auto ev = m_events.front();
            m_events.pop_front();
            m_gameDown = ev.down;
            fire(ev.down);
        }
        m_active = -1;
    }

private:
    struct Event {
        double t;
        bool down;
        bool chained = false; // queued by the pattern-follow, not by a real press
    };

    std::deque<Event> m_events;
    bool m_gameDown = false;
    bool m_physDown = false;
    bool m_swallowedPress = false;
    double m_physDownAt = 0.0;
    int m_active = -1;
    int m_lastUsed = -1;

    void dropQueuedReleases() {
        while (!m_events.empty() && !m_events.back().down) m_events.pop_back();
    }

    // Queue the next macro press if the player is still holding and it starts soon
    bool chainNext(std::vector<Press> const& presses, bool p2, double now, Params const& prm) {
        if (!prm.spam || !m_physDown || now - m_physDownAt > prm.followMax) return false;
        for (int i = m_active + 1; i < static_cast<int>(presses.size()); i++) {
            if (presses[i].p2 != p2) continue;
            if (presses[i].start > now + prm.followGap) return false;
            if (presses[i].end <= now) continue;
            m_active = i;
            m_lastUsed = i;
            m_events.push_back({std::max(presses[i].start, now + kGap), true, true});
            return true;
        }
        return false;
    }

    int findMatch(std::vector<Press> const& presses, bool p2, double now, double window, bool unusedOnly) const {
        int best = -1;
        double bestDist = 0.0;
        auto it = std::lower_bound(presses.begin(), presses.end(), now - window, [](Press const& p, double t) {
            return p.start < t;
        });
        for (; it != presses.end() && it->start <= now + window; ++it) {
            int idx = static_cast<int>(it - presses.begin());
            if (it->p2 != p2 || (unusedOnly && idx <= m_lastUsed)) continue;
            double dist = std::abs(it->start - now);
            if (best < 0 || dist < bestDist) {
                best = idx;
                bestDist = dist;
            }
        }
        return best;
    }

    // A hold that started before `now` and hasn't ended yet
    int findHoldCovering(std::vector<Press> const& presses, bool p2, double now) const {
        auto it = std::lower_bound(presses.begin(), presses.end(), now, [](Press const& p, double t) {
            return p.start < t;
        });
        int limit = 0;
        while (it != presses.begin() && limit++ < 8) {
            --it;
            if (it->p2 != p2) continue;
            int idx = static_cast<int>(it - presses.begin());
            if (idx <= m_lastUsed) return -1;
            if (it->end > now + kSnapEpsilon && it->end - it->start > 0.03) return idx;
        }
        return -1;
    }
};
