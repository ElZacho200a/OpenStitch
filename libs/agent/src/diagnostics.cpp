// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

void DiagnosticLog::set_enabled(bool on) {
    const std::lock_guard lock(mutex_);
    enabled_ = on;
}
bool DiagnosticLog::enabled() const {
    const std::lock_guard lock(mutex_);
    return enabled_;
}
void DiagnosticLog::set_content_consent(bool on) {
    const std::lock_guard lock(mutex_);
    content_ = on;
}
bool DiagnosticLog::content_consent() const {
    const std::lock_guard lock(mutex_);
    return content_;
}
void DiagnosticLog::add_secret(const std::string& secret) {
    const std::lock_guard lock(mutex_);
    if (secret.size() >= 8) {
        secrets_.push_back(secret);
    }
}

std::string DiagnosticLog::sanitize(std::string text) const {
    std::vector<std::string> secrets;
    {
        const std::lock_guard lock(mutex_);
        secrets = secrets_;
    }
    text = redact(std::move(text), secrets);
    // Chemins absolus Windows (C:\...) et Unix (/home/..., /Users/...) : jamais dans un journal.
    static const std::regex windows(R"([A-Za-z]:[\\/][^\s"',;]*)");
    static const std::regex unix_home(R"(/(?:home|Users|root|tmp|var)/[^\s"',;]*)");
    text = std::regex_replace(text, windows, "<chemin>");
    text = std::regex_replace(text, unix_home, "<chemin>");
    return text;
}

void DiagnosticLog::record(const std::string& event, const std::string& detail) {
    if (!enabled()) {
        return;
    }
    std::string line = event;
    if (!detail.empty()) {
        line += " " + detail;
    }
    line = sanitize(std::move(line));
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    lines_.push_back(std::string(stamp) + " #" + std::to_string(next_++) + " " + line);
    if (lines_.size() > capacity_) {
        lines_.erase(lines_.begin(),
                     lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - capacity_));
    }
}

void DiagnosticLog::record_content(const std::string& event, const std::string& content) {
    if (!content_consent()) {
        return;
    }
    record(event, content.size() > 2000 ? content.substr(0, 2000) + "…" : content);
}

std::string DiagnosticLog::dump() const {
    const std::lock_guard lock(mutex_);
    std::string out;
    for (const auto& line : lines_) {
        out += line + "\n";
    }
    return out;
}

std::size_t DiagnosticLog::size() const {
    const std::lock_guard lock(mutex_);
    return lines_.size();
}

void DiagnosticLog::clear() {
    const std::lock_guard lock(mutex_);
    lines_.clear();
}

} // namespace openstitch::agent
