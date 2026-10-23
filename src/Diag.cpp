#include "Diag.hpp"

#include <Geode/utils/file.hpp>
#include <fstream>

using namespace geode::prelude;

std::filesystem::path Diag::path() const {
    return Mod::get()->getSaveDir() / "diag.log";
}

void Diag::setEnabled(bool on) {
    if (m_enabled && !on) this->flush();
    m_enabled = on;
}

void Diag::line(char const* tag, std::string text) {
    if (!m_enabled) return;
    m_buf.push_back(fmt::format("{} {}", tag, text));
    m_pending++;
    if (m_buf.size() > 20000) m_buf.pop_front();
    if (m_pending >= 200) this->flush();
}

void Diag::flush() {
    if (m_pending == 0) return;
    std::error_code ec;
    std::filesystem::create_directories(this->path().parent_path(), ec);
    std::ofstream f(this->path(), std::ios::app);
    size_t start = m_buf.size() >= m_pending ? m_buf.size() - m_pending : 0;
    for (size_t i = start; i < m_buf.size(); i++) f << m_buf[i] << '\n';
    m_pending = 0;
}

void Diag::clearFile() {
    m_buf.clear();
    m_pending = 0;
    std::error_code ec;
    std::filesystem::remove(this->path(), ec);
}
