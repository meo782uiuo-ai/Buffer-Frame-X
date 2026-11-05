#pragma once

#include <Geode/Geode.hpp>
#include <array>
#include <string>

namespace bf {
    enum class Mode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };
    constexpr int kModes = 8;

    inline char const* modeName(Mode m) {
        switch (m) {
            case Mode::Cube: return "Cubo";
            case Mode::Ship: return "Nave";
            case Mode::Ball: return "Bola";
            case Mode::Ufo: return "Ovni";
            case Mode::Wave: return "Onda";
            case Mode::Robot: return "Robot";
            case Mode::Spider: return "Arana";
            case Mode::Swing: return "Columpio";
        }
        return "?";
    }

    // Etapa prefabricada: cuantos ticks puede retrasar un click.
    struct Stage {
        char const* name;
        int all;  // todos los modos
        int wave; // onda (suele necesitar menos)
    };

    inline constexpr std::array<Stage, 8> kStages = {{
        {"Chispa", 4, 4},
        {"Brasa", 8, 6},
        {"Llama", 12, 8},
        {"Fuego", 16, 12},
        {"Incendio", 24, 16},
        {"Infierno", 32, 24},
        {"Abismo", 48, 32},
        {"Caos", 64, 48},
    }};

    constexpr int kCustomSlots = 3;
    constexpr int kFirstCustom = 8;
    constexpr int kStageCount = kFirstCustom + kCustomSlots;
    constexpr int kMaxTicks = 96;
    constexpr int kWarnTicks = 48;

    struct Config {
        bool enabled = true;
        int stage = 3;          // 0..7 prefabricadas, 8..10 personalizadas
        bool adaptive = false;
        int margin = 28;        // ticks que debe sobrevivir despues del punto de muerte
        int budgetMs = 60;      // tiempo maximo de busqueda por muerte
        bool hud = true;
        bool notice = true;
        // [slot][0] = todos los modos, [slot][1 + modo] = valor del modo (0 = igual que todos)
        int custom[kCustomSlots][1 + kModes] = {};

        void load();
        void save() const;
        void resetDefaults();

        int cap(Mode m) const;
        std::string stageName(int i) const;
        bool exceedsWarning() const;
    };

    Config& cfg();
}
