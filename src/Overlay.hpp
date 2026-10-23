#pragma once

#include <Geode/Geode.hpp>
#include "Macro.hpp"
#include "Assist.hpp"

#include <functional>
#include <optional>
#include <vector>

// Stats for the current attempt
struct AttemptStats {
    int perfect = 0, great = 0, good = 0, bad = 0, miss = 0, extra = 0;
    double sumAbsMs = 0.0;
    double sumMs = 0.0;
    int judged = 0; // clicks that had an offset (perfect..bad)
    int assisted = 0; // inputs the aim assist corrected
    struct Worst { int press; double ms; };
    std::vector<Worst> worst; // sorted by |ms| desc, at most 5

    void reset() { *this = AttemptStats{}; }
    int total() const { return perfect + great + good + bad + miss; }
    double avgAbs() const { return judged ? sumAbsMs / judged : 0.0; }
    double avg() const { return judged ? sumMs / judged : 0.0; }
    double accuracy() const; // 0..100
};

// Draws the hit line, the click bars and the osu!mania-style lane on top of PlayLayer.
class ClickOverlay : public cocos2d::CCNode {
public:
    static ClickOverlay* create(Macro macro, int levelID);

    void refresh(PlayLayer* pl, float dt);
    void onReset();
    // Called when the real player presses or releases jump
    void onPlayerInput(PlayLayer* pl, bool down, bool p2);

    Macro const& getMacro() const { return m_macro; }
    AttemptStats const& getStats() const { return m_stats; }
    // Human readable summary of the current attempt
    std::string statsText() const;

    void loadSettings();
    // Save trouble-spot counts to disk
    void saveTrouble();
    void resetTrouble();

    // ---- Aim assist ----
    bool assistActive(PlayLayer* pl) const;
    // Why the assist is not acting right now ("" when it is active, or the assist is turned off)
    std::string assistInactiveReason(PlayLayer* pl) const;
    // What the assist did with the last real input (for the diagnostic log)
    std::string const& lastAssistNote() const { return m_assistNote; }
    bool assistOnPress(PlayLayer* pl, bool p2);
    bool assistOnRelease(PlayLayer* pl, bool p2);
    // Level percent from the player's position (the game's own percent is attempt-relative
    // when playing from a start pos, which made every percent-based option misfire there)
    static float levelPercent(PlayLayer* pl);
    // Practice mode or playing from a start pos
    static bool practicing(PlayLayer* pl);
    // Physics step length seen this tick (to compare the game's TPS with the macro's)
    void notePhysicsStep(float dt);
    // "" or a one-line warning when the game runs at a different TPS than the macro was made at
    std::string tpsWarning() const;
    void assistTick(PlayLayer* pl, std::function<void(bool down, bool p2)> const& fire);
    void setFire(std::function<void(bool down, bool p2)> fire) { m_fire = std::move(fire); }
    static double assistWindowFor(int strength);

    double currentTime(PlayLayer* pl);

    // Diagnostics: "idx=12 start=3.4167 end=3.5000 dStart=-0.0083" for the macro press nearest to t
    std::string nearestInfo(double t, bool p2) const;
    // Diagnostics: where the bars put time t in the level (with the live correction)
    float barXForTime(double t) const { return m_usePositions ? this->xForTime(t) + (m_xDeltaValid ? m_xDelta : 0.f) : -1.f; }
    float xDelta() const { return m_xDeltaValid ? m_xDelta : 0.f; }
    bool usesPositions() const { return m_usePositions; }

    // ---- Auto-play (bot) ----
    bool autoplayActive(PlayLayer* pl) const;
    // Called every tick: fires the macro's own presses/releases. fire(down, p2)
    void autoplayTick(PlayLayer* pl, std::function<void(bool down, bool p2)> const& fire);

    // ---- Section practice ----
    // true when the player just crossed the section end (caller restarts the level)
    bool sectionEndReached(PlayLayer* pl);
    bool sectionWantsCheckpoint(PlayLayer* pl); // crossed the section start in practice mode

    // Attempt finished (death / complete / section loop): store history, return accuracy
    void finishAttempt(bool completed);
    std::vector<double> const& history() const { return m_history; }

    // Macro alignment. Eclipse (and some other bots) count a macro recorded from a start pos
    // from that start pos, not from the level start. Called at every fresh start of an attempt.
    void alignForAttempt(PlayLayer* pl, bool freshStart);
    double alignOffset() const { return m_alignOffset; }

    // Macro editing helpers
    void shiftMacro(double seconds);
    void trimMacroBefore(double seconds);
    double nowTime() const { return m_now; }

protected:
    bool init(Macro macro, int levelID);

private:
    struct Settings {
        bool showBars, showHitLine, showMarkers, hidePassed;
        bool showLane, laneRight, laneHorizontal, showJudgments;
        bool assistFlash, troubleSpots, tickPress, tickRelease;
        bool offsetTrail, countdown, timeline, countIn, autoplay, cueSound;
        float laneLookahead, laneScale, offset, hideAfterPercent, laneGap;
        float tickVolume, autoplaySpeed, cueLead, cueVolume;
        std::string cuePath;
        float sectionStart, sectionEnd;
        double perfectMs, greatMs, goodMs;
        int troubleThreshold;
        cocos2d::ccColor4F barColor, p2Color, lineColor, laneColor, p2LaneColor, troubleColor;
    };

    Macro m_macro;
    int m_levelID = 0;
    Settings m_set{};
    AttemptStats m_stats;

    cocos2d::CCDrawNode* m_barDraw = nullptr;
    cocos2d::CCDrawNode* m_laneDraw = nullptr;
    cocos2d::CCLabelBMFont* m_judgeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_offsetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_assistLabel = nullptr;
    cocos2d::CCLabelBMFont* m_warnLabel = nullptr;
    double m_stepRate = 0.0; // smoothed physics steps per second

    bool m_prepared = false;
    bool m_usePositions = false;
    float m_tableStep = 1.f / 60.f;
    std::vector<float> m_timeTable;

    float m_pxPerSecond = 0.f;
    float m_lastPlayerX = 0.f;
    double m_lastLevelTime = -1.0;

    double m_now = 0.0;
    size_t m_missCursor = 0;
    size_t m_tickCursor = 0;
    size_t m_cueCursor = 0;
    bool m_onlyPractice = false;
    bool m_enabled = true;
    std::string m_alignMode = "Auto"; // Auto / Level Start / Start Pos
    double m_alignOffset = 0.0;       // seconds currently added to the macro's own times

    AssistEngine m_assist[2];
    std::function<void(bool, bool)> m_fire; // sends a game input (set by assistTick)
    double m_clockOffset = 0.0;
    float m_xDelta = 0.f;
    bool m_xDeltaValid = false;
    AssistEngine::Params m_assistParams;
    bool m_assistOn = false;
    float m_assistOffPercent = 0.f;
    float m_assistFlashTimer = 0.f;
    std::string m_assistNote;

    // autoplay: index of the macro press each player is holding (-1 = not holding)
    size_t m_autoCursor = 0;
    int m_autoHeld[2] = {-1, -1};
    bool m_autoResync = true;
    // offset trail: offsets from the previous attempt, by press index
    std::vector<float> m_lastOffsets;
    std::vector<float> m_curOffsets;
    // attempt history (accuracy per attempt) for this level
    std::vector<double> m_history;
    // count-in
    std::vector<double> m_countIn;
    size_t m_countInCursor = 0;
    // section practice
    bool m_sectionArmedEnd = false;
    bool m_sectionArmedStart = false;
    cocos2d::CCLabelBMFont* m_countdownLabel = nullptr;
    cocos2d::CCDrawNode* m_timelineDraw = nullptr;

    // trouble spots: bad attempts per macro press
    std::vector<int> m_trouble;
    bool m_troubleDirty = false;

    bool m_held[2] = {false, false};
    bool m_needResync = true;
    float m_judgeTimer = 0.f;
    float m_receptorFlash[2] = {0.f, 0.f};
    cocos2d::ccColor4F m_flashColor[2]{};

    void prepare(PlayLayer* pl);
    float xForTime(double t) const;
    double timeForX(float x) const;
    void resync();
    void updateMisses();
    void playTicks();
    void loadTrouble();
    void noteResult(int pressIndex, bool bad);
    void recordJudgment(char const* text, int pressIndex, std::optional<double> ms);
    void showJudgment(char const* text, cocos2d::ccColor3B color, std::optional<double> offsetMs, bool p2);
    void onAssisted(bool p2);
    bool isTrouble(int pressIndex) const;
    bool pastHidePercent(PlayLayer* pl) const;

    void drawBars(PlayLayer* pl);
    void drawLane(int lane, float centerX);
    void drawLaneHorizontal(int lane, float centerY);
    void drawTimeline(PlayLayer* pl);
    void drawTrailVertical(int lane, float cx, float bottomY, float topY, float speed);
    void drawTrailHorizontal(int lane, float cy, float leftX, float rightX, float speed);
    void loadHistory();
    void saveHistory();
    void setupCountIn();
    void applyTheme(std::string const& theme);
};
