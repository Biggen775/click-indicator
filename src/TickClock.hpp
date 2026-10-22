#pragma once

// A single level-clock reading per game tick, shared by the recorder and the assist.
// begin() is called at the start of input processing for a tick; while inside the tick
// every reading returns that snapshot, so recording and playback can't drift by a tick.
struct TickClock {
    static inline double s_value = 0.0;
    static inline double s_lastFired = -1.0;
    static inline bool s_inTick = false;

    static void begin(double levelTime) {
        s_value = levelTime;
        s_lastFired = levelTime;
        s_inTick = true;
    }
    static void end() { s_inTick = false; }
    static bool inTick() { return s_inTick; }
    static bool firedThisTick(double levelTime) { return s_lastFired == levelTime; }
    static double now(double liveLevelTime) { return s_inTick ? s_value : liveLevelTime; }
};
