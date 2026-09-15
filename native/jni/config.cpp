// ─────────────────────────────────────────────────────────────────────────────
// GhostBoot — config.cpp
// Target list persistence.  Reads / writes /data/adb/ghostboot/targets.conf
// ─────────────────────────────────────────────────────────────────────────────
#include "ghostboot.hpp"

#include <fstream>
#include <string>
#include <cstring>
#include <cctype>
#include <sys/stat.h>
#include <cerrno>

namespace ghostboot {

const char* work_dir_path()   { return "/data/adb/ghostboot"; }
const char* config_file_path() { return "/data/adb/ghostboot/targets.conf"; }
const char* settings_file_path() { return "/data/adb/ghostboot/settings.conf"; }

// Obfuscated log tag: XOR-folded at compile time, decoded once into a
// static buffer. No plain "GhostBoot" bytes in .rodata.
const char* log_tag() {
    static char tag[10] = {0};
    static bool init = false;
    if (!init) {
        const unsigned char enc[] = {0x1C,0x33,0x34,0x28,0x2F,0x19,0x34,0x34,0x2F};
        for (int i = 0; i < 9; i++) tag[i] = (char)(enc[i] ^ 0x5B);
        tag[9] = '\0';
        init = true;
    }
    return tag;
}

TargetConfig& TargetConfig::instance() {
    static TargetConfig cfg;
    return cfg;
}

void TargetConfig::load() {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
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
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    saveLocked();
}

void TargetConfig::saveLocked() const {
    mkdir(work_dir_path(), 0700);
    std::ofstream f(config_file_path(), std::ios::trunc);
    if (!f) return;
    f << "# GhostBoot target list\n";
    for (const auto& p : packages_) f << p << '\n';
    chmod(config_file_path(), 0600);
}

bool TargetConfig::is_target(const char* name) const {
    if (!name) return false;
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    return packages_.count(name) > 0;
}

void TargetConfig::add(const std::string& pkg) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    packages_.insert(pkg);
    saveLocked();
}
void TargetConfig::remove(const std::string& pkg) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    packages_.erase(pkg);
    saveLocked();
}
void TargetConfig::clear() {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    packages_.clear();
    saveLocked();
}
std::unordered_set<std::string> TargetConfig::list() const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    return packages_;  // return a copy — safe to use after lock is released
}

// ── Settings ────────────────────────────────────────────────────────────────
namespace {
std::recursive_mutex g_settings_mutex;
Settings g_settings;  // defaults = all protections ON (see header)

std::string trim_copy(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool is_on(const std::string& v) {
    return v == "1" || v == "true" || v == "on" || v == "yes";
}
} // anonymous namespace

void reload_settings() {
    std::lock_guard<std::recursive_mutex> lk(g_settings_mutex);
    Settings s;  // start from defaults every time (missing keys stay default)
    std::ifstream f(settings_file_path());
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            line = trim_copy(line);
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim_copy(line.substr(0, eq));
            std::string val = trim_copy(line.substr(eq + 1));
            for (auto& c : val) c = (char)tolower(c);
            if (key == "bootloader_spoof")      s.bootloader_spoof = is_on(val);
            else if (key == "root_hide") {
                if (val == "off")              s.root_hide = RootHideLevel::Off;
                else if (val == "aggressive")  s.root_hide = RootHideLevel::Aggressive;
                else                           s.root_hide = RootHideLevel::Basic;
            }
            else if (key == "lsposed_hide")     s.lsposed_hide = is_on(val);
            else if (key == "stealth_mode")     s.stealth_mode = is_on(val);
        }
    }
    g_settings = s;
}

Settings settings() {
    std::lock_guard<std::recursive_mutex> lk(g_settings_mutex);
    return g_settings;
}

bool logging_enabled() {
    std::lock_guard<std::recursive_mutex> lk(g_settings_mutex);
    return !g_settings.stealth_mode;
}

} // namespace ghostboot
