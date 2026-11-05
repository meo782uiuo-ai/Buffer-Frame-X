#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Paleta y piezas de dibujo del menu: negro y rojo carmesi.
// Todo se dibuja con CCDrawNode (colores opacos) para no depender de texturas.
namespace bf::ui {
    using namespace geode::prelude;

    constexpr float kPi = 3.14159265358979f;

    namespace col {
        constexpr unsigned black = 0x0A0709;
        constexpr unsigned panel = 0x130B0E;
        constexpr unsigned raised = 0x1E1217;
        constexpr unsigned line = 0x3A1220;
        constexpr unsigned crimson = 0xDC143C;
        constexpr unsigned hot = 0xFF3355;
        constexpr unsigned deep = 0x5A0A1C;
        constexpr unsigned glow1 = 0x2B0912;
        constexpr unsigned glow2 = 0x1A060C;
        constexpr unsigned text = 0xF4E9EB;
        constexpr unsigned dim = 0x9C8388;
        constexpr unsigned ok = 0x3DDC84;
        constexpr unsigned warn = 0xFFB020;
        constexpr unsigned off = 0x2A1A1F;
    }

    inline ccColor4F hex4(unsigned v, float a = 1.f) {
        return ccColor4F{
            static_cast<float>((v >> 16) & 0xFF) / 255.f,
            static_cast<float>((v >> 8) & 0xFF) / 255.f,
            static_cast<float>(v & 0xFF) / 255.f,
            a
        };
    }

    inline ccColor3B hex3(unsigned v) {
        return ccColor3B{
            static_cast<GLubyte>((v >> 16) & 0xFF),
            static_cast<GLubyte>((v >> 8) & 0xFF),
            static_cast<GLubyte>(v & 0xFF)
        };
    }

    // Puntos de un rectangulo redondeado (sentido antihorario, convexo).
    inline std::vector<CCPoint> roundedPoints(float x, float y, float w, float h, float r, int seg = 6) {
        std::vector<CCPoint> v;
        r = std::min(r, std::min(w, h) / 2.f);
        auto arc = [&](float cx, float cy, float a0) {
            for (int i = 0; i <= seg; ++i) {
                float a = a0 + (kPi / 2.f) * static_cast<float>(i) / static_cast<float>(seg);
                v.push_back(CCPoint{cx + r * std::cos(a), cy + r * std::sin(a)});
            }
        };
        arc(x + w - r, y + r, -kPi / 2.f);
        arc(x + w - r, y + h - r, 0.f);
        arc(x + r, y + h - r, kPi / 2.f);
        arc(x + r, y + r, kPi);
        return v;
    }

    inline void rrect(CCDrawNode* n, float x, float y, float w, float h, float r,
                      unsigned fill, float borderWidth = 0.f, unsigned border = 0) {
        auto pts = roundedPoints(x, y, w, h, r);
        bool hasBorder = borderWidth > 0.f;
        n->drawPolygon(
            pts.data(), static_cast<unsigned int>(pts.size()),
            hex4(fill), hasBorder ? borderWidth : 0.f,
            hasBorder ? hex4(border) : ccColor4F{0.f, 0.f, 0.f, 0.f}
        );
    }

    inline void circle(CCDrawNode* n, float cx, float cy, float r, unsigned fill, float borderWidth = 0.f, unsigned border = 0) {
        std::vector<CCPoint> pts;
        for (int i = 0; i < 28; ++i) {
            float a = 2.f * kPi * static_cast<float>(i) / 28.f;
            pts.push_back(CCPoint{cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        bool hasBorder = borderWidth > 0.f;
        n->drawPolygon(
            pts.data(), static_cast<unsigned int>(pts.size()),
            hex4(fill), hasBorder ? borderWidth : 0.f,
            hasBorder ? hex4(border) : ccColor4F{0.f, 0.f, 0.f, 0.f}
        );
    }

    inline CCLabelBMFont* label(std::string const& text, char const* font, float scale, unsigned color, CCPoint anchor = CCPoint{.5f, .5f}) {
        auto l = CCLabelBMFont::create(text.c_str(), font);
        l->setScale(scale);
        l->setColor(hex3(color));
        l->setAnchorPoint(anchor);
        return l;
    }

    // Boton redondeado (nodo listo para meter en un CCMenuItemSpriteExtra).
    inline CCNode* pill(std::string const& text, float w, float h, unsigned fill, unsigned border,
                        unsigned textColor, float fontScale = .4f, char const* font = "bigFont.fnt", float radius = -1.f) {
        auto node = CCNode::create();
        node->setContentSize(CCSize{w, h});
        node->setAnchorPoint(CCPoint{.5f, .5f});
        node->ignoreAnchorPointForPosition(false);

        auto d = CCDrawNode::create();
        rrect(d, 0.f, 0.f, w, h, radius < 0.f ? h / 2.f : radius, fill, 1.f, border);
        node->addChild(d);

        if (!text.empty()) {
            auto l = label(text, font, fontScale, textColor);
            float natural = l->getContentSize().width * fontScale;
            float maxW = w - 10.f;
            if (natural > maxW && natural > 0.f) l->setScale(fontScale * maxW / natural);
            l->setPosition(CCPoint{w / 2.f, h / 2.f});
            node->addChild(l);
        }
        return node;
    }

    // Icono del mod: circulo oscuro con borde carmesi y un pincho.
    inline CCNode* spikeIcon(float s) {
        auto node = CCNode::create();
        node->setContentSize(CCSize{s, s});
        node->setAnchorPoint(CCPoint{.5f, .5f});
        node->ignoreAnchorPointForPosition(false);

        auto d = CCDrawNode::create();
        circle(d, s / 2.f, s / 2.f, s / 2.f - 1.f, col::black, 1.5f, col::crimson);
        CCPoint tri[3] = {
            CCPoint{s * 0.50f, s * 0.78f},
            CCPoint{s * 0.24f, s * 0.26f},
            CCPoint{s * 0.76f, s * 0.26f},
        };
        d->drawPolygon(tri, 3, hex4(col::crimson), 1.f, hex4(col::hot));
        CCPoint shine[3] = {
            CCPoint{s * 0.50f, s * 0.64f},
            CCPoint{s * 0.40f, s * 0.36f},
            CCPoint{s * 0.50f, s * 0.36f},
        };
        d->drawPolygon(shine, 3, hex4(col::hot), 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
        node->addChild(d);
        return node;
    }
}
