#include "Overlay.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    constexpr double kJudgeWindow = 0.15;  // seconds: max distance to count a click as "for" a macro press
    constexpr float kJudgeShowTime = 0.7f; // seconds the judgment text stays up
    constexpr double kBadMs = 45.0;        // worse than GREAT counts as a bad attempt for trouble spots

    ccColor4F toColor4F(ccColor4B c) {
        return ccc4FFromccc4B(c);
    }
    ccColor4F withAlpha(ccColor4F c, float a) {
        c.a = a;
        return c;
    }
    ccColor4F mix(ccColor4F a, ccColor4F b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
    std::string troubleKey(int levelID, Macro const& m) {
        return fmt::format("trouble/{}/{}", levelID, m.presses.size());
    }
}

double AttemptStats::accuracy() const {
    int t = this->total();
    if (t == 0) return 100.0;
    double score = perfect * 1.0 + great * 0.8 + good * 0.5 + bad * 0.2;
    return score / t * 100.0;
}

ClickOverlay* ClickOverlay::create(Macro macro, int levelID) {
    auto ret = new ClickOverlay();
    if (ret->init(std::move(macro), levelID)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ClickOverlay::init(Macro macro, int levelID) {
    if (!CCNode::init()) return false;
    m_macro = std::move(macro);
    m_levelID = levelID;

    this->setID("click-indicator-overlay"_spr);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition({0.f, 0.f});

    m_barDraw = CCDrawNode::create();
    m_barDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_barDraw, 0);

    m_laneDraw = CCDrawNode::create();
    m_laneDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_laneDraw, 1);

    m_judgeLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_judgeLabel->setScale(0.28f);
    m_judgeLabel->setOpacity(0);
    this->addChild(m_judgeLabel, 2);

    m_offsetLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_offsetLabel->setScale(0.45f);
    m_offsetLabel->setOpacity(0);
    this->addChild(m_offsetLabel, 2);

    m_assistLabel = CCLabelBMFont::create("ASSIST", "bigFont.fnt");
    m_assistLabel->setScale(0.22f);
    m_assistLabel->setColor({255, 220, 60});
    m_assistLabel->setOpacity(0);
    this->addChild(m_assistLabel, 2);

    m_countdownLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_countdownLabel->setScale(0.3f);
    m_countdownLabel->setOpacity(0);
    this->addChild(m_countdownLabel, 2);

    m_timelineDraw = CCDrawNode::create();
    m_timelineDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_timelineDraw, 1);

    this->loadSettings();
    this->loadTrouble();
    this->loadHistory();

    // Apply the timing offset once
    double offset = m_set.offset / 1000.0;
    for (auto& p : m_macro.presses) {
        p.start += offset;
        p.end += offset;
    }
    return true;
}

void ClickOverlay::loadSettings() {
    auto mod = Mod::get();
    m_set.showBars = mod->getSettingValue<bool>("show-bars");
    m_set.showHitLine = mod->getSettingValue<bool>("show-hit-line");
    m_set.showMarkers = mod->getSettingValue<bool>("show-markers");
    m_set.hidePassed = mod->getSettingValue<bool>("hide-passed");
    m_set.showLane = mod->getSettingValue<bool>("show-lane");
    m_set.laneRight = mod->getSettingValue<std::string>("lane-side") == "Right";
    m_set.laneHorizontal = mod->getSettingValue<std::string>("lane-style") == "Horizontal";
    m_set.showJudgments = mod->getSettingValue<bool>("show-judgments");
    m_set.laneLookahead = static_cast<float>(mod->getSettingValue<double>("lane-lookahead"));
    m_set.laneScale = static_cast<float>(mod->getSettingValue<double>("lane-scale"));
    m_set.laneGap = static_cast<float>(mod->getSettingValue<double>("lane-gap"));
    m_set.offset = static_cast<float>(mod->getSettingValue<double>("offset-ms"));
    m_set.hideAfterPercent = static_cast<float>(mod->getSettingValue<int64_t>("hide-after-percent"));
    m_set.barColor = toColor4F(mod->getSettingValue<ccColor4B>("bar-color"));
    m_set.p2Color = toColor4F(mod->getSettingValue<ccColor4B>("p2-color"));
    m_set.lineColor = toColor4F(mod->getSettingValue<ccColor4B>("line-color"));
    m_set.laneColor = toColor4F(mod->getSettingValue<ccColor4B>("lane-color"));
    m_set.p2LaneColor = toColor4F(mod->getSettingValue<ccColor4B>("p2-lane-color"));
    m_set.troubleColor = toColor4F(mod->getSettingValue<ccColor4B>("trouble-color"));
    m_set.troubleSpots = mod->getSettingValue<bool>("trouble-spots");
    m_set.troubleThreshold = static_cast<int>(mod->getSettingValue<int64_t>("trouble-threshold"));
    m_set.tickPress = mod->getSettingValue<bool>("tick-sound");
    m_set.tickRelease = mod->getSettingValue<bool>("tick-release");
    m_set.tickVolume = static_cast<float>(mod->getSettingValue<double>("tick-volume"));
    m_set.assistFlash = mod->getSettingValue<bool>("assist-flash");
    m_set.offsetTrail = mod->getSettingValue<bool>("offset-trail");
    m_set.countdown = mod->getSettingValue<bool>("countdown");
    m_set.timeline = mod->getSettingValue<bool>("timeline");
    m_set.countIn = mod->getSettingValue<bool>("count-in");
    m_set.autoplay = mod->getSettingValue<bool>("autoplay");
    m_set.autoplaySpeed = static_cast<float>(mod->getSettingValue<double>("autoplay-speed"));
    m_set.sectionStart = static_cast<float>(mod->getSettingValue<int64_t>("section-start"));
    m_set.sectionEnd = static_cast<float>(mod->getSettingValue<int64_t>("section-end"));
    m_set.perfectMs = mod->getSettingValue<double>("judge-perfect-ms");
    m_set.greatMs = std::max(m_set.perfectMs, mod->getSettingValue<double>("judge-great-ms"));
    m_set.goodMs = std::max(m_set.greatMs, mod->getSettingValue<double>("judge-good-ms"));
    this->applyTheme(mod->getSettingValue<std::string>("theme"));
    m_onlyPractice = mod->getSettingValue<bool>("only-practice");
    m_enabled = mod->getSettingValue<bool>("enabled");
    m_assistOn = mod->getSettingValue<bool>("assist-enabled");
    int strength = static_cast<int>(mod->getSettingValue<int64_t>("assist-strength"));
    int relStrength = static_cast<int>(mod->getSettingValue<int64_t>("assist-release-strength"));
    if (relStrength <= 0) relStrength = strength;
    m_assistWindow = assistWindowFor(strength);
    m_releaseWindow = assistWindowFor(relStrength);
    m_assistFollowHold = relStrength >= 5;
    m_assistOffPercent = static_cast<float>(mod->getSettingValue<int64_t>("assist-off-percent"));
}

// ---------------------------------------------------------------------------
// Trouble spots (persisted per level)
// ---------------------------------------------------------------------------

void ClickOverlay::loadTrouble() {
    m_trouble.assign(m_macro.presses.size(), 0);
    auto saved = Mod::get()->getSavedValue<matjson::Value>(troubleKey(m_levelID, m_macro), matjson::Value::array());
    if (!saved.isArray()) return;
    size_t i = 0;
    for (auto const& v : saved) {
        if (i >= m_trouble.size()) break;
        m_trouble[i++] = static_cast<int>(v.asInt().unwrapOr(0));
    }
}

void ClickOverlay::saveTrouble() {
    if (!m_troubleDirty) return;
    m_troubleDirty = false;
    auto arr = matjson::Value::array();
    for (int c : m_trouble) arr.push(matjson::Value(static_cast<std::intmax_t>(c)));
    Mod::get()->setSavedValue(troubleKey(m_levelID, m_macro), arr);
}

void ClickOverlay::resetTrouble() {
    m_trouble.assign(m_macro.presses.size(), 0);
    m_troubleDirty = true;
    this->saveTrouble();
}

void ClickOverlay::noteResult(int pressIndex, bool bad) {
    if (pressIndex < 0 || pressIndex >= static_cast<int>(m_trouble.size())) return;
    int& c = m_trouble[pressIndex];
    c = bad ? std::min(c + 1, 99) : std::max(c - 1, 0);
    m_troubleDirty = true;
}

bool ClickOverlay::isTrouble(int pressIndex) const {
    return m_set.troubleSpots && pressIndex >= 0 && pressIndex < static_cast<int>(m_trouble.size()) &&
        m_trouble[pressIndex] >= m_set.troubleThreshold;
}

void ClickOverlay::applyTheme(std::string const& theme) {
    auto set = [&](ccColor4F bar, ccColor4F p2, ccColor4F line, ccColor4F lane, ccColor4F p2lane, ccColor4F trouble) {
        m_set.barColor = bar; m_set.p2Color = p2; m_set.lineColor = line;
        m_set.laneColor = lane; m_set.p2LaneColor = p2lane; m_set.troubleColor = trouble;
    };
    if (theme == "osu! Pink") {
        set(ccc4f(1.f, 0.4f, 0.75f, 0.32f), ccc4f(0.4f, 0.8f, 1.f, 0.32f), ccc4f(1.f, 1.f, 1.f, 0.7f),
            ccc4f(1.f, 0.45f, 0.8f, 1.f), ccc4f(0.4f, 0.8f, 1.f, 1.f), ccc4f(1.f, 0.9f, 0.2f, 1.f));
    }
    else if (theme == "High Contrast") {
        set(ccc4f(1.f, 1.f, 0.f, 0.5f), ccc4f(0.f, 1.f, 1.f, 0.5f), ccc4f(1.f, 1.f, 1.f, 1.f),
            ccc4f(1.f, 1.f, 0.f, 1.f), ccc4f(0.f, 1.f, 1.f, 1.f), ccc4f(1.f, 0.f, 0.f, 1.f));
    }
    else if (theme == "Ice") {
        set(ccc4f(0.45f, 0.85f, 1.f, 0.32f), ccc4f(0.8f, 0.6f, 1.f, 0.32f), ccc4f(1.f, 1.f, 1.f, 0.7f),
            ccc4f(0.5f, 0.9f, 1.f, 1.f), ccc4f(0.8f, 0.6f, 1.f, 1.f), ccc4f(1.f, 0.35f, 0.35f, 1.f));
    }
    // "Classic" and "Custom" keep the colors from the settings
}

// ---------------------------------------------------------------------------
// Attempt history
// ---------------------------------------------------------------------------

void ClickOverlay::loadHistory() {
    m_history.clear();
    auto saved = Mod::get()->getSavedValue<matjson::Value>(fmt::format("history/{}", m_levelID), matjson::Value::array());
    if (!saved.isArray()) return;
    for (auto const& v : saved) m_history.push_back(v.asDouble().unwrapOr(0.0));
}

void ClickOverlay::saveHistory() {
    auto arr = matjson::Value::array();
    size_t from = m_history.size() > 30 ? m_history.size() - 30 : 0;
    for (size_t i = from; i < m_history.size(); i++) arr.push(matjson::Value(m_history[i]));
    Mod::get()->setSavedValue(fmt::format("history/{}", m_levelID), arr);
}

void ClickOverlay::finishAttempt(bool completed) {
    if (m_stats.total() == 0) return;
    m_history.push_back(m_stats.accuracy());
    if (m_history.size() > 30) m_history.erase(m_history.begin());
    this->saveHistory();

    // progressive assist: a clean run drops the strength by one
    auto mod = Mod::get();
    if (mod->getSettingValue<bool>("progressive-assist") && m_assistOn && m_stats.miss == 0 &&
        m_stats.accuracy() >= static_cast<double>(mod->getSettingValue<int64_t>("progressive-target"))) {
        int64_t st = mod->getSettingValue<int64_t>("assist-strength");
        if (st > 1) {
            mod->setSettingValue<int64_t>("assist-strength", st - 1);
            Notification::create(fmt::format("Clean run! Assist strength is now {}", st - 1), NotificationIcon::Success)->show();
        }
    }
}

// ---------------------------------------------------------------------------
// Count-in, autoplay, section practice, editing
// ---------------------------------------------------------------------------

void ClickOverlay::setupCountIn() {
    m_countIn.clear();
    m_countInCursor = 0;
    if (!m_set.countIn) return;
    auto it = std::lower_bound(m_macro.presses.begin(), m_macro.presses.end(), m_now, [](Press const& p, double t) {
        return p.start < t;
    });
    if (it == m_macro.presses.end()) return;
    double first = it->start;
    if (first - m_now < 1.2) return; // too close for a count-in
    for (int i = 3; i >= 1; i--) m_countIn.push_back(first - 0.5 * i);
}

bool ClickOverlay::autoplayActive(PlayLayer* pl) const {
    return m_set.autoplay && m_enabled && !pl->m_isPlatformer && !m_macro.presses.empty();
}

void ClickOverlay::autoplayTick(PlayLayer* pl, std::function<void(bool, bool)> const& fire) {
    bool active = this->autoplayActive(pl);
    if (!active) {
        for (int p = 0; p < 2; p++) {
            if (m_autoHeld[p]) { m_autoHeld[p] = false; fire(false, p == 1); }
        }
        m_autoResync = true;
        return;
    }
    double now = this->currentTime(pl) + 0.5 / m_macro.framerate;
    auto& presses = m_macro.presses;

    if (m_autoResync) {
        m_autoResync = false;
        m_autoCursor = 0;
        while (m_autoCursor < presses.size() && presses[m_autoCursor].start <= now) m_autoCursor++;
        // inside a hold right now? press it
        for (size_t i = m_autoCursor; i-- > 0;) {
            if (presses[i].start > now - 5.0) {
                int p = presses[i].p2 ? 1 : 0;
                if (presses[i].end > now && !m_autoHeld[p]) { m_autoHeld[p] = true; fire(true, presses[i].p2); }
            }
            else break;
        }
    }

    // releases that are due
    for (size_t i = m_autoCursor; i-- > 0;) {
        if (presses[i].start < now - 5.0) break;
        int p = presses[i].p2 ? 1 : 0;
        if (m_autoHeld[p] && presses[i].end <= now && presses[i].end > now - 0.2) {
            // only release if no later press for this player is already holding
            m_autoHeld[p] = false;
            fire(false, presses[i].p2);
        }
    }
    // presses that are due
    while (m_autoCursor < presses.size() && presses[m_autoCursor].start <= now) {
        auto const& pr = presses[m_autoCursor];
        int p = pr.p2 ? 1 : 0;
        if (pr.end > now) {
            if (!m_autoHeld[p]) { m_autoHeld[p] = true; fire(true, pr.p2); }
        }
        else if (pr.start > now - 0.2) {
            // a whole tap inside one tick: press now, release next tick
            if (!m_autoHeld[p]) { m_autoHeld[p] = true; fire(true, pr.p2); }
        }
        m_autoCursor++;
    }
}

bool ClickOverlay::sectionEndReached(PlayLayer* pl) {
    if (m_set.sectionEnd <= 0.f || m_set.sectionEnd >= 100.f) return false;
    float pct = pl->getCurrentPercent();
    if (pct < m_set.sectionEnd - 2.f) m_sectionArmedEnd = true;
    if (m_sectionArmedEnd && pct >= m_set.sectionEnd) {
        m_sectionArmedEnd = false;
        return true;
    }
    return false;
}

bool ClickOverlay::sectionWantsCheckpoint(PlayLayer* pl) {
    if (m_set.sectionStart <= 0.f || !pl->m_isPracticeMode) return false;
    float pct = pl->getCurrentPercent();
    if (pct < m_set.sectionStart - 2.f) m_sectionArmedStart = true;
    if (m_sectionArmedStart && pct >= m_set.sectionStart) {
        m_sectionArmedStart = false;
        return true;
    }
    return false;
}

void ClickOverlay::shiftMacro(double seconds) {
    for (auto& p : m_macro.presses) { p.start += seconds; p.end += seconds; }
    m_prepared = false;
    m_needResync = true;
    m_autoResync = true;
}

void ClickOverlay::trimMacroBefore(double seconds) {
    auto& v = m_macro.presses;
    v.erase(std::remove_if(v.begin(), v.end(), [&](Press const& p) { return p.end < seconds; }), v.end());
    m_trouble.assign(v.size(), 0);
    m_troubleDirty = true;
    m_lastOffsets.clear();
    m_prepared = false;
    m_needResync = true;
    m_autoResync = true;
}

// ---------------------------------------------------------------------------
// Aim assist
// ---------------------------------------------------------------------------

double ClickOverlay::assistWindowFor(int strength) {
    strength = std::clamp(strength, 1, 10);
    // 1 -> 1 ms, 5 -> ~22 ms, 7 -> 100 ms, 10 -> 1000 ms
    double ms = std::pow(1000.0, (strength - 1) / 9.0);
    return ms / 1000.0;
}

bool ClickOverlay::assistActive(PlayLayer* pl) const {
    if (!m_assistOn || !m_enabled || pl->m_isPlatformer || m_macro.presses.empty()) return false;
    if (m_onlyPractice && !pl->m_isPracticeMode) return false;
    if (m_assistOffPercent > 0.f && pl->getCurrentPercent() >= m_assistOffPercent) return false;
    return true;
}

void ClickOverlay::onAssisted(bool p2) {
    m_stats.assisted++;
    if (!m_set.assistFlash) return;
    m_assistFlashTimer = 0.3f;
    int lane = p2 ? 1 : 0;
    m_receptorFlash[lane] = 0.2f;
    m_flashColor[lane] = ccc4f(1.f, 0.86f, 0.24f, 1.f);
}

bool ClickOverlay::assistOnPress(PlayLayer* pl, bool p2) {
    auto& a = m_assist[p2 ? 1 : 0];
    bool active = this->assistActive(pl);
    if (!active && !a.hasQueued()) return false;
    bool held = a.onPress(m_macro.presses, p2, this->currentTime(pl), active ? m_assistWindow : 0.0);
    if (held) this->onAssisted(p2);
    return held;
}

bool ClickOverlay::assistOnRelease(PlayLayer* pl, bool p2) {
    auto& a = m_assist[p2 ? 1 : 0];
    bool active = this->assistActive(pl);
    if (!active && !a.hasQueued()) return false;
    bool held = a.onRelease(m_macro.presses, this->currentTime(pl), active ? m_releaseWindow : 0.0, active && m_assistFollowHold);
    if (held) this->onAssisted(p2);
    return held;
}

void ClickOverlay::assistTick(PlayLayer* pl, std::function<void(bool, bool)> const& fire) {
    bool active = this->assistActive(pl);
    for (int p = 0; p < 2; p++) {
        auto& a = m_assist[p];
        if (!a.hasQueued()) continue;
        bool p2 = p == 1;
        auto send = [&](bool down) { fire(down, p2); };
        // half a tick of tolerance so a click due on this tick is never pushed to the next one
        if (active) a.tick(this->currentTime(pl) + 0.5 / m_macro.framerate, send);
        else a.flush(send); // assist turned off mid-level: send what's waiting right away
    }
}

// ---------------------------------------------------------------------------
// Time <-> level position
// ---------------------------------------------------------------------------

void ClickOverlay::prepare(PlayLayer* pl) {
    m_prepared = true;
    m_usePositions = false;
    m_timeTable.clear();

    if (pl->m_isPlatformer || m_macro.presses.empty()) return;

    double duration = 0.0;
    for (auto const& p : m_macro.presses) duration = std::max(duration, p.end);
    duration += 2.0;

    // cap at one hour so a broken macro can't freeze the game
    size_t count = static_cast<size_t>(std::min(duration, 3600.0) / m_tableStep) + 2;
    m_timeTable.resize(count);

    float maxX = -1e30f;
    for (size_t i = 0; i < count; i++) {
        float x = pl->posForTime(static_cast<float>(i) * m_tableStep).x;
        if (!std::isfinite(x)) x = maxX;
        x = std::max(x, maxX); // keep the table monotonic
        maxX = x;
        m_timeTable[i] = x;
    }

    m_usePositions = count > 2 && (m_timeTable.back() - m_timeTable.front()) > 100.f;
    if (!m_usePositions) {
        log::info("posForTime gave no usable positions, using time-based scrolling");
        m_timeTable.clear();
        return;
    }

    for (auto& p : m_macro.presses) {
        p.startX = this->xForTime(p.start);
        p.endX = this->xForTime(p.end);
    }
}

float ClickOverlay::xForTime(double t) const {
    if (m_timeTable.size() < 2) return 0.f;
    double fi = t / m_tableStep;
    if (fi <= 0.0) {
        float slope = (m_timeTable[1] - m_timeTable[0]);
        return m_timeTable[0] + static_cast<float>(fi) * slope;
    }
    size_t i = static_cast<size_t>(fi);
    if (i + 1 >= m_timeTable.size()) {
        size_t n = m_timeTable.size();
        float slope = m_timeTable[n - 1] - m_timeTable[n - 2];
        return m_timeTable[n - 1] + static_cast<float>(fi - static_cast<double>(n - 1)) * slope;
    }
    float frac = static_cast<float>(fi - static_cast<double>(i));
    return m_timeTable[i] + (m_timeTable[i + 1] - m_timeTable[i]) * frac;
}

double ClickOverlay::timeForX(float x) const {
    if (m_timeTable.size() < 2) return 0.0;
    auto it = std::lower_bound(m_timeTable.begin(), m_timeTable.end(), x);
    if (it == m_timeTable.begin()) {
        float slope = m_timeTable[1] - m_timeTable[0];
        return slope > 0 ? (x - m_timeTable[0]) / slope * m_tableStep : 0.0;
    }
    if (it == m_timeTable.end()) {
        size_t n = m_timeTable.size();
        float slope = m_timeTable[n - 1] - m_timeTable[n - 2];
        double base = static_cast<double>(n - 1) * m_tableStep;
        return slope > 0 ? base + (x - m_timeTable[n - 1]) / slope * m_tableStep : base;
    }
    size_t i = static_cast<size_t>(it - m_timeTable.begin()) - 1;
    float a = m_timeTable[i], b = m_timeTable[i + 1];
    double frac = b > a ? (x - a) / (b - a) : 0.0;
    return (static_cast<double>(i) + frac) * m_tableStep;
}

double ClickOverlay::currentTime(PlayLayer* pl) {
    // Go by where the player is in the level (matches the bars exactly). The game's
    // tick counter turned out not to track macro frames, so it is no longer used.
    if (m_usePositions && pl->m_player1) {
        return this->timeForX(pl->m_player1->getPositionX());
    }
    return pl->m_gameState.m_levelTime;
}

// ---------------------------------------------------------------------------
// Judgments, stats, ticks
// ---------------------------------------------------------------------------

void ClickOverlay::onReset() {
    this->loadSettings();
    this->saveTrouble();
    m_needResync = true;
    m_held[0] = m_held[1] = false;
    m_lastLevelTime = -1.0;
    m_assist[0].reset();
    m_assist[1].reset();
    m_stats.reset();
    m_autoResync = true;
    // keep last attempt's offsets for the trail, start a fresh set
    if (!m_curOffsets.empty()) m_lastOffsets = m_curOffsets;
    m_curOffsets.assign(m_macro.presses.size(), 9999.f);
    m_sectionArmedEnd = false;
    m_sectionArmedStart = false;
}

void ClickOverlay::resync() {
    m_needResync = false;
    m_missCursor = 0;
    for (size_t i = 0; i < m_macro.presses.size(); i++) {
        auto& p = m_macro.presses[i];
        p.judged = p.start < m_now - kJudgeWindow;
        if (p.judged) m_missCursor = i + 1;
    }
    // ticks: start from the first press that is still ahead
    m_tickCursor = 0;
    while (m_tickCursor < m_macro.presses.size() && m_macro.presses[m_tickCursor].start <= m_now) m_tickCursor++;
    if (m_curOffsets.size() != m_macro.presses.size()) m_curOffsets.assign(m_macro.presses.size(), 9999.f);
    this->setupCountIn();
}

void ClickOverlay::updateMisses() {
    auto& presses = m_macro.presses;
    while (m_missCursor < presses.size() && presses[m_missCursor].start < m_now - kJudgeWindow) {
        auto& p = presses[m_missCursor];
        if (!p.judged) {
            p.judged = true;
            this->recordJudgment("MISS", static_cast<int>(m_missCursor), std::nullopt);
            if (m_set.showJudgments) {
                this->showJudgment("MISS", {255, 60, 60}, std::nullopt, p.p2);
            }
        }
        m_missCursor++;
    }
}

void ClickOverlay::playTicks() {
    static std::string s_tickPath;
    static std::string s_tickUpPath;
    if (s_tickPath.empty()) {
        s_tickPath = utils::string::pathToString(Mod::get()->getResourcesDir() / "tick.ogg");
        s_tickUpPath = utils::string::pathToString(Mod::get()->getResourcesDir() / "tick-up.ogg");
    }
    // count-in beats before the first click
    while (m_countInCursor < m_countIn.size() && m_countIn[m_countInCursor] <= m_now) {
        if (m_countIn[m_countInCursor] > m_now - 0.25) {
            FMODAudioEngine::sharedEngine()->playEffect(s_tickUpPath, 1.f, 0.f, m_set.tickVolume);
        }
        m_countInCursor++;
    }
    if (!m_set.tickPress) return;
    auto& presses = m_macro.presses;
    // presses whose start we just crossed
    int played = 0;
    while (m_tickCursor < presses.size() && presses[m_tickCursor].start <= m_now) {
        if (presses[m_tickCursor].start > m_now - 0.25 && played < 4) {
            FMODAudioEngine::sharedEngine()->playEffect(s_tickPath, 1.f, 0.f, m_set.tickVolume);
            played++;
        }
        m_tickCursor++;
    }
    // releases of holds: scan a little behind for ends we crossed this frame
    if (m_set.tickRelease) {
        static double s_lastNow = -1.0;
        double from = s_lastNow;
        s_lastNow = m_now;
        if (from < 0.0 || from > m_now || m_now - from > 0.25) return;
        auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 3.0, [](Press const& p, double t) {
            return p.start < t;
        });
        for (; it != presses.end() && it->start <= m_now; ++it) {
            bool isHold = it->end - it->start > 0.05;
            if (isHold && it->end > from && it->end <= m_now && played < 6) {
                FMODAudioEngine::sharedEngine()->playEffect(s_tickUpPath, 1.f, 0.f, m_set.tickVolume);
                played++;
            }
        }
    }
}

void ClickOverlay::recordJudgment(char const* text, int pressIndex, std::optional<double> ms) {
    std::string_view t = text;
    if (t == "PERFECT") m_stats.perfect++;
    else if (t == "GREAT") m_stats.great++;
    else if (t == "GOOD") m_stats.good++;
    else if (t == "BAD") m_stats.bad++;
    else if (t == "MISS") m_stats.miss++;
    else if (t == "EXTRA") m_stats.extra++;

    if (ms) {
        m_stats.judged++;
        m_stats.sumAbsMs += std::abs(*ms);
        m_stats.sumMs += *ms;
    }

    bool bad = t == "MISS" || (ms && std::abs(*ms) > m_set.greatMs);
    if (pressIndex >= 0) {
        this->noteResult(pressIndex, bad);
        if (bad) {
            double v = ms ? *ms : 999.0;
            auto& w = m_stats.worst;
            w.push_back({pressIndex, v});
            std::sort(w.begin(), w.end(), [](auto const& a, auto const& b) { return std::abs(a.ms) > std::abs(b.ms); });
            if (w.size() > 5) w.resize(5);
        }
    }
}

std::string ClickOverlay::statsText() const {
    auto const& s = m_stats;
    if (s.total() == 0 && s.extra == 0) return "No clicks judged yet this attempt.";
    std::string out = fmt::format(
        "Accuracy: {:.1f}%\nPerfect {}  Great {}  Good {}  Bad {}  Miss {}  Extra {}\n"
        "Average offset: {:.0f} ms (you tend to be {:+.0f} ms)\n",
        s.accuracy(), s.perfect, s.great, s.good, s.bad, s.miss, s.extra, s.avgAbs(), s.avg()
    );
    if (s.assisted > 0) out += fmt::format("Aim assist corrected {} inputs\n", s.assisted);
    if (!s.worst.empty()) {
        out += "Furthest off:";
        double total = 0.0;
        for (auto const& p : m_macro.presses) total = std::max(total, p.end);
        for (auto const& w : s.worst) {
            if (w.press < 0 || w.press >= static_cast<int>(m_macro.presses.size())) continue;
            double t = m_macro.presses[w.press].start;
            int pct = total > 0 ? static_cast<int>(t / total * 100.0) : 0;
            if (w.ms >= 999.0) out += fmt::format("\n  {}%  ({:.1f}s)  missed", pct, t);
            else out += fmt::format("\n  {}%  ({:.1f}s)  {:+.0f} ms", pct, t, w.ms);
        }
    }
    return out;
}

void ClickOverlay::onPlayerInput(PlayLayer* pl, bool down, bool p2) {
    m_held[p2 ? 1 : 0] = down;
    if (!down || !this->isVisible()) return;

    double now = this->currentTime(pl);
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), now - kJudgeWindow, [](Press const& p, double t) {
        return p.start < t;
    });

    Press* best = nullptr;
    for (; it != presses.end() && it->start <= now + kJudgeWindow; ++it) {
        if (it->p2 != p2 || it->judged) continue;
        if (!best || std::abs(it->start - now) < std::abs(best->start - now)) best = &*it;
    }

    if (!best) {
        this->recordJudgment("EXTRA", -1, std::nullopt);
        if (m_set.showJudgments) this->showJudgment("EXTRA", {255, 140, 60}, std::nullopt, p2);
        return;
    }

    best->judged = true;
    int idx = static_cast<int>(best - presses.data());
    double ms = (now - best->start) * 1000.0;
    double a = std::abs(ms);
    char const* text;
    ccColor3B color;
    if (a <= m_set.perfectMs) { text = "PERFECT"; color = {90, 240, 255}; }
    else if (a <= m_set.greatMs) { text = "GREAT"; color = {60, 255, 120}; }
    else if (a <= m_set.goodMs) { text = "GOOD"; color = {255, 230, 80}; }
    else { text = "BAD"; color = {255, 140, 60}; }
    if (idx >= 0 && idx < static_cast<int>(m_curOffsets.size())) m_curOffsets[idx] = static_cast<float>(ms);
    this->recordJudgment(text, idx, ms);
    if (m_set.showJudgments) this->showJudgment(text, color, ms, p2);
}

void ClickOverlay::showJudgment(char const* text, ccColor3B color, std::optional<double> offsetMs, bool p2) {
    m_judgeLabel->setString(text);
    m_judgeLabel->setColor(color);
    if (offsetMs) {
        m_offsetLabel->setString(fmt::format("{:+.0f}ms", *offsetMs).c_str());
    }
    else {
        m_offsetLabel->setString("");
    }
    m_judgeTimer = kJudgeShowTime;

    int lane = p2 ? 1 : 0;
    m_receptorFlash[lane] = 0.15f;
    m_flashColor[lane] = ccc4FFromccc3B(color);
}

bool ClickOverlay::pastHidePercent(PlayLayer* pl) const {
    return m_set.hideAfterPercent > 0.f && pl->getCurrentPercent() >= m_set.hideAfterPercent;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ClickOverlay::refresh(PlayLayer* pl, float dt) {
    bool visible = m_enabled && (!m_onlyPractice || pl->m_isPracticeMode);
    if (!m_prepared) this->prepare(pl);

    m_now = this->currentTime(pl);
    if (m_needResync) this->resync();
    this->updateMisses();
    if (visible) this->playTicks();

    bool hidden = this->pastHidePercent(pl);
    this->setVisible(visible && !hidden);
    if (!visible || hidden) return;

    m_barDraw->clear();
    m_laneDraw->clear();
    m_timelineDraw->clear();

    if (m_set.showBars || m_set.showHitLine) this->drawBars(pl);
    if (m_set.timeline) this->drawTimeline(pl);

    // countdown to the next click
    if (m_set.countdown) {
        auto it = std::lower_bound(m_macro.presses.begin(), m_macro.presses.end(), m_now, [](Press const& p, double t) {
            return p.start < t;
        });
        double gap = it != m_macro.presses.end() ? it->start - m_now : -1.0;
        if (gap >= 1.0) {
            m_countdownLabel->setString(fmt::format("{:.1f}", gap).c_str());
            m_countdownLabel->setOpacity(200);
        }
        else {
            m_countdownLabel->setOpacity(0);
        }
    }
    else {
        m_countdownLabel->setOpacity(0);
    }

    auto win = CCDirector::get()->getWinSize();
    float s = m_set.laneScale;
    if (m_set.showLane) {
        if (m_set.laneHorizontal) {
            float y0 = 42.f * s;
            this->drawLaneHorizontal(0, y0);
            if (m_macro.hasP2) this->drawLaneHorizontal(1, y0 + (26.f + m_set.laneGap) * s);
        }
        else {
            float x0 = m_set.laneRight ? win.width - 26.f * s : 26.f * s;
            this->drawLane(0, x0);
            if (m_macro.hasP2) {
                float gap = (36.f + m_set.laneGap) * s;
                this->drawLane(1, x0 + (m_set.laneRight ? -gap : gap));
            }
        }
    }
    else {
        m_judgeLabel->setOpacity(0);
        m_offsetLabel->setOpacity(0);
    }

    if (m_judgeTimer > 0.f) m_judgeTimer -= dt;
    if (m_set.showLane) {
        float alpha = std::clamp(m_judgeTimer / 0.25f, 0.f, 1.f);
        m_judgeLabel->setOpacity(static_cast<GLubyte>(alpha * 255));
        m_offsetLabel->setOpacity(static_cast<GLubyte>(alpha * 220));
    }
    if (m_assistFlashTimer > 0.f) m_assistFlashTimer -= dt;
    {
        float alpha = std::clamp(m_assistFlashTimer / 0.3f, 0.f, 1.f);
        m_assistLabel->setOpacity(static_cast<GLubyte>(alpha * 255));
        if (!m_set.showLane) m_assistLabel->setPosition({win.width / 2.f, win.height - 40.f});
    }
    for (auto& f : m_receptorFlash) f = std::max(0.f, f - dt);
}

void ClickOverlay::drawBars(PlayLayer* pl) {
    auto win = CCDirector::get()->getWinSize();
    auto layer = pl->m_objectLayer;
    if (!layer || !pl->m_player1) return;

    auto toScreen = [&](CCPoint world) {
        return this->convertToNodeSpace(layer->convertToWorldSpace(world));
    };

    CCPoint p1World = pl->m_player1->getPosition();
    CCPoint p1 = toScreen(p1World);
    CCPoint p2 = pl->m_player2 ? toScreen(pl->m_player2->getPosition()) : p1;
    float hitX = p1.x;

    // screenX = a * levelX + b (camera zoom / mirror portals handled by a's sign)
    float b = toScreen({0.f, p1World.y}).x;
    float a = toScreen({1.f, p1World.y}).x - b;
    if (std::abs(a) < 1e-4f) return;
    float dir = a > 0 ? 1.f : -1.f;

    if (m_set.showBars && !m_macro.presses.empty()) {
        // time-based fallback: estimate scroll speed from the player's movement
        if (!m_usePositions) {
            double lt = pl->m_gameState.m_levelTime;
            if (m_lastLevelTime >= 0.0 && lt > m_lastLevelTime) {
                float v = static_cast<float>((p1World.x - m_lastPlayerX) / (lt - m_lastLevelTime));
                if (v > 0.f && v < 5000.f) m_pxPerSecond = m_pxPerSecond <= 0.f ? v : m_pxPerSecond * 0.9f + v * 0.1f;
            }
            m_lastLevelTime = lt;
            m_lastPlayerX = p1World.x;
        }

        auto screenStart = [&](Press const& p) {
            return m_usePositions ? a * p.startX + b : hitX + dir * static_cast<float>(p.start - m_now) * m_pxPerSecond * std::abs(a);
        };
        auto screenEnd = [&](Press const& p) {
            return m_usePositions ? a * p.endX + b : hitX + dir * static_cast<float>(p.end - m_now) * m_pxPerSecond * std::abs(a);
        };

        // Only look at presses that can be on screen: from a few seconds back to the right edge
        auto& presses = m_macro.presses;
        auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 20.0, [](Press const& p, double t) {
            return p.start < t;
        });

        float markerSize = 2.6f;
        int drawn = 0;
        for (; it != presses.end() && drawn < 1500; ++it) {
            auto const& p = *it;
            float xs = screenStart(p);
            float xe = screenEnd(p);
            float ahead = (xs - hitX) * dir; // > 0 means still in front of the player

            // past the right edge of the screen? then every later press is too
            if (dir > 0 ? xs > win.width + 10.f : xs < -10.f) break;

            float lo = std::min(xs, xe), hi = std::max(xs, xe);
            if (hi < -10.f || lo > win.width + 10.f) continue;

            bool startClipped = false;
            if (m_set.hidePassed) {
                float endAhead = (xe - hitX) * dir;
                if (endAhead < 0.f) continue;
                if (ahead < 0.f) {
                    startClipped = true;
                    xs = hitX;
                    lo = std::min(xs, xe);
                    hi = std::max(xs, xe);
                }
            }

            int idx = static_cast<int>(&p - presses.data());
            auto color = this->isTrouble(idx) ? withAlpha(m_set.troubleColor, (p.p2 ? m_set.p2Color : m_set.barColor).a) : (p.p2 ? m_set.p2Color : m_set.barColor);
            auto edge = withAlpha(color, std::min(1.f, color.a * 2.5f + 0.35f));
            float y = p.p2 ? p2.y : p1.y;

            if (hi - lo >= 1.5f) {
                m_barDraw->drawRect(ccp(lo, 0.f), ccp(hi, win.height), color, 0.f, withAlpha(color, 0.f));
            }
            if (!startClipped) {
                m_barDraw->drawSegment(ccp(xs, 0.f), ccp(xs, win.height), 0.55f, edge);
            }
            if (hi - lo >= 1.5f) {
                m_barDraw->drawSegment(ccp(xe, 0.f), ccp(xe, win.height), 0.55f, edge);
            }

            if (m_set.showMarkers) {
                auto fill = ccc4f(0.f, 0.f, 0.f, 0.55f);
                if (!startClipped) {
                    m_barDraw->drawRect(ccp(xs - markerSize, y - markerSize), ccp(xs + markerSize, y + markerSize), fill, 0.7f, edge);
                }
                if (hi - lo >= 1.5f) {
                    m_barDraw->drawRect(ccp(xe - markerSize, y - markerSize), ccp(xe + markerSize, y + markerSize), fill, 0.7f, edge);
                }
            }
            drawn++;
        }
    }

    if (m_set.showHitLine) {
        m_barDraw->drawSegment(ccp(hitX, 0.f), ccp(hitX, win.height), 0.7f, m_set.lineColor);
    }
}

void ClickOverlay::drawLane(int lane, float cx) {
    auto win = CCDirector::get()->getWinSize();
    float s = m_set.laneScale;

    float bottomY = win.height * 0.2f;
    float topY = std::min(win.height - 20.f, bottomY + 170.f * s);
    float bw = 13.f * s; // half width at the bottom
    float tw = 6.f * s;  // half width at the top (perspective)

    auto halfW = [&](float y) {
        float f = std::clamp((y - bottomY) / (topY - bottomY), 0.f, 1.f);
        return bw + (tw - bw) * f;
    };

    bool p2 = lane == 1;
    auto color = p2 ? m_set.p2LaneColor : m_set.laneColor;
    auto dim = withAlpha(color, color.a * 0.18f);

    // lane background + rails
    CCPoint bg[4] = {ccp(cx - bw, bottomY), ccp(cx + bw, bottomY), ccp(cx + tw, topY), ccp(cx - tw, topY)};
    m_laneDraw->drawPolygon(bg, 4, withAlpha(ccc4f(0.f, 0.f, 0.f, 1.f), 0.45f), 0.f, withAlpha(color, 0.f));
    m_laneDraw->drawSegment(ccp(cx - bw, bottomY), ccp(cx - tw, topY), 0.8f * s, color);
    m_laneDraw->drawSegment(ccp(cx + bw, bottomY), ccp(cx + tw, topY), 0.8f * s, color);
    m_laneDraw->drawSegment(ccp(cx - tw, topY), ccp(cx + tw, topY), 0.5f * s, dim);

    // notes
    float speed = (topY - bottomY) / std::max(0.2f, m_set.laneLookahead);
    double visibleSec = m_set.laneLookahead;
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 30.0, [](Press const& p, double t) {
        return p.start < t;
    });
    float inset = 1.6f * s;
    for (; it != presses.end() && it->start <= m_now + visibleSec; ++it) {
        auto const& p = *it;
        if (p.p2 != p2) continue;
        int idx = static_cast<int>(it - presses.begin());

        float y0 = bottomY + static_cast<float>(p.start - m_now) * speed;
        float y1 = bottomY + static_cast<float>(p.end - m_now) * speed;
        bool isTap = (y1 - y0) < 4.f * s;
        if (isTap) y1 = y0 + 4.f * s;
        if (y1 < bottomY) continue;
        if (isTap && y0 < bottomY - 2.f) continue;

        y0 = std::max(y0, bottomY);
        y1 = std::min(y1, topY);
        if (y0 >= topY) continue;

        auto noteBase = this->isTrouble(idx) ? m_set.troubleColor : color;
        CCPoint pts[4] = {
            ccp(cx - halfW(y0) + inset, y0),
            ccp(cx + halfW(y0) - inset, y0),
            ccp(cx + halfW(y1) - inset, y1),
            ccp(cx - halfW(y1) + inset, y1),
        };
        auto noteColor = withAlpha(noteBase, isTap ? 0.95f : 0.65f);
        m_laneDraw->drawPolygon(pts, 4, noteColor, 0.f, withAlpha(noteBase, 0.f));

        if (!isTap && y0 > bottomY + 0.5f) {
            float capTop = std::min(y0 + 3.f * s, y1);
            CCPoint cap[4] = {
                ccp(cx - halfW(y0) + inset, y0),
                ccp(cx + halfW(y0) - inset, y0),
                ccp(cx + halfW(capTop) - inset, capTop),
                ccp(cx - halfW(capTop) + inset, capTop),
            };
            m_laneDraw->drawPolygon(cap, 4, withAlpha(noteBase, 1.f), 0.f, withAlpha(noteBase, 0.f));
        }
    }

    // receptor + chevrons light up while you hold
    auto white = ccc4f(1.f, 1.f, 1.f, 1.f);
    auto receptor = m_held[lane] ? white : color;
    if (m_receptorFlash[lane] > 0.f) {
        receptor = mix(receptor, m_flashColor[lane], std::clamp(m_receptorFlash[lane] / 0.15f, 0.f, 1.f));
    }
    m_laneDraw->drawRect(ccp(cx - bw, bottomY - 2.2f * s), ccp(cx + bw, bottomY + 1.2f * s), receptor, 0.f, withAlpha(color, 0.f));

    float cw = 4.5f * s, ch = 4.5f * s, gap = 3.f * s;
    float lx = cx - bw - gap;
    float rx = cx + bw + gap;
    float cy = bottomY - 0.5f * s;
    float lw = 0.9f * s;
    m_laneDraw->drawSegment(ccp(lx - cw, cy + ch), ccp(lx, cy), lw, receptor);
    m_laneDraw->drawSegment(ccp(lx, cy), ccp(lx - cw, cy - ch), lw, receptor);
    m_laneDraw->drawSegment(ccp(rx + cw, cy + ch), ccp(rx, cy), lw, receptor);
    m_laneDraw->drawSegment(ccp(rx, cy), ccp(rx + cw, cy - ch), lw, receptor);

    if (m_set.offsetTrail) this->drawTrailVertical(lane, cx, bottomY, topY, speed);

    if (lane == 0) {
        m_judgeLabel->setPosition({cx, bottomY - 11.f * s});
        m_judgeLabel->setScale(0.28f * s);
        m_offsetLabel->setPosition({cx, bottomY - 19.f * s});
        m_offsetLabel->setScale(0.45f * s);
        m_assistLabel->setPosition({cx, topY + 8.f * s});
        m_assistLabel->setScale(0.22f * s);
        m_countdownLabel->setPosition({cx, topY + 18.f * s});
        m_countdownLabel->setScale(0.3f * s);
    }
}

// Dots showing how early/late you were on each note last attempt (and this one)
void ClickOverlay::drawTrailVertical(int lane, float cx, float bottomY, float topY, float speed) {
    float s = m_set.laneScale;
    bool p2 = lane == 1;
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 1.0, [](Press const& p, double t) {
        return p.start < t;
    });
    for (; it != presses.end() && it->start <= m_now + m_set.laneLookahead; ++it) {
        if (it->p2 != p2) continue;
        int idx = static_cast<int>(it - presses.begin());
        float y = bottomY + static_cast<float>(it->start - m_now) * speed;
        if (y < bottomY - 4.f || y > topY) continue;
        auto draw = [&](std::vector<float> const& offs, ccColor4F col) {
            if (idx >= static_cast<int>(offs.size()) || offs[idx] > 5000.f) return;
            float ms = offs[idx];
            float dy = std::clamp(ms / 1000.f * speed, -14.f * s, 14.f * s); // late = above the note
            float dx = std::clamp(ms / 8.f, -12.f, 12.f) * s;                   // and shifted sideways so it's readable
            m_laneDraw->drawDot(ccp(cx + dx, y + dy), 1.8f * s, col);
        };
        draw(m_lastOffsets, ccc4f(1.f, 1.f, 1.f, 0.45f));
        draw(m_curOffsets, ccc4f(1.f, 0.9f, 0.3f, 0.95f));
    }
    // center guide
    m_laneDraw->drawSegment(ccp(cx, bottomY), ccp(cx, topY), 0.3f, ccc4f(1.f, 1.f, 1.f, 0.12f));
}

void ClickOverlay::drawTrailHorizontal(int lane, float cy, float leftX, float rightX, float speed) {
    float s = m_set.laneScale;
    bool p2 = lane == 1;
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 1.0, [](Press const& p, double t) {
        return p.start < t;
    });
    for (; it != presses.end() && it->start <= m_now + m_set.laneLookahead; ++it) {
        if (it->p2 != p2) continue;
        int idx = static_cast<int>(it - presses.begin());
        float x = leftX + static_cast<float>(it->start - m_now) * speed;
        if (x < leftX - 4.f || x > rightX) continue;
        auto draw = [&](std::vector<float> const& offs, ccColor4F col) {
            if (idx >= static_cast<int>(offs.size()) || offs[idx] > 5000.f) return;
            float ms = offs[idx];
            float dx = std::clamp(ms / 1000.f * speed, -14.f * s, 14.f * s);
            float dy = std::clamp(ms / 8.f, -8.f, 8.f) * s;
            m_laneDraw->drawDot(ccp(x + dx, cy + dy), 1.8f * s, col);
        };
        draw(m_lastOffsets, ccc4f(1.f, 1.f, 1.f, 0.45f));
        draw(m_curOffsets, ccc4f(1.f, 0.9f, 0.3f, 0.95f));
    }
}

// Thin bar across the top: every click as a tick, trouble spots red, and where you are
void ClickOverlay::drawTimeline(PlayLayer* pl) {
    auto win = CCDirector::get()->getWinSize();
    float y = win.height - 22.f;
    float x0 = 60.f, x1 = win.width - 60.f;
    double total = 0.0;
    for (auto const& p : m_macro.presses) total = std::max(total, p.end);
    if (total <= 0.0) return;

    m_timelineDraw->drawRect(ccp(x0, y - 3.f), ccp(x1, y + 3.f), ccc4f(0.f, 0.f, 0.f, 0.5f), 0.f, ccc4f(0, 0, 0, 0));
    for (size_t i = 0; i < m_macro.presses.size(); i++) {
        auto const& p = m_macro.presses[i];
        float xs = x0 + static_cast<float>(p.start / total) * (x1 - x0);
        float xe = x0 + static_cast<float>(p.end / total) * (x1 - x0);
        auto c = this->isTrouble(static_cast<int>(i)) ? m_set.troubleColor : (p.p2 ? m_set.p2LaneColor : m_set.laneColor);
        c = withAlpha(c, p.start < m_now ? 0.35f : 0.9f);
        m_timelineDraw->drawRect(ccp(xs, y - 2.5f), ccp(std::max(xe, xs + 0.8f), y + 2.5f), c, 0.f, ccc4f(0, 0, 0, 0));
    }
    float px = x0 + static_cast<float>(std::clamp(m_now / total, 0.0, 1.0)) * (x1 - x0);
    m_timelineDraw->drawSegment(ccp(px, y - 5.f), ccp(px, y + 5.f), 0.8f, m_set.lineColor);

    // section markers
    if (m_set.sectionStart > 0.f) {
        float sx = x0 + m_set.sectionStart / 100.f * (x1 - x0);
        m_timelineDraw->drawSegment(ccp(sx, y - 5.f), ccp(sx, y + 5.f), 0.6f, ccc4f(1.f, 0.9f, 0.2f, 0.9f));
    }
    if (m_set.sectionEnd > 0.f && m_set.sectionEnd < 100.f) {
        float sx = x0 + m_set.sectionEnd / 100.f * (x1 - x0);
        m_timelineDraw->drawSegment(ccp(sx, y - 5.f), ccp(sx, y + 5.f), 0.6f, ccc4f(1.f, 0.9f, 0.2f, 0.9f));
    }
}

// Guitar-Hero style: notes come from the right and hit a receptor near the left
void ClickOverlay::drawLaneHorizontal(int lane, float cy) {
    auto win = CCDirector::get()->getWinSize();
    float s = m_set.laneScale;

    float leftX = 60.f * s;                                        // receptor
    float rightX = std::min(win.width - 10.f, leftX + 260.f * s);  // far end
    float hh = 10.f * s; // half height

    bool p2 = lane == 1;
    auto color = p2 ? m_set.p2LaneColor : m_set.laneColor;
    auto dim = withAlpha(color, color.a * 0.18f);

    m_laneDraw->drawRect(ccp(leftX - 6.f * s, cy - hh), ccp(rightX, cy + hh), withAlpha(ccc4f(0.f, 0.f, 0.f, 1.f), 0.45f), 0.f, withAlpha(color, 0.f));
    m_laneDraw->drawSegment(ccp(leftX - 6.f * s, cy + hh), ccp(rightX, cy + hh), 0.7f * s, color);
    m_laneDraw->drawSegment(ccp(leftX - 6.f * s, cy - hh), ccp(rightX, cy - hh), 0.7f * s, color);
    m_laneDraw->drawSegment(ccp(rightX, cy - hh), ccp(rightX, cy + hh), 0.5f * s, dim);

    float speed = (rightX - leftX) / std::max(0.2f, m_set.laneLookahead);
    double visibleSec = m_set.laneLookahead;
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 30.0, [](Press const& p, double t) {
        return p.start < t;
    });
    float inset = 2.f * s;
    for (; it != presses.end() && it->start <= m_now + visibleSec; ++it) {
        auto const& p = *it;
        if (p.p2 != p2) continue;
        int idx = static_cast<int>(it - presses.begin());

        float x0 = leftX + static_cast<float>(p.start - m_now) * speed;
        float x1 = leftX + static_cast<float>(p.end - m_now) * speed;
        bool isTap = (x1 - x0) < 4.f * s;
        if (isTap) x1 = x0 + 4.f * s;
        if (x1 < leftX) continue;
        if (isTap && x0 < leftX - 2.f) continue;
        x0 = std::max(x0, leftX);
        x1 = std::min(x1, rightX);
        if (x0 >= rightX) continue;

        auto noteBase = this->isTrouble(idx) ? m_set.troubleColor : color;
        m_laneDraw->drawRect(ccp(x0, cy - hh + inset), ccp(x1, cy + hh - inset), withAlpha(noteBase, isTap ? 0.95f : 0.65f), 0.f, withAlpha(noteBase, 0.f));
        if (!isTap && x0 > leftX + 0.5f) {
            m_laneDraw->drawRect(ccp(x0, cy - hh + inset), ccp(std::min(x0 + 3.f * s, x1), cy + hh - inset), withAlpha(noteBase, 1.f), 0.f, withAlpha(noteBase, 0.f));
        }
    }

    auto white = ccc4f(1.f, 1.f, 1.f, 1.f);
    auto receptor = m_held[lane] ? white : color;
    if (m_receptorFlash[lane] > 0.f) {
        receptor = mix(receptor, m_flashColor[lane], std::clamp(m_receptorFlash[lane] / 0.15f, 0.f, 1.f));
    }
    m_laneDraw->drawRect(ccp(leftX - 2.2f * s, cy - hh), ccp(leftX + 1.2f * s, cy + hh), receptor, 0.f, withAlpha(color, 0.f));

    // "^" above and "v" below the receptor
    float cw = 4.5f * s, gap = 3.f * s, lw = 0.9f * s;
    float ty = cy + hh + gap, by = cy - hh - gap;
    m_laneDraw->drawSegment(ccp(leftX - cw, ty + cw), ccp(leftX, ty), lw, receptor);
    m_laneDraw->drawSegment(ccp(leftX, ty), ccp(leftX + cw, ty + cw), lw, receptor);
    m_laneDraw->drawSegment(ccp(leftX - cw, by - cw), ccp(leftX, by), lw, receptor);
    m_laneDraw->drawSegment(ccp(leftX, by), ccp(leftX + cw, by - cw), lw, receptor);

    if (m_set.offsetTrail) this->drawTrailHorizontal(lane, cy, leftX, rightX, speed);

    if (lane == 0) {
        m_judgeLabel->setPosition({leftX - 30.f * s, cy + 4.f * s});
        m_judgeLabel->setScale(0.28f * s);
        m_offsetLabel->setPosition({leftX - 30.f * s, cy - 6.f * s});
        m_offsetLabel->setScale(0.45f * s);
        m_assistLabel->setPosition({leftX - 30.f * s, cy + hh + 14.f * s});
        m_assistLabel->setScale(0.22f * s);
        m_countdownLabel->setPosition({rightX + 14.f * s, cy});
        m_countdownLabel->setScale(0.3f * s);
    }
}
