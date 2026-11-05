#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Config.hpp"
#include "Engine.hpp"
#include "ui/BufferMenu.hpp"
#include "ui/Theme.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    bf::cfg().load();
}

// Entrada, fisica por ticks y bucle principal.
class $modify(BFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (auto pl = bf::engine::asPlay(this)) {
            if (bf::engine::onButton(pl, down, button, isPlayer1)) return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        if (auto pl = bf::engine::asPlay(this)) {
            bf::engine::inject(pl);
            // Durante una simulacion no se consumen los clicks reales.
            if (bf::engine::simulating()) return;
        }
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (auto pl = bf::engine::asPlay(this)) bf::engine::inject(pl);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    // El fantasma no debe tocar el estado global del nivel: portales de modo, dual,
    // espejo, orbes y triggers por toque se cortan (la prediccion se da por buena).
    void playerTouchedRing(PlayerObject* player, RingObject* obj) {
        if (bf::engine::isGhost(player)) {
            bf::engine::ghostUnmodeled();
            return;
        }
        GJBaseGameLayer::playerTouchedRing(player, obj);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* obj) {
        if (bf::engine::isGhost(player)) {
            bf::engine::ghostUnmodeled();
            return;
        }
        GJBaseGameLayer::playerTouchedTrigger(player, obj);
    }

    void playerWillSwitchMode(PlayerObject* player, GameObject* obj) {
        if (bf::engine::isGhost(player)) {
            bf::engine::ghostUnmodeled();
            return;
        }
        GJBaseGameLayer::playerWillSwitchMode(player, obj);
    }

    void toggleDualMode(GameObject* obj, bool dual, PlayerObject* player, bool noEffects) {
        if (bf::engine::simulating()) {
            bf::engine::ghostUnmodeled();
            return;
        }
        GJBaseGameLayer::toggleDualMode(obj, dual, player, noEffects);
    }

    void toggleFlipped(bool flip, bool noEffects) {
        if (bf::engine::simulating()) {
            bf::engine::ghostUnmodeled();
            return;
        }
        GJBaseGameLayer::toggleFlipped(flip, noEffects);
    }

    void update(float dt) {
        auto pl = bf::engine::asPlay(this);
        if (!pl || bf::engine::simulating()) {
            GJBaseGameLayer::update(dt);
            return;
        }
        bf::engine::beforeUpdate(pl, dt);
        int64_t before = bf::engine::progress(pl);
        GJBaseGameLayer::update(dt);
        bf::engine::afterUpdate(pl, dt, before);
    }
};

class $modify(BFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        bf::engine::onLevelStart(this);
        if (bf::cfg().enabled) {
            bf::engine::createHud(this);
        }
        return true;
    }

    void resetLevel() {
        bf::engine::reset();
        PlayLayer::resetLevel();
        bf::engine::refreshHud(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (bf::engine::onDestroy(this, player, object)) return;
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        bf::engine::onQuit();
        PlayLayer::onQuit();
    }

    void levelComplete() {
        // Si una simulacion llega al final del nivel, no se debe terminar el nivel de verdad.
        if (bf::engine::simulating()) return;
        bf::engine::reset();
        PlayLayer::levelComplete();
    }
};

// Durante una busqueda no debe sonar nada ni moverse la musica.
class $modify(BFAudio, FMODAudioEngine) {
    void loadAudioState(FMODAudioState& state) {
        if (bf::engine::simulating()) return;
        FMODAudioEngine::loadAudioState(state);
    }

    int playEffect(gd::string path, float speed, float unknown, float volume) {
        if (bf::engine::simulating()) return 0;
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }

    int playEffectAdvanced(
        gd::string path, float speed, float unknown, float volume, float pitch,
        bool fastFourierTransform, bool reverb, int startMillis, int endMillis,
        int fadeIn, int fadeOut, bool loopEnabled, int effectID, bool overrideFlag,
        bool noPreload, int channelID, int uniqueID, float minInterval, int sfxGroup
    ) {
        if (bf::engine::simulating()) return 0;
        return FMODAudioEngine::playEffectAdvanced(
            path, speed, unknown, volume, pitch, fastFourierTransform, reverb,
            startMillis, endMillis, fadeIn, fadeOut, loopEnabled, effectID, overrideFlag,
            noPreload, channelID, uniqueID, minInterval, sfxGroup
        );
    }
};

// Boton del menu: menu principal y pausa.
static CCMenuItemSpriteExtra* makeMenuButton(float size) {
    auto btn = CCMenuItemExt::createSpriteExtra(
        bf::ui::spikeIcon(size),
        [](CCMenuItemSpriteExtra*) { bf::BufferMenu::open(); }
    );
    btn->setID("open-menu-button"_spr);
    return btn;
}

class $modify(BFMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto btn = makeMenuButton(34.f);
        if (auto bottom = this->getChildByID("bottom-menu")) {
            bottom->addChild(btn);
            bottom->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition(CCPoint{34.f, 34.f});
            btn->setPosition(CCPoint{0.f, 0.f});
            menu->addChild(btn);
            this->addChild(menu, 10);
        }
        return true;
    }
};

class $modify(BFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto btn = makeMenuButton(34.f);
        if (auto side = this->getChildByID("right-button-menu")) {
            side->addChild(btn);
            side->updateLayout();
        } else {
            auto ws = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition(CCPoint{ws.width - 34.f, 80.f});
            btn->setPosition(CCPoint{0.f, 0.f});
            menu->addChild(btn);
            this->addChild(menu, 10);
        }
    }
};
