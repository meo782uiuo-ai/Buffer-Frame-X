#include "BufferMenu.hpp"

#include <algorithm>
#include <cmath>

#include "../Config.hpp"
#include "../Engine.hpp"
#include "Theme.hpp"

using namespace geode::prelude;

namespace bf {
    namespace {
        constexpr float kW = 380.f;
        constexpr float kH = 262.f;
        constexpr float kCardTop = 78.f;
        constexpr float kCardBottom = 246.f;

        // Estrella de 4 puntas (decoracion).
        void star(CCDrawNode* d, float cx, float cy, float r, unsigned color, float alpha) {
            CCPoint p[8];
            for (int i = 0; i < 8; ++i) {
                float a = ui::kPi / 2.f + ui::kPi * static_cast<float>(i) / 4.f;
                float rr = (i % 2 == 0) ? r : r * 0.32f;
                p[i] = CCPoint{cx + rr * std::cos(a), cy + rr * std::sin(a)};
            }
            d->drawPolygon(p, 8, ui::hex4(color, alpha), 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
        }
    }

    BufferMenu* BufferMenu::create() {
        auto ret = new BufferMenu();
        if (ret->setupMenu()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void BufferMenu::open() {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene || scene->getChildByID("buffer-frame-menu"_spr)) return;
        if (auto menu = BufferMenu::create()) menu->show();
    }

    bool BufferMenu::setupMenu() {
        if (!Popup::init(kW, kH, "square01_001.png")) return false;
        this->setID("buffer-frame-menu"_spr);

        // El fondo y el borde se dibujan a mano; se esconde lo de serie.
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);

        this->rebuild();
        return true;
    }

    // Los cambios se aplican en el siguiente tick, fuera del callback del boton.
    void BufferMenu::queueRebuild() {
        Loader::get()->queueInMainThread([self = Ref<BufferMenu>(this)]() {
            if (self->getParent()) self->rebuild();
        });
    }

    CCMenuItemSpriteExtra* BufferMenu::addBtn(CCNode* sprite, float x, float y, std::function<void()> callback) {
        auto item = CCMenuItemExt::createSpriteExtra(
            sprite,
            [cb = std::move(callback)](CCMenuItemSpriteExtra*) { cb(); }
        );
        m_buttonMenu->addChildAtPosition(item, Anchor::BottomLeft, CCPoint{x, kH - y});
        m_buttons.push_back(item);
        return item;
    }

    void BufferMenu::addText(
        std::string const& text, char const* font, float scale, unsigned color,
        float x, float y, CCPoint anchor
    ) {
        auto l = ui::label(text, font, scale, color, anchor);
        l->setPosition(CCPoint{x, kH - y});
        m_content->addChild(l, 3);
    }

    void BufferMenu::addStatic(CCNode* node, float x, float y) {
        node->setPosition(CCPoint{x, kH - y});
        m_content->addChild(node, 3);
    }

    void BufferMenu::addStepper(
        float y, int value, int lo, int hi, int stepS, int stepB, bool editable,
        std::function<void(int)> setter, std::string const& shown
    ) {
        struct Spec { int delta; float x; float w; };
        Spec specs[4] = {
            {-stepB, 209.f, 26.f}, {-stepS, 237.f, 22.f}, {stepS, 307.f, 22.f}, {stepB, 335.f, 26.f}
        };
        for (auto const& s : specs) {
            auto spr = ui::pill(
                fmt::format("{:+d}", s.delta), s.w, 16.f,
                editable ? ui::col::raised : ui::col::panel,
                editable ? ui::col::line : ui::col::off,
                editable ? ui::col::text : ui::col::dim, .28f
            );
            if (editable) {
                int d = s.delta;
                addBtn(spr, s.x, y, [this, d, value, lo, hi, setter]() {
                    setter(std::clamp(value + d, lo, hi));
                    cfg().save();
                    engine::onConfigChanged();
                    this->queueRebuild();
                });
            } else {
                addStatic(spr, s.x, y);
            }
        }
        addStatic(
            ui::pill(
                shown.empty() ? std::to_string(value) : shown, 40.f, 16.f, ui::col::black,
                editable ? ui::col::crimson : ui::col::line, ui::col::text, .3f
            ),
            272.f, y
        );

        // Barra fina bajo el stepper (solo visual).
        float frac = hi > lo ? static_cast<float>(value - lo) / static_cast<float>(hi - lo) : 0.f;
        float by = kH - (y + 13.f);
        ui::rrect(m_deco, 196.f, by, 152.f, 3.f, 1.5f, ui::col::off);
        if (frac > 0.f) {
            ui::rrect(
                m_deco, 196.f, by, std::max(3.f, 152.f * frac), 3.f, 1.5f,
                editable ? ui::col::crimson : ui::col::dim
            );
        }
    }

    void BufferMenu::addToggle(
        float y, std::string const& name, std::string const& desc, bool value,
        std::function<void(bool)> setter
    ) {
        addText(name, "chatFont.fnt", .75f, ui::col::text, 28.f, y - 4.f, CCPoint{0.f, .5f});
        addText(desc, "chatFont.fnt", .5f, ui::col::dim, 28.f, y + 8.f, CCPoint{0.f, .5f});
        auto p = ui::pill(
            value ? "ON" : "OFF", 46.f, 16.f,
            value ? ui::col::deep : ui::col::off,
            value ? ui::col::crimson : ui::col::line,
            value ? ui::col::text : ui::col::dim, .3f
        );
        addBtn(p, kW - 50.f, y, [this, value, setter]() {
            setter(!value);
            cfg().save();
            engine::onConfigChanged();
            this->queueRebuild();
        });
    }

    void BufferMenu::rebuild() {
        for (auto b : m_buttons) b->removeFromParent();
        m_buttons.clear();
        if (m_content) m_content->removeFromParent();

        m_content = CCNode::create();
        m_content->setContentSize(CCSize{kW, kH});
        m_mainLayer->addChild(m_content, 1);

        m_deco = CCDrawNode::create();
        m_content->addChild(m_deco, 0);

        // Marco: brillo suave, panel negro y borde carmesi.
        ui::rrect(m_deco, -4.f, -4.f, kW + 8.f, kH + 8.f, 24.f, ui::col::glow2);
        ui::rrect(m_deco, -2.f, -2.f, kW + 4.f, kH + 4.f, 21.f, ui::col::glow1);
        ui::rrect(m_deco, 0.f, 0.f, kW, kH, 18.f, ui::col::black, 1.5f, ui::col::crimson);
        // Cabecera y tarjeta del cuerpo.
        ui::rrect(m_deco, 14.f, kH - 49.f, kW - 28.f, 1.f, .5f, ui::col::line);
        ui::rrect(
            m_deco, 14.f, kH - kCardBottom, kW - 28.f, kCardBottom - kCardTop, 12.f,
            ui::col::panel, 1.f, ui::col::line
        );
        // Destellos.
        star(m_deco, 172.f, kH - 13.f, 4.f, ui::col::hot, 0.9f);
        star(m_deco, 184.f, kH - 31.f, 2.6f, ui::col::warn, 0.8f);
        star(m_deco, 203.f, kH - 16.f, 2.f, ui::col::crimson, 0.9f);
        star(m_deco, kW - 150.f, kH - 40.f, 2.f, ui::col::hot, 0.6f);

        this->buildHeader();
        switch (m_tab) {
            case Tab::Stages: this->buildStages(); break;
            case Tab::Log: this->buildLog(); break;
            case Tab::Settings: this->buildSettings(); break;
        }

        addText(
            "Solo para practica: da ban en rankeds.",
            "chatFont.fnt", .5f, ui::col::dim, kW / 2.f, 255.f, CCPoint{.5f, .5f}
        );
    }

    void BufferMenu::buildHeader() {
        auto& c = cfg();

        addStatic(ui::spikeIcon(28.f), 28.f, 25.f);
        addText("Buffer Frame", "bigFont.fnt", .5f, ui::col::text, 48.f, 19.f, CCPoint{0.f, .5f});
        addText(
            fmt::format("Etapa: {}", c.stageName(c.stage)),
            "chatFont.fnt", .6f, ui::col::hot, 48.f, 35.f, CCPoint{0.f, .5f}
        );

        // Interruptor "Buffer on/off".
        bool on = c.enabled;
        auto node = ui::pill(
            "", 84.f, 18.f,
            on ? ui::col::deep : ui::col::off,
            on ? ui::col::crimson : ui::col::line, ui::col::text
        );
        auto dot = CCDrawNode::create();
        ui::circle(dot, 11.f, 9.f, 3.f, on ? ui::col::hot : ui::col::dim);
        node->addChild(dot, 2);
        auto txt = ui::label(
            on ? "Buffer on" : "Buffer off", "chatFont.fnt", .65f,
            on ? ui::col::text : ui::col::dim, CCPoint{0.f, .5f}
        );
        txt->setPosition(CCPoint{20.f, 9.f});
        node->addChild(txt, 2);
        addBtn(node, kW - 78.f, 24.f, [this]() {
            cfg().enabled = !cfg().enabled;
            cfg().save();
            engine::onConfigChanged();
            this->queueRebuild();
        });

        // Cerrar.
        addBtn(
            ui::pill("X", 22.f, 22.f, ui::col::raised, ui::col::line, ui::col::text, .34f, "bigFont.fnt", 6.f),
            kW - 22.f, 24.f, [this]() { this->onClose(nullptr); }
        );

        // Pestanas.
        char const* names[3] = {"Etapas", "Registro", "Ajustes"};
        for (int i = 0; i < 3; ++i) {
            bool active = static_cast<int>(m_tab) == i;
            auto tab = ui::pill(
                names[i], 96.f, 22.f,
                active ? ui::col::deep : ui::col::panel,
                active ? ui::col::crimson : ui::col::line,
                active ? ui::col::text : ui::col::dim, .36f
            );
            addBtn(tab, kW / 2.f + static_cast<float>(i - 1) * 104.f, 61.f, [this, i]() {
                m_tab = static_cast<Tab>(i);
                this->queueRebuild();
            });
        }
    }

    void BufferMenu::buildStages() {
        auto& c = cfg();
        int st = c.stage;
        bool custom = st >= kFirstCustom;
        int slot = st - kFirstCustom;

        addText(c.stageName(st), "bigFont.fnt", .55f, ui::col::text, 28.f, 96.f, CCPoint{0.f, .5f});
        addText(
            fmt::format("{}/{}", st + 1, kStageCount),
            "bigFont.fnt", .4f, ui::col::hot, kW - 28.f, 96.f, CCPoint{1.f, .5f}
        );
        addText(
            fmt::format(
                "Retrasa clicks hasta {} ticks  |  onda {} ticks",
                c.cap(Mode::Cube), c.cap(Mode::Wave)
            ),
            "chatFont.fnt", .6f, ui::col::dim, 28.f, 114.f, CCPoint{0.f, .5f}
        );

        if (custom) {
            addBtn(
                ui::pill("Reiniciar", 64.f, 16.f, ui::col::raised, ui::col::line, ui::col::text, .3f),
                kW - 62.f, 114.f,
                [this, slot]() {
                    auto& cc = cfg();
                    cc.custom[slot][0] = 16;
                    for (int i = 1; i <= kModes; ++i) cc.custom[slot][i] = 0;
                    cc.save();
                    engine::onConfigChanged();
                    this->queueRebuild();
                }
            );
        }

        // Selector de etapa (11 fichas en dos filas).
        for (int i = 0; i < kStageCount; ++i) {
            int row = i / 6;
            int cx = i % 6;
            float x = 28.f + static_cast<float>(cx) * 54.f + 24.f;
            float y = 138.f + static_cast<float>(row) * 22.f;
            bool sel = i == st;
            auto chip = ui::pill(
                c.stageName(i), 48.f, 18.f,
                sel ? ui::col::crimson : ui::col::raised,
                sel ? ui::col::hot : ui::col::line,
                sel ? ui::col::text : ui::col::dim, .28f
            );
            addBtn(chip, x, y, [this, i]() {
                cfg().stage = i;
                cfg().save();
                engine::onConfigChanged();
                this->queueRebuild();
            });
        }

        // Ticks maximos: todos los modos.
        int allVal = custom ? c.custom[slot][0] : c.cap(Mode::Cube);
        addText("Todos los modos", "chatFont.fnt", .7f, ui::col::text, 28.f, 186.f, CCPoint{0.f, .5f});
        addStepper(186.f, allVal, 0, kMaxTicks, 1, 4, custom, [slot](int v) {
            cfg().custom[slot][0] = v;
        });

        // Ticks maximos: un modo en concreto.
        auto mode = static_cast<Mode>(m_customMode);
        int modeVal = custom ? c.custom[slot][1 + m_customMode] : c.cap(mode);
        addBtn(
            ui::pill(
                fmt::format("Modo: {}", modeName(mode)), 104.f, 18.f,
                ui::col::raised, ui::col::line, ui::col::text, .3f
            ),
            80.f, 208.f,
            [this]() {
                m_customMode = (m_customMode + 1) % kModes;
                this->queueRebuild();
            }
        );
        int mi = m_customMode;
        addStepper(
            208.f, modeVal, 0, kMaxTicks, 1, 4, custom,
            [slot, mi](int v) { cfg().custom[slot][1 + mi] = v; },
            (custom && modeVal == 0) ? std::string("= todos") : std::string()
        );

        if (!custom) {
            addText(
                "Elige Custom 1, 2 o 3 para editar los ticks.",
                "chatFont.fnt", .55f, ui::col::dim, 28.f, 230.f, CCPoint{0.f, .5f}
            );
        } else if (c.exceedsWarning()) {
            addText(
                fmt::format("Mas de {} ticks puede dar lag o no encontrar salvacion.", kWarnTicks),
                "chatFont.fnt", .55f, ui::col::warn, 28.f, 230.f, CCPoint{0.f, .5f}
            );
        } else {
            addText(
                "0 en un modo = usar el valor de Todos los modos.",
                "chatFont.fnt", .55f, ui::col::dim, 28.f, 230.f, CCPoint{0.f, .5f}
            );
        }
    }

    void BufferMenu::buildLog() {
        auto const& st = engine::stats();
        auto const& lg = engine::log();

        addText(
            fmt::format("Salvados {}    Fallidos {}    Tiempo agotado {}", st.saved, st.failed, st.timeouts),
            "chatFont.fnt", .65f, ui::col::text, 28.f, 96.f, CCPoint{0.f, .5f}
        );
        float avgMs = st.searches > 0 ? st.msSum / static_cast<float>(st.searches) : 0.f;
        float avgDelay = st.saved > 0 ? static_cast<float>(st.delaySum) / static_cast<float>(st.saved) : 0.f;
        addText(
            fmt::format("Peligros {}   Busqueda {:.1f} ms   Retraso medio {:.1f}", engine::hazardCount(), avgMs, avgDelay),
            "chatFont.fnt", .55f, ui::col::dim, 28.f, 112.f, CCPoint{0.f, .5f}
        );
        addBtn(
            ui::pill("Limpiar", 60.f, 16.f, ui::col::raised, ui::col::line, ui::col::text, .3f),
            kW - 60.f, 104.f,
            [this]() {
                engine::clearLog();
                this->queueRebuild();
            }
        );

        if (lg.empty()) {
            addText(
                "Sin busquedas todavia.", "chatFont.fnt", .7f, ui::col::dim,
                kW / 2.f, 176.f, CCPoint{.5f, .5f}
            );
            addText(
                "Cuando mueras por un click temprano, aqui aparece lo que hizo el mod.",
                "chatFont.fnt", .5f, ui::col::dim, kW / 2.f, 192.f, CCPoint{.5f, .5f}
            );
            return;
        }

        addText("Ultimas busquedas", "chatFont.fnt", .5f, ui::col::dim, 28.f, 132.f, CCPoint{0.f, .5f});
        int shown = 0;
        for (int i = static_cast<int>(lg.size()) - 1; i >= 0 && shown < 6; --i, ++shown) {
            auto const& e = lg[i];
            std::string what;
            unsigned color = ui::col::text;
            switch (e.outcome) {
                case Outcome::Saved:
                    what = fmt::format("salvado +{} ticks", e.delay);
                    color = ui::col::ok;
                    break;
                case Outcome::Failed:
                    what = "sin salvacion";
                    color = ui::col::hot;
                    break;
                case Outcome::Timeout:
                    what = "tiempo agotado";
                    color = ui::col::warn;
                    break;
            }
            addText(
                fmt::format(
                    "{}%   {}   {}   ({} pruebas, {:.1f} ms)",
                    e.percent, modeName(e.mode), what, e.tries, e.ms
                ),
                "chatFont.fnt", .55f, color, 28.f, 150.f + static_cast<float>(shown) * 16.f, CCPoint{0.f, .5f}
            );
        }
    }

    void BufferMenu::buildSettings() {
        auto& c = cfg();

        addToggle(100.f, "Buffer activo", "Corrige clicks tempranos (cheat de prueba)", c.enabled,
            [](bool v) { cfg().enabled = v; });
        addToggle(124.f, "Modo adaptativo", "Aprende tu retraso tipico y acota la busqueda", c.adaptive,
            [](bool v) { cfg().adaptive = v; });
        addToggle(148.f, "Etiqueta en nivel", "Muestra BUF ON en la esquina mientras juegas", c.hud,
            [](bool v) { cfg().hud = v; });

        addText("Margen de seguridad (ticks)", "chatFont.fnt", .65f, ui::col::text, 28.f, 198.f, CCPoint{0.f, .5f});
        addStepper(198.f, c.margin, 10, 90, 1, 5, true, [](int v) { cfg().margin = v; });

        addText("Tiempo de busqueda (ms)", "chatFont.fnt", .65f, ui::col::text, 28.f, 222.f, CCPoint{0.f, .5f});
        addStepper(222.f, c.budgetMs, 10, 250, 5, 20, true, [](int v) { cfg().budgetMs = v; });
    }
}
