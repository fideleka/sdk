#include "ui.h"
#include <iostream>

namespace {
int checks = 0;
int failures = 0;
constexpr uint16_t brown = 0xa145;
constexpr uint16_t green = 0x07e0;
const menu_icon_t icon = {};

void check(bool condition, const String& label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}
const TextDraw* find(const Arduino_GFX& canvas, const String& text) {
    for (const auto& draw : canvas.draws) {
        if (draw.text == text) return &draw;
    }
    return nullptr;
}
void expectColor(const Arduino_GFX& canvas, const String& text, uint16_t expected, const String& label) {
    const auto* draw = find(canvas, text);
    check(draw && draw->color == expected, label + " / " + text);
    if (draw && draw->color != expected) {
        std::cerr << "  foreground=" << draw->color << " expected=" << expected << '\n';
    }
}

void variants() {
    // Cross-product: previous brown row, short/marquee title, optional icons/postfix,
    // custom selection color, first/later selected row, and cursor movement.
    for (bool icons : {false, true}) {
        for (bool postfix : {false, true}) {
            for (bool longTitle : {false, true}) {
                for (uint16_t selection : {lilka::colors::White, lilka::colors::Yellow}) {
                    const String selected = longTitle ? "Selected title that must marquee" : "Selected";
                    const String label = String(icons ? "icons " : "no-icons ") +
                                         (postfix ? "postfix " : "no-postfix ") +
                                         (longTitle ? "marquee " : "fits ") + std::to_string(selection);
                    lilka::Menu menu("Menu");
                    menu.setColor(selection);
                    menu.addItem("Brown", icons ? &icon : nullptr, brown);
                    menu.addItem(selected, icons ? &icon : nullptr, green, postfix ? "P" : "");
                    menu.addItem("Tail", nullptr, lilka::colors::Blue);
                    Arduino_GFX canvas;
                    for (int cursor : {1, 0, 2, 1}) {
                        menu.setCursor(cursor);
                        hostTime += 1000;
                        menu.draw(&canvas);
                        expectColor(canvas, "Menu", selection, label);
                        expectColor(canvas, "Brown", cursor == 0 ? selection : brown, label);
                        expectColor(canvas, selected, cursor == 1 ? selection : green, label);
                        expectColor(canvas, "Tail", cursor == 2 ? selection : lilka::colors::Blue, label);
                        if (postfix) expectColor(canvas, "P", selection, label + " postfix unchanged");
                        const auto* draw = find(canvas, selected);
                        check(draw && draw->font == FONT_10x20 && draw->size == 1, label + " item font/size");
                        check(draw && draw->y == 104, label + " row layout");
                        if (!longTitle || cursor != 1) {
                            const int width = 240 - 32 - (postfix ? 10 : 0) - 8 - 4 - 4;
                            check(draw && draw->x == 32 && draw->boundWidth == width, label + " title bounds");
                        }
                    }
                }
            }
        }
    }
}

void scrolling() {
    lilka::Menu menu("Scrolling");
    std::vector<String> titles;
    for (int i = 0; i < 8; ++i) {
        titles.push_back("Item" + std::to_string(i));
        menu.addItem(titles.back(), i % 2 ? &icon : nullptr, i % 2 ? green : brown);
    }
    Arduino_GFX canvas;
    // update() is the real production scrolling path; no private-state test hooks.
    for (int cursor : {0, 4, 5, 7, 2, 0}) {
        menu.setCursor(cursor);
        menu.update();
        menu.draw(&canvas);
        const int scroll = cursor == 5 ? 1 : cursor == 7 ? 3 : cursor == 2 ? 2 : 0;
        for (int i = 0; i < 8; ++i) {
            const auto* draw = find(canvas, titles[i]);
            if (i < scroll || i >= scroll + 5) {
                check(!draw, "scrolled-off row hidden");
            } else {
                expectColor(canvas, titles[i], i == cursor ? lilka::colors::White : i % 2 ? green : brown,
                            "scroll cursor=" + std::to_string(cursor));
                check(draw && draw->y == 80 + (i - scroll) * 24, "scroll row layout");
            }
        }
    }
}

void pageNavigation() {
    lilka::Menu menu("Paging");
    for(int i=0;i<12;++i)menu.addItem("row "+std::to_string(i),nullptr,brown);
    for(int cursor : {0,1,4,5,6,11}) {
        menu.setCursor(cursor);lilka::controller.state={};lilka::controller.state.left.justPressed=true;
        menu.update();check(menu.getCursor()==(cursor==0 ? 11 : cursor<=5 ? 0 : cursor-5),"PageUp floor/wrap");
    }
    lilka::controller.state={};
}

void marqueeHeader() {
    lilka::Menu menu("Menu header that must use a marquee");
    menu.setColor(lilka::colors::Yellow);
    menu.addItem("Brown", nullptr, brown);
    menu.addItem("Selected", nullptr, green);
    Arduino_GFX canvas;
    for (int cursor : {0, 1}) {
        menu.setCursor(cursor);
        menu.draw(&canvas);
        expectColor(canvas, "Menu header that must use a marquee", lilka::colors::Yellow, "header marquee");
        expectColor(canvas, "Brown", cursor == 0 ? lilka::colors::Yellow : brown, "first row/header marquee");
        expectColor(canvas, "Selected", cursor == 1 ? lilka::colors::Yellow : green, "later row/header marquee");
    }
}
} // namespace

int main() {
    variants();
    scrolling();
    marqueeHeader();
    pageNavigation();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
