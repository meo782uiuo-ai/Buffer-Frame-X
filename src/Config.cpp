#include "Config.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace bf {
    Config& cfg() {
        static Config c;
        return c;
    }

    static std::string customKey(int slot, int i) {
        return fmt::format("custom-{}-{}", slot, i);
    }

    void Config::load() {
        auto* m = Mod::get();
        enabled = m->getSavedValue<bool>("enabled", true);
        stage = std::clamp(m->getSavedValue<int>("stage", 3), 0, kStageCount - 1);
        adaptive = m->getSavedValue<bool>("adaptive", false);
        margin = std::clamp(m->getSavedValue<int>("margin", 28), 10, 90);
        budgetMs = std::clamp(m->getSavedValue<int>("budget-ms", 60), 10, 250);
        hud = m->getSavedValue<bool>("hud", true);
        notice = m->getSavedValue<bool>("notice", true);
        for (int s = 0; s < kCustomSlots; ++s) {
            for (int i = 0; i <= kModes; ++i) {
                int def = i == 0 ? 16 : 0;
                custom[s][i] = std::clamp(m->getSavedValue<int>(customKey(s, i), def), 0, kMaxTicks);
            }
        }
    }

    void Config::save() const {
        auto* m = Mod::get();
        m->setSavedValue<bool>("enabled", enabled);
        m->setSavedValue<int>("stage", stage);
        m->setSavedValue<bool>("adaptive", adaptive);
        m->setSavedValue<int>("margin", margin);
        m->setSavedValue<int>("budget-ms", budgetMs);
        m->setSavedValue<bool>("hud", hud);
        m->setSavedValue<bool>("notice", notice);
        for (int s = 0; s < kCustomSlots; ++s) {
            for (int i = 0; i <= kModes; ++i) {
                m->setSavedValue<int>(customKey(s, i), custom[s][i]);
            }
        }
    }

    void Config::resetDefaults() {
        *this = Config{};
        for (int s = 0; s < kCustomSlots; ++s) custom[s][0] = 16;
    }

    int Config::cap(Mode m) const {
        int v;
        if (stage < kFirstCustom) {
            auto const& s = kStages[stage];
            v = m == Mode::Wave ? s.wave : s.all;
        } else {
            auto const& c = custom[stage - kFirstCustom];
            int own = c[1 + static_cast<int>(m)];
            v = own > 0 ? own : c[0];
        }
        return std::clamp(v, 0, kMaxTicks);
    }

    std::string Config::stageName(int i) const {
        if (i < kFirstCustom) return kStages[i].name;
        return fmt::format("Custom {}", i - kFirstCustom + 1);
    }

    bool Config::exceedsWarning() const {
        for (int i = 0; i < kModes; ++i) {
            if (cap(static_cast<Mode>(i)) > kWarnTicks) return true;
        }
        return false;
    }
}
