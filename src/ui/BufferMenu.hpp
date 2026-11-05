#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <functional>
#include <string>
#include <vector>

namespace bf {
    using namespace geode::prelude;

    // Menu negro y carmesi con pestanas: Etapas, Registro y Ajustes.
    class BufferMenu : public geode::Popup {
    public:
        static BufferMenu* create();
        static void open();

    protected:
        enum class Tab { Stages = 0, Log = 1, Settings = 2 };

        Tab m_tab = Tab::Stages;
        int m_customMode = 4; // modo que se edita en las etapas Custom (Onda por defecto)
        CCNode* m_content = nullptr;
        CCDrawNode* m_deco = nullptr;
        std::vector<CCNode*> m_buttons;

        bool setupMenu();
        void rebuild();
        void queueRebuild();

        void buildHeader();
        void buildStages();
        void buildLog();
        void buildSettings();

        // Todas las coordenadas son "desde arriba a la izquierda" (x hacia la derecha, y hacia abajo).
        CCMenuItemSpriteExtra* addBtn(CCNode* sprite, float x, float y, std::function<void()> callback);
        void addText(
            std::string const& text, char const* font, float scale, unsigned color,
            float x, float y, CCPoint anchor
        );
        void addStatic(CCNode* node, float x, float y);
        void addStepper(
            float y, int value, int lo, int hi, int stepS, int stepB, bool editable,
            std::function<void(int)> setter, std::string const& shown = ""
        );
        void addToggle(
            float y, std::string const& name, std::string const& desc, bool value,
            std::function<void(bool)> setter
        );
    };
}
