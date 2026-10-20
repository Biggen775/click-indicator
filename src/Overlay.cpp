#include "Overlay.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    constexpr double kJudgeWindow = 0.15;  // seconds: max distance to count a click as "for" a macro press
    constexpr float kJudgeShowTime = 0.7f; // seconds the judgment text stays up

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
}

ClickOverlay* ClickOverlay::create(Macro macro) {
    auto ret = new ClickOverlay();
    if (ret->init(std::move(macro))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ClickOverlay::init(Macro macro) {
    if (!CCNode::init()) return false;
    m_macro = std::move(macro);

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

    this->loadSettings();

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
    m_set.showJudgments = mod->getSettingValue<bool>("show-judgments");
    m_set.laneSpeed = static_cast<float>(mod->getSettingValue<double>("lane-speed"));
    m_set.laneScale = static_cast<float>(mod->getSettingValue<double>("lane-scale"));
    m_set.offset = static_cast<float>(mod->getSettingValue<double>("offset-ms"));
    m_set.barColor = toColor4F(mod->getSettingValue<ccColor4B>("bar-color"));
    m_set.p2Color = toColor4F(mod->getSettingValue<ccColor4B>("p2-color"));
    m_set.lineColor = toColor4F(mod->getSettingValue<ccColor4B>("line-color"));
    m_set.laneColor = toColor4F(mod->getSettingValue<ccColor4B>("lane-color"));
    m_onlyPractice = mod->getSettingValue<bool>("only-practice");
    m_enabled = mod->getSettingValue<bool>("enabled");
    m_assistOn = mod->getSettingValue<bool>("assist-enabled");
    m_assistWindow = assistWindowFor(static_cast<int>(mod->getSettingValue<int64_t>("assist-strength")));
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
    return m_assistOn && m_enabled && !pl->m_isPlatformer && !m_macro.presses.empty() &&
        (!m_onlyPractice || pl->m_isPracticeMode);
}

bool ClickOverlay::assistOnPress(PlayLayer* pl, bool p2) {
    auto& a = m_assist[p2 ? 1 : 0];
    if (!this->assistActive(pl)) {
        a = AssistState{a.pressAt, a.releaseAt, -1, a.lastUsed};
        return false;
    }

    // Pressed again while a held-back release is waiting: the game is still holding, keep it that way
    if (a.releaseAt) {
        a.releaseAt.reset();
        return true;
    }
    if (a.pressAt) return true;

    double now = this->currentTime(pl);
    auto const& presses = m_macro.presses;

    // Nearest unused macro click for this player inside the window
    int best = -1;
    double bestDist = 0.0;
    auto it = std::lower_bound(presses.begin(), presses.end(), now - m_assistWindow, [](Press const& p, double t) {
        return p.start < t;
    });
    for (; it != presses.end() && it->start <= now + m_assistWindow; ++it) {
        int idx = static_cast<int>(it - presses.begin());
        if (it->p2 != p2 || idx <= a.lastUsed) continue;
        double dist = std::abs(it->start - now);
        if (best < 0 || dist < bestDist) {
            best = idx;
            bestDist = dist;
        }
    }

    if (best < 0) {
        a.active = -1;
        return false; // nothing to line up with: normal click
    }

    a.active = best;
    a.lastUsed = best;
    double start = presses[best].start;
    if (start - now > 0.002) {
        a.pressAt = start; // early: hold the click until the macro's moment
        return true;
    }
    return false; // on time or late: let it through now
}

bool ClickOverlay::assistOnRelease(PlayLayer* pl, bool p2) {
    auto& a = m_assist[p2 ? 1 : 0];
    if (!this->assistActive(pl) || a.active < 0 || a.active >= static_cast<int>(m_macro.presses.size())) {
        a.active = -1;
        return false;
    }

    double end = m_macro.presses[a.active].end;

    // The press itself hasn't fired yet: release right after the macro's release
    if (a.pressAt) {
        a.releaseAt = std::max(end, *a.pressAt + 0.001);
        return true;
    }

    double now = this->currentTime(pl);
    if (end - now > 0.002 && end - now <= m_assistWindow) {
        a.releaseAt = end; // released early: hold it until the macro lets go
        return true;
    }
    a.active = -1;
    return false;
}

void ClickOverlay::assistTick(PlayLayer* pl, std::function<void(bool, bool)> const& fire) {
    bool active = this->assistActive(pl);
    for (int p = 0; p < 2; p++) {
        auto& a = m_assist[p];
        if (!a.pressAt && !a.releaseAt) continue;

        double now = active ? this->currentTime(pl) : 1e300; // assist turned off: flush everything now
        if (a.pressAt && now >= *a.pressAt) {
            a.pressAt.reset();
            fire(true, p == 1);
        }
        if (!a.pressAt && a.releaseAt && now >= *a.releaseAt) {
            a.releaseAt.reset();
            a.active = -1;
            fire(false, p == 1);
        }
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
    if (m_usePositions && pl->m_player1) {
        return this->timeForX(pl->m_player1->getPositionX());
    }
    return pl->m_gameState.m_levelTime;
}

// ---------------------------------------------------------------------------
// Judgments
// ---------------------------------------------------------------------------

void ClickOverlay::onReset() {
    this->loadSettings();
    m_needResync = true;
    m_held[0] = m_held[1] = false;
    m_lastLevelTime = -1.0;
    m_assist[0] = AssistState{};
    m_assist[1] = AssistState{};
}

void ClickOverlay::resync() {
    m_needResync = false;
    m_missCursor = 0;
    for (size_t i = 0; i < m_macro.presses.size(); i++) {
        auto& p = m_macro.presses[i];
        p.judged = p.start < m_now - kJudgeWindow;
        if (p.judged) m_missCursor = i + 1;
    }
}

void ClickOverlay::updateMisses() {
    auto& presses = m_macro.presses;
    while (m_missCursor < presses.size() && presses[m_missCursor].start < m_now - kJudgeWindow) {
        auto& p = presses[m_missCursor];
        if (!p.judged) {
            p.judged = true;
            if (m_set.showJudgments) {
                this->showJudgment("MISS", {255, 60, 60}, std::nullopt, p.p2);
            }
        }
        m_missCursor++;
    }
}

void ClickOverlay::onPlayerInput(PlayLayer* pl, bool down, bool p2) {
    m_held[p2 ? 1 : 0] = down;
    if (!down || !m_set.showJudgments || !this->isVisible()) return;

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
        this->showJudgment("EXTRA", {255, 140, 60}, std::nullopt, p2);
        return;
    }

    best->judged = true;
    double ms = (now - best->start) * 1000.0;
    double a = std::abs(ms);
    if (a <= 20.0) this->showJudgment("PERFECT", {90, 240, 255}, ms, p2);
    else if (a <= 45.0) this->showJudgment("GREAT", {60, 255, 120}, ms, p2);
    else if (a <= 90.0) this->showJudgment("GOOD", {255, 230, 80}, ms, p2);
    else this->showJudgment("BAD", {255, 140, 60}, ms, p2);
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

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ClickOverlay::refresh(PlayLayer* pl, float dt) {
    bool visible = m_enabled && (!m_onlyPractice || pl->m_isPracticeMode);
    this->setVisible(visible);
    if (!visible) return;

    if (!m_prepared) this->prepare(pl);

    m_now = this->currentTime(pl);
    if (m_needResync) this->resync();
    this->updateMisses();

    m_barDraw->clear();
    m_laneDraw->clear();

    if (m_set.showBars || m_set.showHitLine) this->drawBars(pl);

    if (m_set.showLane) {
        auto win = CCDirector::get()->getWinSize();
        float s = m_set.laneScale;
        float x0 = m_set.laneRight ? win.width - 26.f * s : 26.f * s;
        this->drawLane(0, x0);
        if (m_macro.hasP2) {
            this->drawLane(1, x0 + (m_set.laneRight ? -36.f : 36.f) * s);
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

            auto color = p.p2 ? m_set.p2Color : m_set.barColor;
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

    auto color = m_set.laneColor;
    auto dim = withAlpha(color, color.a * 0.18f);

    // lane background + rails
    CCPoint bg[4] = {ccp(cx - bw, bottomY), ccp(cx + bw, bottomY), ccp(cx + tw, topY), ccp(cx - tw, topY)};
    m_laneDraw->drawPolygon(bg, 4, withAlpha(ccc4f(0.f, 0.f, 0.f, 1.f), 0.45f), 0.f, withAlpha(color, 0.f));
    m_laneDraw->drawSegment(ccp(cx - bw, bottomY), ccp(cx - tw, topY), 0.8f * s, color);
    m_laneDraw->drawSegment(ccp(cx + bw, bottomY), ccp(cx + tw, topY), 0.8f * s, color);
    m_laneDraw->drawSegment(ccp(cx - tw, topY), ccp(cx + tw, topY), 0.5f * s, dim);

    // notes
    bool p2 = lane == 1;
    float speed = m_set.laneSpeed * s;
    double visibleSec = (topY - bottomY) / speed;
    auto& presses = m_macro.presses;
    auto it = std::lower_bound(presses.begin(), presses.end(), m_now - 30.0, [](Press const& p, double t) {
        return p.start < t;
    });
    float inset = 1.6f * s;
    for (; it != presses.end() && it->start <= m_now + visibleSec; ++it) {
        auto const& p = *it;
        if (p.p2 != p2) continue;

        float y0 = bottomY + static_cast<float>(p.start - m_now) * speed;
        float y1 = bottomY + static_cast<float>(p.end - m_now) * speed;
        bool isTap = (y1 - y0) < 4.f * s;
        if (isTap) y1 = y0 + 4.f * s;
        if (y1 < bottomY) continue;               // fully passed
        if (isTap && y0 < bottomY - 2.f) continue; // tap already through the receptor

        y0 = std::max(y0, bottomY);
        y1 = std::min(y1, topY);
        if (y0 >= topY) continue;

        CCPoint pts[4] = {
            ccp(cx - halfW(y0) + inset, y0),
            ccp(cx + halfW(y0) - inset, y0),
            ccp(cx + halfW(y1) - inset, y1),
            ccp(cx - halfW(y1) + inset, y1),
        };
        auto noteColor = withAlpha(color, isTap ? 0.95f : 0.65f);
        m_laneDraw->drawPolygon(pts, 4, noteColor, 0.f, withAlpha(color, 0.f));

        // bright cap on the head of a hold
        if (!isTap && y0 > bottomY + 0.5f) {
            float capTop = std::min(y0 + 3.f * s, y1);
            CCPoint cap[4] = {
                ccp(cx - halfW(y0) + inset, y0),
                ccp(cx + halfW(y0) - inset, y0),
                ccp(cx + halfW(capTop) - inset, capTop),
                ccp(cx - halfW(capTop) + inset, capTop),
            };
            m_laneDraw->drawPolygon(cap, 4, withAlpha(color, 1.f), 0.f, withAlpha(color, 0.f));
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
    // ">" on the left
    m_laneDraw->drawSegment(ccp(lx - cw, cy + ch), ccp(lx, cy), lw, receptor);
    m_laneDraw->drawSegment(ccp(lx, cy), ccp(lx - cw, cy - ch), lw, receptor);
    // "<" on the right
    m_laneDraw->drawSegment(ccp(rx + cw, cy + ch), ccp(rx, cy), lw, receptor);
    m_laneDraw->drawSegment(ccp(rx, cy), ccp(rx + cw, cy - ch), lw, receptor);

    if (lane == 0) {
        m_judgeLabel->setPosition({cx, bottomY - 11.f * s});
        m_judgeLabel->setScale(0.28f * s);
        m_offsetLabel->setPosition({cx, bottomY - 19.f * s});
        m_offsetLabel->setScale(0.45f * s);
    }
}
