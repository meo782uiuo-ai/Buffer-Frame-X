#include "Engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

// Como funciona (resumen):
//
//  1. Al dar un click se guarda una foto del juego (checkpoint interno) y se anotan
//     todos tus clicks desde esa foto. La foto se renueva cada ~20 ticks.
//  2. Si vas a morir, la muerte se suprime solo ese frame. Al empezar el siguiente
//     frame se busca, SIN que se vea, el retraso mas chico que te salva: se vuelve a
//     la foto y se avanza la fisica tick por tick con el click culpable retrasado
//     1, 2, 3... ticks. Cada candidato debe sobrevivir `margin` ticks despues del
//     punto de muerte.
//  3. Con el retraso ganador se deja el juego en el tick actual y el click (y su
//     suelta) quedan programados con ese retraso. Si ninguno funciona, mueres normal.

namespace bf::engine {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr int kRefresh = 20;   // ticks entre fotos
        constexpr int kHorizon = 200;  // maximo de ticks entre el click culpable y la muerte
        constexpr std::size_t kLogMax = 40;

        enum class SimResult { Survived, Died, Aborted };

        struct State {
            bool sim = false;        // dentro de una simulacion
            bool injecting = false;  // estamos aplicando un evento nosotros
            bool pending = false;    // hay una muerte que intentar salvar
            bool rescueOff = false;  // ya fallo en este intento: dejar morir
            bool clampNext = false;  // recortar el dt del proximo frame
            bool simDied = false;
            bool peekSeen = false;   // se detecto algun click en la cola antes del frame

            // Eventos que la simulacion aplica DENTRO del tick (igual que cuando se grabaron).
            std::vector<Ev> const* simEv = nullptr;
            std::size_t simNext = 0;

            CheckpointObject* anchor = nullptr;
            int64_t baseProg = 0;
            std::vector<Ev> inputs;     // eventos ya aplicados desde la foto
            std::vector<Ev> scheduled;  // eventos futuros (con retraso)
            int shift = 0;
            bool shiftHold = false;     // el click retrasado sigue apretado

            int relDeath = 0;
            Mode deathMode = Mode::Cube;

            float stepDt = 1.f / 240.f;
            double calDt = 0.0;
            int64_t calSteps = 0;
            int calFrames = 0;

            std::vector<LogEntry> log;
            Stats stats;
        };

        State g;

        float msSince(Clock::time_point t0) {
            return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        }
    }

    PlayLayer* asPlay(GJBaseGameLayer* layer) {
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == layer) return pl;
        return nullptr;
    }

    int64_t progress(GJBaseGameLayer* layer) {
        return static_cast<int64_t>(layer->m_gameState.m_currentProgress);
    }

    Mode modeOf(PlayerObject* p) {
        if (!p) return Mode::Cube;
        if (p->m_isDart) return Mode::Wave;
        if (p->m_isShip) return Mode::Ship;
        if (p->m_isBird) return Mode::Ufo;
        if (p->m_isBall) return Mode::Ball;
        if (p->m_isRobot) return Mode::Robot;
        if (p->m_isSpider) return Mode::Spider;
        if (p->m_isSwing) return Mode::Swing;
        return Mode::Cube;
    }

    bool simulating() { return g.sim; }

    std::vector<LogEntry> const& log() { return g.log; }
    Stats const& stats() { return g.stats; }
    void clearLog() {
        g.log.clear();
        g.stats = Stats{};
    }

    namespace {
        int rel(GJBaseGameLayer* l) {
            return static_cast<int>(progress(l) - g.baseProg);
        }

        void dropAnchor() {
            if (g.anchor) {
                g.anchor->release();
                g.anchor = nullptr;
            }
        }

        void takeAnchor(PlayLayer* pl) {
            auto cp = pl->createCheckpoint();
            if (!cp) return;
            cp->retain();
            dropAnchor();
            g.anchor = cp;
            int64_t nb = progress(pl);
            int delta = static_cast<int>(nb - g.baseProg);
            for (auto& e : g.scheduled) e.rel -= delta;
            g.baseProg = nb;
            g.inputs.clear();
        }

        bool inShift() {
            return g.shift > 0 && (g.shiftHold || !g.scheduled.empty());
        }

        void apply(PlayLayer* pl, Ev const& e) {
            g.injecting = true;
            pl->handleButton(e.down, e.button, e.p1);
            g.injecting = false;
        }

        int effectiveCap(Mode m) {
            int cap = cfg().cap(m);
            if (cfg().adaptive && g.stats.saved >= 3) {
                int learned = static_cast<int>(std::ceil(g.stats.ema * 2.f)) + 3;
                cap = std::min(cap, std::max(4, learned));
            }
            return cap;
        }

        // Copia los eventos moviendo el click culpable (y todo lo posterior) `d` ticks.
        std::vector<Ev> shifted(std::vector<Ev> const& all, int c, int d, int relNow, bool synthetic) {
            std::vector<Ev> ev = all;
            for (auto& e : ev) {
                if (e.rel >= c) e.rel += d;
            }
            if (synthetic) {
                // Para comprobar el futuro suponemos que sueltas despues de mantener lo mismo
                // que llevas hasta ahora (minimo 6 ticks).
                for (bool pf : {true, false}) {
                    int last = -1;
                    for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
                        if (ev[i].button == 1 && ev[i].p1 == pf) last = i;
                    }
                    if (last >= 0 && ev[last].down && ev[last].rel >= c + d) {
                        int hold = std::max(relNow - c, 6);
                        ev.push_back({ev[last].rel + hold, false, 1, pf});
                    }
                }
                std::stable_sort(ev.begin(), ev.end(), [](Ev const& a, Ev const& b) {
                    return a.rel < b.rel;
                });
            }
            return ev;
        }

        // Vuelve a la foto y avanza la fisica hasta `untilRel` aplicando `ev`.
        SimResult simulate(PlayLayer* pl, std::vector<Ev> const& ev, int untilRel, Clock::time_point t0, float budgetMs) {
            // `sim` se activa ANTES de cargar la foto: asi el hook de audio no mueve la musica.
            g.sim = true;
            g.simDied = false;
            pl->loadFromCheckpoint(g.anchor);
            g.baseProg = progress(pl);

            // Los eventos se aplican en `inject()`, dentro del tick, justo donde se grabaron.
            g.simEv = &ev;
            g.simNext = 0;

            int guard = untilRel * 4 + 64;
            int counter = 0;
            SimResult res = SimResult::Survived;

            while (rel(pl) < untilRel) {
                if (--guard < 0) {
                    res = SimResult::Aborted;
                    break;
                }
                if ((++counter & 15) == 0 && msSince(t0) > budgetMs) {
                    res = SimResult::Aborted;
                    break;
                }

                pl->m_extraDelta = 0.0;
                pl->update(g.stepDt * 1.02f);

                if (g.simDied) {
                    res = SimResult::Died;
                    break;
                }
            }

            g.simEv = nullptr;
            g.sim = false;
            return res;
        }

        // Los eventos justo en el borde (`rel == r`) se dejan como programados: si por
        // casualidad ya se aplicaron, repetir un click es inofensivo; perderlo no.
        void commit(PlayLayer* pl, std::vector<Ev> const& ev) {
            int r = rel(pl);
            g.inputs.clear();
            g.scheduled.clear();
            for (auto const& e : ev) {
                (e.rel < r ? g.inputs : g.scheduled).push_back(e);
            }
        }

        void pushLog(LogEntry const& e) {
            g.log.push_back(e);
            if (g.log.size() > kLogMax) g.log.erase(g.log.begin());
        }

        void runSearch(PlayLayer* pl) {
            g.pending = false;
            auto t0 = Clock::now();
            auto const& c = cfg();

            int relNow = rel(pl);
            bool wasShift = inShift();
            int oldShift = g.shift;

            std::vector<Ev> all = g.inputs;
            all.insert(all.end(), g.scheduled.begin(), g.scheduled.end());

            int ci = -1;
            for (int i = static_cast<int>(all.size()) - 1; i >= 0; --i) {
                if (all[i].down && all[i].button == 1 && all[i].rel <= g.relDeath) {
                    ci = i;
                    break;
                }
            }

            int cap = effectiveCap(g.deathMode);
            bool ok = g.anchor && ci >= 0 && cap > 0
                && (g.relDeath - all[ci].rel) <= kHorizon
                && pl->m_gameState.m_timeWarp == 1.f;

            int cRel = ok ? all[ci].rel : 0;
            int found = -1;
            int tries = 0;
            bool timeout = false;
            float budget = static_cast<float>(c.budgetMs);
            int until = g.relDeath + c.margin;

            auto test = [&](int d) {
                ++tries;
                auto ev = shifted(all, cRel, d, relNow, true);
                return simulate(pl, ev, until, t0, budget);
            };

            if (ok) {
                int stride = cap > 32 ? 4 : (cap > 16 ? 3 : (cap > 6 ? 2 : 1));
                for (int d = stride; d <= cap + stride - 1; d += stride) {
                    int dd = std::min(d, cap);
                    if (msSince(t0) > budget) {
                        timeout = true;
                        break;
                    }
                    auto res = test(dd);
                    if (res == SimResult::Survived) {
                        found = dd;
                        break;
                    }
                    if (res == SimResult::Aborted) {
                        timeout = true;
                        break;
                    }
                    if (dd == cap) break;
                }
                // Afinar: probar un poco menos para quedarse con el retraso mas chico.
                if (found > 0 && stride > 1) {
                    for (int k = 1; k < stride && found - 1 >= 1; ++k) {
                        if (msSince(t0) > budget) break;
                        auto res = test(found - 1);
                        if (res != SimResult::Survived) break;
                        --found;
                    }
                }
            }

            bool saved = false;
            if (found > 0) {
                auto ev = shifted(all, cRel, found, relNow, false);
                auto res = simulate(pl, ev, relNow, t0, 1e9f);
                if (res == SimResult::Survived) {
                    commit(pl, ev);
                    bool released = true;
                    for (int i = static_cast<int>(ev.size()) - 1; i >= 0; --i) {
                        if (ev[i].button == 1) {
                            released = !ev[i].down;
                            break;
                        }
                    }
                    g.shift = (wasShift ? oldShift : 0) + found;
                    g.shiftHold = !released;
                    saved = true;
                }
            }

            float ms = msSince(t0);
            int percent = pl->getCurrentPercentInt();

            if (!saved) {
                // Dejar el mundo justo antes de la muerte y que ocurra de forma natural.
                int target = std::max(0, g.relDeath - 1);
                simulate(pl, all, target, t0, 1e9f);
                commit(pl, all);
                g.rescueOff = true;
            }

            auto& st = g.stats;
            ++st.searches;
            st.msSum += ms;
            Outcome out = saved ? Outcome::Saved : (timeout ? Outcome::Timeout : Outcome::Failed);
            if (saved) {
                ++st.saved;
                st.delaySum += found;
                st.ema = st.ema <= 0.f ? static_cast<float>(found) : st.ema * 0.8f + found * 0.2f;
            } else if (timeout) {
                ++st.timeouts;
            } else {
                ++st.failed;
            }

            pushLog({percent, g.deathMode, saved ? found : 0, tries, ms, out});
            refreshHud(pl, saved ? fmt::format("+{}t", found) : std::string("sin salvar"));
            g.clampNext = true;
        }
    }

    void reset() {
        dropAnchor();
        g.sim = false;
        g.injecting = false;
        g.pending = false;
        g.rescueOff = false;
        g.clampNext = false;
        g.simDied = false;
        g.inputs.clear();
        g.scheduled.clear();
        g.shift = 0;
        g.shiftHold = false;
        g.baseProg = 0;
        g.simEv = nullptr;
        g.simNext = 0;
    }

    // Analisis previo: lista de peligros ordenados por X. Cerca de un peligro las
    // fotos se toman mas seguido, asi la busqueda parte de mas cerca y prueba mas retrasos.
    static std::vector<float> s_haz;

    static int refreshNow(PlayLayer* pl) {
        if (s_haz.empty() || !pl->m_player1) return kRefresh;
        float x = pl->m_player1->getPositionX();
        auto it = std::lower_bound(s_haz.begin(), s_haz.end(), x);
        if (it != s_haz.end() && *it - x < 420.f) return 8;
        return kRefresh;
    }

    int hazardCount() { return static_cast<int>(s_haz.size()); }

    void onLevelStart(PlayLayer* pl) {
        reset();
        s_haz.clear();
        if (!pl || !pl->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (obj && obj->m_objectType == GameObjectType::Hazard) s_haz.push_back(obj->getPositionX());
        }
        std::sort(s_haz.begin(), s_haz.end());
    }

    bool onButton(PlayLayer* pl, bool down, int button, bool p1) {
        if (!cfg().enabled || g.sim || g.injecting || g.rescueOff) return false;

        if (inShift()) {
            int r = g.anchor ? rel(pl) : 0;
            g.scheduled.push_back({r + g.shift, down, button, p1});
            std::stable_sort(g.scheduled.begin(), g.scheduled.end(), [](Ev const& a, Ev const& b) {
                return a.rel < b.rel;
            });
            if (!down && button == 1) g.shiftHold = false;
            return true;
        }

        // Respaldo: lo normal es que la foto ya se haya tomado entre frames (beforeUpdate).
        // Solo si nunca vimos clicks en la cola se toma aqui, a mitad de tick.
        if (down && button == 1) {
            bool stale = !g.peekSeen && rel(pl) > refreshNow(pl);
            if (!g.anchor || stale) {
                if (pl->m_player1 && !pl->m_player1->m_isDead) takeAnchor(pl);
            }
        }
        if (g.anchor) g.inputs.push_back({rel(pl), down, button, p1});
        return false;
    }

    void inject(PlayLayer* pl) {
        if (g.sim) {
            if (!g.simEv) return;
            int r = rel(pl);
            while (g.simNext < g.simEv->size() && (*g.simEv)[g.simNext].rel <= r) {
                apply(pl, (*g.simEv)[g.simNext++]);
            }
            return;
        }
        if (g.scheduled.empty()) return;
        int r = rel(pl);
        while (!g.scheduled.empty() && g.scheduled.front().rel <= r) {
            Ev e = g.scheduled.front();
            g.scheduled.erase(g.scheduled.begin());
            apply(pl, e);
            g.inputs.push_back({r, e.down, e.button, e.p1});
        }
    }

    bool onDestroy(PlayLayer* pl, PlayerObject* player, GameObject* object) {
        if (!cfg().enabled) return false;
        if (g.sim) {
            g.simDied = true;
            return true;
        }
        if (g.pending) return true;
        if (g.rescueOff || !g.anchor) return false;
        if (object && object == pl->m_anticheatSpike) return false;

        g.pending = true;
        g.relDeath = rel(pl);
        g.deathMode = modeOf(player);
        return true;
    }

    void beforeUpdate(PlayLayer* pl, float& dt) {
        if (!cfg().enabled) {
            if (g.anchor || g.pending) reset();
            return;
        }
        if (g.pending) runSearch(pl);

        // La foto se toma aqui, ENTRE frames (estado limpio), justo antes de que se
        // procese un click que esta esperando en la cola de entrada.
        if (!g.sim && !g.pending && !g.rescueOff && !inShift()
            && pl->m_player1 && !pl->m_player1->m_isDead) {
            bool queued = false;
            for (auto const& cmd : pl->m_queuedButtons) {
                if (cmd.m_isPush && cmd.m_button == PlayerButton::Jump) {
                    queued = true;
                    break;
                }
            }
            if (queued) {
                g.peekSeen = true;
                if (!g.anchor || rel(pl) > refreshNow(pl)) takeAnchor(pl);
            }
        }

        if (g.clampNext) {
            dt = std::min(dt, 1.f / 60.f);
            g.clampNext = false;
        }
    }

    void afterUpdate(PlayLayer* pl, float dt, int64_t before) {
        int64_t after = progress(pl);
        if (after <= before || dt <= 0.f || dt > 0.1f) return;
        if (pl->m_gameState.m_timeWarp != 1.f) return;

        g.calDt += dt;
        g.calSteps += after - before;
        if (++g.calFrames >= 90) {
            double s = g.calDt / static_cast<double>(g.calSteps);
            int n = static_cast<int>(std::lround(1.0 / s));
            if (n >= 30 && n <= 2000) g.stepDt = 1.f / static_cast<float>(n);
            g.calDt = 0.0;
            g.calSteps = 0;
            g.calFrames = 0;
        }
    }

    void createHud(PlayLayer* pl) {
        if (!cfg().hud || !pl || pl->getChildByID("hud"_spr)) return;
        auto lbl = CCLabelBMFont::create("BUF ON", "bigFont.fnt");
        lbl->setID("hud"_spr);
        lbl->setScale(.28f);
        lbl->setAnchorPoint({0.f, 1.f});
        auto ws = CCDirector::get()->getWinSize();
        lbl->setPosition({8.f, ws.height - 6.f});
        lbl->setColor({220, 20, 60});
        lbl->setOpacity(150);
        pl->addChild(lbl, 1000);
    }

    void refreshHud(PlayLayer* pl, std::string const& flash) {
        if (!pl) return;
        auto node = pl->getChildByID("hud"_spr);
        if (!node) return;
        auto lbl = static_cast<CCLabelBMFont*>(node);
        std::string base = cfg().enabled ? "BUF ON" : "BUF OFF";
        if (flash.empty()) {
            lbl->setString(base.c_str());
            return;
        }
        lbl->setString(fmt::format("{}  {}", base, flash).c_str());
        lbl->stopAllActions();
        lbl->setOpacity(255);
        lbl->runAction(CCSequence::create(
            CCDelayTime::create(1.4f),
            CCFadeTo::create(0.6f, 150),
            nullptr
        ));
    }

    void onConfigChanged() {
        auto pl = PlayLayer::get();
        if (!cfg().enabled) {
            reset();
        }
        if (!pl) return;
        auto hud = pl->getChildByID("hud"_spr);
        if (hud && !cfg().hud) hud->removeFromParent();
        else if (!hud && cfg().hud) createHud(pl);
        refreshHud(pl);
    }

    void warnOnce() {}
}
