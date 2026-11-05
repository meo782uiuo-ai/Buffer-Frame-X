#include "Engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

// Como funciona (v0.3, motor predictivo con "fantasma"):
//
//  1. Al empezar el nivel se leen los objetos: se cuentan los peligros y se guardan los
//     objetos con estado (pads, orbes, portales) para poder restaurarlos.
//  2. Cuando das un click, un jugador FANTASMA invisible copia tu estado y se simula
//     hacia adelante (solo ese jugador, sin efectos ni sonido) con tu click.
//  3. Si el fantasma muere, se repite la prueba con el click retrasado 1, 2, 3... ticks.
//     El primer retraso con el que el salto aterriza a salvo gana.
//  4. El click real se entrega con ese retraso (y tu suelta con el mismo retraso, para
//     conservar el ritmo). Todo ocurre ANTES de morir: no se rebobina nada.

namespace bf::engine {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr std::size_t kLogMax = 40;
        constexpr int kTap = 6;  // ticks que el fantasma mantiene el click

        enum class Sim { Safe, Dead, Abort };

        struct State {
            bool sim = false;        // el fantasma se esta simulando
            bool injecting = false;  // estamos aplicando un evento nosotros
            bool ghostDied = false;
            bool ghostStop = false;  // el fantasma toco algo que no se modela: se da por bueno

            PlayerObject* ghost = nullptr;
            CCLayer* ghostLayer = nullptr;
            int ghostBase = 0;

            std::vector<Ev> scheduled;  // eventos futuros (rel = tick absoluto)
            int shift = 0;
            bool shiftHold = false;

            float stepDt = 1.f / 240.f;
            double calDt = 0.0;
            int64_t calSteps = 0;
            int calFrames = 0;

            std::vector<LogEntry> log;
            Stats stats;
        };

        State g;

        int s_hazards = 0;
        std::vector<std::pair<float, GameObject*>> s_state;  // objetos con estado, por X
        std::vector<std::pair<GameObject*, bool>> s_snap;    // su estado antes de simular

        float msSince(Clock::time_point t0) {
            return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        }

        bool inShift() {
            return g.shift > 0 && (g.shiftHold || !g.scheduled.empty());
        }

        bool supported(Mode m) {
            return m == Mode::Cube || m == Mode::Ball || m == Mode::Ufo
                || m == Mode::Spider || m == Mode::Swing;
        }

        int effectiveCap(Mode m) {
            int cap = cfg().cap(m);
            if (cfg().adaptive && g.stats.saved >= 3) {
                int learned = static_cast<int>(std::ceil(g.stats.ema * 2.f)) + 3;
                cap = std::min(cap, std::max(4, learned));
            }
            return cap;
        }

        void dropGhost() {
            if (g.ghostLayer) g.ghostLayer->removeAllChildrenWithCleanup(true);
            if (g.ghost) {
                g.ghost->release();
                g.ghost = nullptr;
            }
            if (g.ghostLayer) {
                g.ghostLayer->release();
                g.ghostLayer = nullptr;
            }
            g.ghostBase = 0;
        }

        // El fantasma vive en una capa que nunca se dibuja: no cuesta nada entre clicks.
        bool makeGhost(PlayLayer* pl) {
            if (g.ghost) return true;
            g.ghostLayer = CCLayer::create();
            if (!g.ghostLayer) return false;
            g.ghostLayer->retain();
            g.ghost = PlayerObject::create(1, 1, pl, g.ghostLayer, true);
            if (!g.ghost) {
                dropGhost();
                return false;
            }
            g.ghost->retain();
            g.ghost->setVisible(false);
            g.ghostLayer->addChild(g.ghost);
            g.ghostBase = g.ghostLayer->getChildrenCount();
            return true;
        }

        void sweepGhostLayer() {
            if (!g.ghostLayer) return;
            auto children = g.ghostLayer->getChildren();
            while (children && static_cast<int>(children->count()) > g.ghostBase) {
                auto node = static_cast<CCNode*>(children->lastObject());
                g.ghostLayer->removeChild(node, true);
            }
        }

        void copyState(PlayerObject* gh, PlayerObject* real) {
            gh->setVisible(false);
            gh->resetTouchedRings(true);
            gh->copyAttributes(real);
            gh->setScale(real->getScale());
            gh->setContentSize(real->getContentSize());
            gh->m_gravityMod = real->m_gravityMod;
            gh->m_isOnGround = real->m_isOnGround;
            gh->m_isUpsideDown = real->m_isUpsideDown;
            gh->m_vehicleSize = real->m_vehicleSize;
            gh->m_playerSpeed = real->m_playerSpeed;
            gh->m_isDashing = real->m_isDashing;
            gh->m_lastActivatedPortal = real->m_lastActivatedPortal;
            gh->setPosition(real->getPosition());
            gh->m_position = real->m_position;
            // Estado de los botones: sin esto el fantasma arrastra el de la prueba anterior.
            gh->m_holdingButtons = real->m_holdingButtons;
            gh->m_jumpBuffered = real->m_jumpBuffered;
            gh->m_wasJumpBuffered = real->m_wasJumpBuffered;
            gh->m_stateJumpBuffered = real->m_stateJumpBuffered;
            gh->m_stateRingJump = real->m_stateRingJump;
            gh->m_stateRingJump2 = real->m_stateRingJump2;
            gh->m_wasRobotJump = real->m_wasRobotJump;
            gh->m_touchedRing = real->m_touchedRing;
            gh->m_touchedPad = real->m_touchedPad;
        }

        // Pads, orbes y portales guardan si ya se usaron: el fantasma no debe gastarlos.
        void snapshot(float px) {
            s_snap.clear();
            auto it = std::lower_bound(
                s_state.begin(), s_state.end(), px - 40.f,
                [](auto const& a, float x) { return a.first < x; }
            );
            for (; it != s_state.end() && it->first < px + 1400.f; ++it) {
                s_snap.push_back({it->second, it->second->m_isActivated});
            }
        }

        void restoreSnapshot() {
            for (auto const& s : s_snap) s.first->m_isActivated = s.second;
        }

        Sim runGhost(PlayLayer* pl, int delay, Clock::time_point t0, float budgetMs) {
            auto real = pl->m_player1;
            auto gh = g.ghost;
            copyState(gh, real);
            g.ghostDied = false;
            g.ghostStop = false;
            g.sim = true;

            int const maxTicks = 90 + cfg().margin;
            int const total = delay + maxTicks;
            float const dt = g.stepDt;
            bool left = false;
            int groundRun = 0;
            Sim res = Sim::Safe;

            for (int tick = 0; tick < total; ++tick) {
                if ((tick & 7) == 7 && msSince(t0) > budgetMs) {
                    res = Sim::Abort;
                    break;
                }
                if (tick == delay) gh->pushButton(PlayerButton::Jump);
                if (tick == delay + kTap) gh->releaseButton(PlayerButton::Jump);

                gh->update(dt);
                gh->updateRotation(dt);
                if (g.ghostDied) {
                    res = Sim::Dead;
                    break;
                }

                gh->m_collisionLogTop->removeAllObjects();
                gh->m_collisionLogBottom->removeAllObjects();
                gh->m_collisionLogLeft->removeAllObjects();
                gh->m_collisionLogRight->removeAllObjects();
                pl->checkCollisions(gh, dt, false);
                if (g.ghostDied) {
                    res = Sim::Dead;
                    break;
                }
                if (g.ghostStop) break;  // portal, orbe...: no se puede seguir, se da por bueno

                if (tick >= delay) {
                    bool onG = gh->m_isOnGround;
                    if (!onG) left = true;
                    groundRun = onG ? groundRun + 1 : 0;
                    if (groundRun >= 3 && (left || tick - delay >= 8)) break;  // aterrizo a salvo
                }
            }

            g.sim = false;
            restoreSnapshot();
            sweepGhostLayer();
            return res;
        }

        void pushLog(LogEntry const& e) {
            g.log.push_back(e);
            if (g.log.size() > kLogMax) g.log.erase(g.log.begin());
        }

        void clearRuntime() {
            g.scheduled.clear();
            g.shift = 0;
            g.shiftHold = false;
            g.sim = false;
            g.injecting = false;
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
    bool isGhost(PlayerObject* p) { return g.sim && g.ghost && p == g.ghost; }
    void ghostUnmodeled() { g.ghostStop = true; }
    int hazardCount() { return s_hazards; }

    std::vector<LogEntry> const& log() { return g.log; }
    Stats const& stats() { return g.stats; }
    void clearLog() {
        g.log.clear();
        g.stats = Stats{};
    }

    void reset() { clearRuntime(); }

    void onQuit() {
        clearRuntime();
        s_state.clear();
        s_snap.clear();
        s_hazards = 0;
        dropGhost();
    }

    void onLevelStart(PlayLayer* pl) {
        onQuit();
        if (!pl || !pl->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (!obj) continue;
            switch (obj->m_objectType) {
                case GameObjectType::Hazard:
                case GameObjectType::AnimatedHazard:
                    ++s_hazards;
                    break;
                case GameObjectType::Solid:
                case GameObjectType::Decoration:
                case GameObjectType::Slope:
                case GameObjectType::CollisionObject:
                case GameObjectType::Special:
                case GameObjectType::EnterEffectObject:
                    break;
                default:
                    s_state.push_back({obj->getPositionX(), obj});
                    break;
            }
        }
        std::sort(s_state.begin(), s_state.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    }

    bool onButton(PlayLayer* pl, bool down, int button, bool p1) {
        if (!cfg().enabled || g.sim || g.injecting || button != 1) return false;
        if (pl->m_levelSettings && pl->m_levelSettings->m_platformerMode) return false;

        int64_t now = progress(pl);

        // Mientras hay un click retrasado, todo lo que hagas se retrasa igual (mismo ritmo).
        if (inShift()) {
            g.scheduled.push_back({static_cast<int>(now) + g.shift, down, button, p1});
            std::stable_sort(g.scheduled.begin(), g.scheduled.end(), [](Ev const& a, Ev const& b) {
                return a.rel < b.rel;
            });
            g.shiftHold = down;
            return true;
        }

        if (!down || !p1) return false;
        auto player = pl->m_player1;
        if (!player || player->m_isDead) return false;
        if (pl->m_gameState.m_timeWarp != 1.f) return false;

        Mode mode = modeOf(player);
        if (!supported(mode)) return false;
        int cap = effectiveCap(mode);
        if (cap <= 0 || !makeGhost(pl)) return false;

        auto t0 = Clock::now();
        float budget = static_cast<float>(cfg().budgetMs);
        int tries = 0;
        snapshot(player->getPositionX());
        auto test = [&](int d) {
            ++tries;
            return runGhost(pl, d, t0, budget);
        };

        // Sin retraso: si ya sobrevives, el click pasa tal cual.
        Sim first = test(0);
        if (first != Sim::Dead) return false;

        int found = 0;
        int fallback = 0;
        bool timeout = false;
        for (int d = 1; d <= cap; d += (d < 8 ? 1 : 2)) {
            if (msSince(t0) > budget) {
                timeout = true;
                break;
            }
            Sim r = test(d);
            if (r == Sim::Abort) {
                timeout = true;
                break;
            }
            if (r != Sim::Safe) continue;
            if (!fallback) fallback = d;
            // Un retraso solido sobrevive tambien con 1 tick mas (el reloj real puede variar).
            Sim r2 = (d == cap) ? Sim::Safe : test(d + 1);
            if (r2 == Sim::Safe) {
                found = d;
                break;
            }
        }
        if (!found) found = fallback;

        float ms = msSince(t0);
        int percent = pl->getCurrentPercentInt();
        auto& st = g.stats;
        ++st.searches;
        st.msSum += ms;

        if (found > 0) {
            g.scheduled.push_back({static_cast<int>(now) + found, true, button, p1});
            g.shift = found;
            g.shiftHold = true;
            ++st.saved;
            st.delaySum += found;
            st.ema = st.ema <= 0.f ? static_cast<float>(found) : st.ema * 0.8f + found * 0.2f;
            pushLog({percent, mode, found, tries, ms, Outcome::Saved});
            refreshHud(pl, fmt::format("+{}t", found));
            return true;
        }

        if (timeout) ++st.timeouts;
        else ++st.failed;
        pushLog({percent, mode, 0, tries, ms, timeout ? Outcome::Timeout : Outcome::Failed});
        refreshHud(pl, "sin salvar");
        return false;
    }

    void inject(PlayLayer* pl) {
        if (g.sim) return;
        if (g.scheduled.empty()) {
            if (!g.shiftHold) g.shift = 0;
            return;
        }
        int64_t now = progress(pl);
        while (!g.scheduled.empty() && g.scheduled.front().rel <= now) {
            Ev e = g.scheduled.front();
            g.scheduled.erase(g.scheduled.begin());
            g.injecting = true;
            pl->handleButton(e.down, e.button, e.p1);
            g.injecting = false;
        }
        if (g.scheduled.empty() && !g.shiftHold) g.shift = 0;
    }

    bool onDestroy(PlayLayer*, PlayerObject* player, GameObject*) {
        // Solo el fantasma se "muere" aqui; tu jugador real muere como siempre.
        if (isGhost(player)) {
            g.ghostDied = true;
            return true;
        }
        return false;
    }

    void beforeUpdate(PlayLayer*, float&) {
        if (!cfg().enabled && (!g.scheduled.empty() || g.shift)) clearRuntime();
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
        if (!cfg().enabled) clearRuntime();
        if (!pl) return;
        auto hud = pl->getChildByID("hud"_spr);
        if (hud && !cfg().hud) hud->removeFromParent();
        else if (!hud && cfg().hud) createHud(pl);
        refreshHud(pl);
    }

    void warnOnce() {}
}
