#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <string>

// Diagnostic log: records what the game and the mod actually did, so timing problems can be
// read off a file instead of guessed. Written to <mod save dir>/diag.log when enabled.
class Diag {
public:
    static Diag& get() {
        static Diag d;
        return d;
    }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    std::filesystem::path path() const;

    // generic line, prefixed with a tag; always safe to call when disabled (no-op)
    void line(char const* tag, std::string text);
    void flush();
    void clearFile();

private:
    bool m_enabled = false;
    std::deque<std::string> m_buf;
    size_t m_pending = 0;
};
