#pragma once

#include <LovyanGFX.hpp>

enum MenuItemType : uint8_t {
    MENU_TOGGLE,
    MENU_SELECT,
    MENU_INTEGER,
};

struct MenuItem {
    const char* name;
    MenuItemType type;

    // Current value. Meaning depends on type:
    //   MENU_TOGGLE:  0 or 1
    //   MENU_SELECT:  index into `options`
    //   MENU_INTEGER: the raw value
    int value = 0;

    // MENU_SELECT only
    const char* const* options = nullptr;
    uint8_t optionCount = 0;

    // MENU_INTEGER only
    int minValue = 0;
    int maxValue = 0;
    int step = 1;
};

class Menu {
   public:
    Menu(MenuItem** items, uint8_t count) : _items(items), _count(count) {}

    void upHandler() { _editing ? editValue(+1) : moveFocus(-1); }
    void downHandler() { _editing ? editValue(-1) : moveFocus(+1); }

    void selectHandler() {
        MenuItem* it = _items[_focus];
        if (_editing) {
            _editing = false;
        } else if (it->type == MENU_TOGGLE) {
            editValue(+1);
        } else {
            _editing = true;
        }
    }

    void backHandler() {
        _exitRequested = true;
        _editing = false;
        _focus = 0;
    }

    bool wantsExit() {
        bool r = _exitRequested;
        _exitRequested = false;
        return r;
    }

    bool changed() {
        bool e = _changed;
        _changed = false;
        return e;
    }

    void draw(LGFX_Sprite& canvas, int offset, int textSize = 2) {
        int h = canvas.height() - offset * 2;
        int w = canvas.width() - offset * 2;

        canvas.setTextSize(textSize);
        int rowHeight = canvas.fontHeight() + canvas.fontHeight() / 4;
        int visibleRows = h / rowHeight;

        int scroll = 0;
        if ((int)_focus >= visibleRows) scroll = _focus - visibleRows + 1;
        int maxScroll = (int)_count - visibleRows;
        if (maxScroll < 0) maxScroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;

        for (int row = 0; row < visibleRows; row++) {
            int i = scroll + row;
            if (i >= (int)_count) break;

            int rowY = offset + row * rowHeight;
            bool focused = (i == _focus);

            if (focused) {
                canvas.fillRect(offset, rowY, w, rowHeight, _editing ? TFT_DARKGREEN : TFT_NAVY);
                canvas.setTextColor(TFT_WHITE, _editing ? TFT_DARKGREEN : TFT_NAVY);
                // TODO: make BG color optional
            } else {
                canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            }

            canvas.setTextDatum(lgfx::middle_left);
            canvas.drawString(_items[i]->name, offset, rowY + rowHeight / 2);

            char valueStr[24];
            formatValue(_items[i], valueStr, sizeof(valueStr));
            canvas.setTextDatum(lgfx::middle_right);
            canvas.drawString(valueStr, offset + w, rowY + rowHeight / 2);
        }
    }

   private:
    MenuItem** _items;
    uint8_t _count;
    uint8_t _focus = 0;
    bool _editing = false;
    int _editBackup = 0;
    bool _exitRequested = false;
    bool _changed = false;

    void moveFocus(int dir) {
        if (_count == 0) return;
        int next = (int)_focus + dir;
        if (next < 0) next = _count - 1;
        if (next >= (int)_count) next = 0;
        _focus = (uint8_t)next;
    }

    void editValue(int dir) {
        MenuItem* it = _items[_focus];
        if (it->type == MENU_SELECT && it->optionCount > 0) {
            it->value = (it->value + dir + it->optionCount) % it->optionCount;
            _changed = true;
        } else if (it->type == MENU_INTEGER) {
            it->value += dir * it->step;
            if (it->value < it->minValue) it->value = it->maxValue;
            if (it->value > it->maxValue) it->value = it->minValue;
            _changed = true;
        } else if (it->type == MENU_TOGGLE && dir) {
            it->value = !it->value;
            _changed = true;
        }
    }

    static void formatValue(const MenuItem* it, char* out, size_t n) {
        switch (it->type) {
            case MENU_TOGGLE:
                snprintf(out, n, "%s", it->value ? "ON" : "OFF");
                break;
            case MENU_SELECT:
                snprintf(
                    out, n, "%s",
                    (it->options && it->value < it->optionCount) ? it->options[it->value] : "?");
                break;
            case MENU_INTEGER:
                snprintf(out, n, "%d", it->value);
                break;
        }
    }
};
