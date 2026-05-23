// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — config.cpp
// Target list persistence.  Reads / writes /data/adb/ghostboot/targets.conf
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <fstream>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <cerrno>

namespace ghostboot {

const char* work_dir_path()   { return "/data/adb/ghostboot"; }
const char* config_file_path() { return "/data/adb/ghostboot/targets.conf"; }

TargetConfig& TargetConfig::instance() {
    static TargetConfig cfg;
    return cfg;
}

void TargetConfig::load() {
    std::lock_guard<std::mutex> lk(mutex_);
    packages_.clear();
    std::ifstream f(config_file_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        auto e = line.find_last_not_of(" \t\r\n");
        std::string pkg = line.substr(s, e - s + 1);
        if (pkg.empty() || pkg[0] == '#') continue;
        packages_.insert(pkg);
    }
}

void TargetConfig::save() {
    std::lock_guard<std::mutex> lk(mutex_);
    mkdir(work_dir_path(), 0700);
    std::ofstream f(config_file_path(), std::ios::trunc);
    if (!f) return;
    f << "# GhostBoot target list\n";
    for (const auto& p : packages_) f << p << '\n';
    chmod(config_file_path(), 0600);
}

bool TargetConfig::is_target(const char* name) const {
    if (!name) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    return packages_.count(name) > 0;
}

void TargetConfig::add(const std::string& pkg) {
    { std::lock_guard<std::mutex> lk(mutex_); packages_.insert(pkg); }
    save();
}
void TargetConfig::remove(const std::string& pkg) {
    { std::lock_guard<std::mutex> lk(mutex_); packages_.erase(pkg); }
    save();
}
void TargetConfig::clear() {
    { std::lock_guard<std::mutex> lk(mutex_); packages_.clear(); }
    save();
}
const std::unordered_set<std::string>& TargetConfig::list() const {
    // WARNING: returned reference is only valid while the caller holds mutex_.
    // Caller MUST copy the set before releasing the lock.
    std::lock_guard<std::mutex> lk(mutex_);
    return packages_;
}

} // namespace ghostboot
