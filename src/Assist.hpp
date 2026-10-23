#pragma once

#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Aim assist for one player.
//
// The player's real button state is tracked (onPress / onRelease). Every game tick,
// evaluate() decides what the GAME's button state should be right now and sends a
// press or release when it has to change. No queues: the decision is re-made each tick
// from the macro, so holds, taps and spam all follow the same few rules.
//
// Rules (W = press window from the strength, R = release window):
//  * A press within W of a macro press is matched to the NEAREST macro press that has
//    not been played yet. Early: the game presses exactly at the macro time. On time or
//    late: goes through now.
//  * Release early: the game keeps holding until the macro's release if that is within R,
//    or always when followHold is set (release strength 5+).
//  * Release late (still holding at the macro's release) with followHold: the game lets go
//    at the macro's release (+ holdSlack); the rest of that physical hold is ignored.
//    Without followHold the hold is the player's own from there.
//  * Re-press while the game is still holding a matched press: the hold is kept to its
//    macro release, then the new press is played at its own macro time (no early release).
//  * Spam assist (wave / ship): extra taps while the macro is holding are absorbed; with
//    `follow`, holding through gaps up to followGap re-presses along the macro's pattern.
//    Taps that match nothing are passed through (or swallowed with swallowStray).
class AssistEngine {
public:
    struct Params {
        double pressWindow = 0.02;
        double releaseWindow = 0.02;
        bool followHold = false;
        bool spam = false;          // absorb extra taps while the macro holds (safe, on by default)
        bool follow = false;        // pattern-follow while physically held (opt-in)
        bool swallowStray = false;  // swallow taps that match nothing (opt-in)
        double followMax = 2.0;
        double followGap = 0.4;
        double releaseOffset = 0.0;
        double holdSlack = 0.0;
    };

    // floating point noise only: macro times are exact tick times, a release is due on the tick
    // whose clock reads the macro's release time
    static constexpr double kEps = 1e-6;
    static constexpr double kStrayWindow = 0.15;

    void reset() {
        m_physDown = false;
        m_gameDown = false;
        m_anchor = -1;
        m_next = -1;
        m_lastFired = -1;
        m_swallow = false;
        m_following = false;
        m_absorbed = false;
        m_plain = false;
        m_lastAction = "reset";
    }

    bool gameDown() const { return m_gameDown; }
    bool busy() const { return m_gameDown || m_anchor >= 0; }
    int anchor() const { return m_anchor; }
    // What the last onPress / onRelease decided (for the diagnostic log)
    char const* lastAction() const { return m_lastAction; }

    // Real press. Returns true if the game should NOT get this press right now
    // (evaluate() will send it at the right moment, or never).
    template <class F>
    bool onPress(std::vector<Press> const& presses, bool p2, double now, Params const& prm, F&& fire) {
        m_physDown = true;
        m_physDownAt = now;
        m_swallow = false;
        m_following = false;
        m_plain = false;
        m_absorbed = false;

        // the game is still holding a matched macro press: keep that hold to its release,
        // and line this new press up with the next macro press if it is close to one
        if (m_anchor >= 0 && m_gameDown && this->endOf(presses, m_anchor, prm) > now + kEps) {
            int match = this->findMatch(presses, p2, now, prm.pressWindow, -1);
            if (match == m_anchor) {
                m_absorbed = true;
                m_lastAction = "absorbed";
                return true; // a re-press for the press we are already holding
            }
            if (match > m_anchor) {
                m_next = match;
                m_lastAction = "queued-next";
                return true;
            }
            if (prm.spam && presses[m_anchor].end - presses[m_anchor].start > 0.12) {
                m_absorbed = true;
                m_lastAction = "absorbed";
                return true; // absorbed, the game keeps holding
            }
            // a re-press for nothing in particular while holding: let go and press again now
            // (the player's own click), the hold's release window still applies
            m_plain = true;
            m_lastAction = "re-press";
            this->set(false, fire);
            this->set(true, fire);
            return true;
        }
        m_next = -1;

        int match = this->findMatch(presses, p2, now, prm.pressWindow, -1);
        if (match < 0) match = this->findHoldCovering(presses, p2, now);
        if (match < 0) {
            // near a macro press but outside the strength window: a plain click for that press
            int nearIdx = this->findMatch(presses, p2, now, std::max(prm.pressWindow, kStrayWindow), -1);
            if (nearIdx >= 0) {
                // the player's own click, sent now; the anchor only serves the release window
                m_anchor = nearIdx;
                m_plain = true;
                m_lastAction = "plain";
                this->set(true, fire);
                return true; // (we sent it ourselves)
            }
            if (prm.swallowStray) {
                m_swallow = true; // stray tap
                m_anchor = -1;
                m_lastAction = "stray-swallowed";
                return true;
            }
            m_anchor = -1;
            m_lastAction = "stray";
            this->set(true, fire);
            return true;
        }

        m_anchor = match;
        m_lastAction = now < presses[match].start ? "early-wait" : "matched";
        this->evaluate(presses, p2, now, prm, fire);
        return true;
    }

    template <class F>
    bool onRelease(std::vector<Press> const& presses, bool p2, double now, Params const& prm, F&& fire) {
        m_physDown = false;
        if (m_swallow) {
            m_swallow = false;
            m_lastAction = "swallowed";
            return true;
        }
        bool wasDown = m_gameDown;
        this->evaluate(presses, p2, now, prm, fire);
        m_lastAction = !wasDown ? "pending" : m_gameDown ? "kept-holding" : "released";
        return true;
    }

    // Called every tick. fire(down)
    template <class F>
    void evaluate(std::vector<Press> const& presses, bool p2, double now, Params const& prm, F&& fire) {
        if (m_anchor < 0) {
            // nothing attached: the game simply mirrors the player (minus swallowed taps)
            this->set(m_physDown && !m_swallow, fire);
            return;
        }
        if (m_anchor >= static_cast<int>(presses.size())) { m_anchor = -1; m_next = -1; this->set(m_physDown, fire); return; }

        Press const& p = presses[m_anchor];
        double start = p.start;
        double end = this->endOf(presses, m_anchor, prm);

        // a later press was queued while this hold was going: release at this hold's end
        // and carry on with the queued press (as an early tap for it)
        if (m_next >= 0 && m_gameDown && now >= end - kEps) {
            this->set(false, fire);
            m_anchor = m_next;
            m_next = -1;
            m_absorbed = false;
            this->evaluate(presses, p2, now, prm, fire);
            return;
        }

        if (m_physDown) {
            if (now < start && !m_plain) { this->set(false, fire); return; }     // early: wait
            if (now < end - kEps) { this->set(true, fire); return; }             // inside the hold
            // past the macro's release while still holding
            if (prm.follow && now - m_physDownAt <= prm.followMax) {
                int next = this->nextPress(presses, p2, m_anchor);
                double gap = next >= 0 ? presses[next].start - p.end : 1e300;
                // grace for your own slightly-late release, only on a lone release (nothing to follow)
                double slack = (m_following || m_absorbed || (next >= 0 && gap <= prm.followGap)) ? 0.0 : prm.holdSlack;
                if (now < end + slack) { this->set(true, fire); return; }       // grace for your own release
                if (next >= 0 && gap <= prm.followGap) {
                    // follow the pattern: release now, press again at the next start
                    m_following = true;
                    m_anchor = next;
                    this->set(now >= presses[next].start, fire);
                    return;
                }
                this->set(false, fire);
                m_anchor = -1;
                m_swallow = true; // the rest of this physical hold is ignored
                return;
            }
            if (prm.followHold && !m_plain) {
                // release strength 5+: the macro's release is the release, even if you are still holding
                if (now < end + prm.holdSlack) { this->set(true, fire); return; }
                this->set(false, fire);
                m_anchor = -1;
                m_swallow = true; // the rest of this physical hold is ignored
                return;
            }
            // no follow-hold: the hold is the player's own from here
            this->set(true, fire);
            m_anchor = -1;
            return;
        }

        // player has let go
        if (!m_gameDown) {
            if (m_following) {
                // a pattern-follow press that hasn't happened yet: the player let go, so cancel it
                m_following = false;
                m_anchor = -1;
                return;
            }
            // early tap released before the macro press: still deliver the tap at macro time
            if (now < start) return;
            if (now < end - kEps) { this->set(true, fire); return; }
            m_anchor = -1;
            return;
        }
        // game is holding, player isn't: hold to the macro's end if allowed
        // a plain (unsnapped) click only gets the release window, never the full follow-hold
        bool keep = now < end - kEps && ((prm.followHold && !m_plain) || end - now <= prm.releaseWindow);
        if (keep) return;
        this->set(false, fire);
        if (m_next >= 0) { m_anchor = m_next; m_next = -1; return; } // queued press: wait for its time
        m_anchor = -1;
        m_following = false;
    }

    template <class F>
    void flush(F&& fire) {
        this->set(m_physDown && !m_swallow, fire);
        m_anchor = -1;
        m_next = -1;
        m_following = false;
    }

private:
    bool m_physDown = false;
    bool m_gameDown = false;
    bool m_swallow = false;
    bool m_following = false;
    bool m_absorbed = false;
    bool m_plain = false;
    double m_physDownAt = 0.0;
    int m_anchor = -1;
    int m_next = -1;      // press queued behind the current hold (re-press while holding)
    int m_lastFired = -1; // last macro press whose press was actually sent
    char const* m_lastAction = "reset";

    template <class F>
    void set(bool down, F&& fire) {
        if (down == m_gameDown) return;
        m_gameDown = down;
        if (down && m_anchor >= 0) m_lastFired = std::max(m_lastFired, m_anchor);
        fire(down);
    }

    double endOf(std::vector<Press> const& presses, int idx, Params const& prm) const {
        if (idx < 0 || idx >= static_cast<int>(presses.size())) return -1.0;
        return presses[idx].end + prm.releaseOffset;
    }

    int nextPress(std::vector<Press> const& presses, bool p2, int from) const {
        for (int i = from + 1; i < static_cast<int>(presses.size()); i++) {
            if (presses[i].p2 == p2) return i;
        }
        return -1;
    }

    // The nearest macro press (for this player) within `window` of now that has not been
    // played yet. Presses up to `after` (and anything already fired) are not candidates,
    // except the current anchor.
    int findMatch(std::vector<Press> const& presses, bool p2, double now, double window, int after) const {
        int best = -1;
        double bestDist = 0.0;
        auto it = std::lower_bound(presses.begin(), presses.end(), now - window, [](Press const& p, double t) {
            return p.start < t;
        });
        for (; it != presses.end() && it->start <= now + window; ++it) {
            int idx = static_cast<int>(it - presses.begin());
            if (it->p2 != p2) continue;
            if (idx <= after) continue;
            if (idx <= m_lastFired && idx != m_anchor) continue;
            double dist = std::abs(it->start - now);
            if (best < 0 || dist < bestDist) {
                best = idx;
                bestDist = dist;
            }
        }
        return best;
    }

    int findHoldCovering(std::vector<Press> const& presses, bool p2, double now) const {
        auto it = std::lower_bound(presses.begin(), presses.end(), now, [](Press const& p, double t) {
            return p.start < t;
        });
        int limit = 0;
        while (it != presses.begin() && limit++ < 8) {
            --it;
            if (it->p2 != p2) continue;
            int idx = static_cast<int>(it - presses.begin());
            if (idx <= m_lastFired && idx != m_anchor) return -1;
            if (it->end > now + kEps && it->end - it->start > 0.03) return idx;
            return -1;
        }
        return -1;
    }
};
