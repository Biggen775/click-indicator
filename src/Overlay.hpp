#pragma once

#include <Geode/Geode.hpp>
#include "Macro.hpp"

#include <functional>
#include <optional>

// Draws the hit line, the click bars and the osu!mania-style lane on top of PlayLayer.
class ClickOverlay : public cocos2d::CCNode {
public:
    static ClickOverlay* create(Macro macro);

    // Called every frame from PlayLayer::postUpdate
    void refresh(PlayLayer* pl, float dt);
    // Called from PlayLayer::resetLevel (death / restart / checkpoint)
    void onReset();
    // Called when the real player presses or releases jump
    void onPlayerInput(PlayLayer* pl, bool down, bool p2);

    Macro const& getMacro() const { return m_macro; }

    // Re-read the mod settings (called on reset and when the in-game menu changes something)
    void loadSettings();

    // ---- Aim assist ----
    // Is the assist on and usable in this level right now?
    bool assistActive(PlayLayer* pl) const;
    // Real press / release from the player. Returns true if the input should be held back.
    bool assistOnPress(PlayLayer* pl, bool p2);
    bool assistOnRelease(PlayLayer* pl, bool p2);
    // Called every game tick: fires held-back inputs when their macro time arrives.
    void assistTick(PlayLayer* pl, std::function<void(bool down, bool p2)> const& fire);
    // Assist window in seconds for a strength of 1..10
    static double assistWindowFor(int strength);

protected:
    bool init(Macro macro);

private:
    struct Settings {
        bool showBars, showHitLine, showMarkers, hidePassed;
        bool showLane, laneRight, showJudgments;
        float laneSpeed, laneScale, offset;
        cocos2d::ccColor4F barColor, p2Color, lineColor, laneColor;
    };

    Macro m_macro;
    Settings m_set{};

    cocos2d::CCDrawNode* m_barDraw = nullptr;
    cocos2d::CCDrawNode* m_laneDraw = nullptr;
    cocos2d::CCLabelBMFont* m_judgeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_offsetLabel = nullptr;

    // Level-time -> level-X lookup table (sampled from PlayLayer::posForTime)
    bool m_prepared = false;
    bool m_usePositions = false;
    float m_tableStep = 1.f / 60.f;
    std::vector<float> m_timeTable;

    // Fallback (time based) scrolling when positions are not available
    float m_pxPerSecond = 0.f;
    float m_lastPlayerX = 0.f;
    double m_lastLevelTime = -1.0;

    double m_now = 0.0;
    size_t m_missCursor = 0;
    bool m_onlyPractice = false;
    bool m_enabled = true;

    struct AssistState {
        std::optional<double> pressAt;   // held-back press, fires at this macro time
        std::optional<double> releaseAt; // held-back release
        int active = -1;                 // macro press this click was matched to
        int lastUsed = -1;               // so one macro click isn't matched twice
    };
    AssistState m_assist[2];
    bool m_assistOn = false;
    double m_assistWindow = 0.02;
    bool m_held[2] = {false, false};
    bool m_needResync = true;
    float m_judgeTimer = 0.f;
    float m_receptorFlash[2] = {0.f, 0.f};
    cocos2d::ccColor4F m_flashColor[2]{};

    void prepare(PlayLayer* pl);
    float xForTime(double t) const;
    double timeForX(float x) const;
    double currentTime(PlayLayer* pl);
    void resync();
    void updateMisses();
    void showJudgment(char const* text, cocos2d::ccColor3B color, std::optional<double> offsetMs, bool p2);

    void drawBars(PlayLayer* pl);
    void drawLane(int lane, float centerX);
};
