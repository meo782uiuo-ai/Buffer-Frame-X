#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "Config.hpp"

// Nota: las clases del juego (PlayLayer, PlayerObject...) viven en el namespace
// global, por eso aqui se usan sin prefijo.
namespace bf {
    // Un click (o suelta) registrado. `rel` son ticks desde la foto (anchor).
    struct Ev {
        int rel;
        bool down;
        int button;
        bool p1;
    };

    enum class Outcome { Saved, Failed, Timeout };

    struct LogEntry {
        int percent;
        Mode mode;
        int delay;   // ticks aplicados (0 si no se salvo)
        int tries;   // candidatos probados
        float ms;    // tiempo de busqueda
        Outcome outcome;
    };

    struct Stats {
        int saved = 0;
        int failed = 0;
        int timeouts = 0;
        int delaySum = 0;
        float msSum = 0.f;
        int searches = 0;
        float ema = 0.f; // retraso tipico que necesitas (para el modo adaptativo)
    };

    namespace engine {
        PlayLayer* asPlay(GJBaseGameLayer* layer);
        int64_t progress(GJBaseGameLayer* layer);
        Mode modeOf(PlayerObject* player);

        bool simulating();
        void reset();
        void onLevelStart(PlayLayer* pl);

        // Devuelve true si el evento fue absorbido (programado con retraso).
        bool onButton(PlayLayer* pl, bool down, int button, bool p1);
        // Aplica los eventos programados (o los de la simulacion) que ya tocan.
        void inject(PlayLayer* pl);
        // Devuelve true si se debe suprimir la muerte (se va a intentar salvar).
        bool onDestroy(PlayLayer* pl, PlayerObject* player, GameObject* object);

        void beforeUpdate(PlayLayer* pl, float& dt);
        void afterUpdate(PlayLayer* pl, float dt, int64_t before);

        void onConfigChanged();
        void createHud(PlayLayer* pl);
        void refreshHud(PlayLayer* pl, std::string const& flash = "");
        void warnOnce();
        int hazardCount();

        std::vector<LogEntry> const& log();
        Stats const& stats();
        void clearLog();
    }
}
