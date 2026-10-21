#pragma once

#include <Geode/Result.hpp>
#include <filesystem>
#include <string>
#include <vector>

// Records the player's real jump inputs in practice mode and saves them as a GDR JSON macro.
class Recorder {
public:
    struct Input {
        double t;
        bool down;
        bool p2;
    };

    void clear() { m_inputs.clear(); m_held[0] = m_held[1] = false; }
    size_t size() const { return m_inputs.size(); }
    bool empty() const { return m_inputs.empty(); }

    void add(double t, bool down, bool p2) {
        int p = p2 ? 1 : 0;
        if (down == m_held[p]) return; // ignore repeats
        m_held[p] = down;
        m_inputs.push_back({t, down, p2});
    }

    // Went back to a checkpoint (or restarted): drop everything after `t`
    void truncate(double t) {
        while (!m_inputs.empty() && m_inputs.back().t >= t) m_inputs.pop_back();
        m_held[0] = m_held[1] = false;
        for (auto const& in : m_inputs) m_held[in.p2 ? 1 : 0] = in.down;
    }

    std::vector<Input> const& inputs() const { return m_inputs; }
    std::pair<double, double> timeRange() const {
        if (m_inputs.empty()) return {0.0, 0.0};
        return {m_inputs.front().t, m_inputs.back().t};
    }

    geode::Result<> save(std::filesystem::path const& path, std::string const& levelName, int levelID, double framerate) const;

private:
    std::vector<Input> m_inputs;
    bool m_held[2] = {false, false};
};
