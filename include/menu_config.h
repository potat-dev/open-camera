#pragma once

#include <Menu.h>

#include "sensor.h"

// TODO: rewrite to a single settings class / struct
// TODO: individual settings also as classes / structs
// TODO: do not define vars in header file

const char* modeOptions[] = {"Auto", "Manual", "Expert"};

MenuItem contrast   = {"Contr", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};
MenuItem brightness = {"Bright", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};
MenuItem saturation = {"Satur", MENU_INTEGER, 0, nullptr, 0, -2, 2, 1};

const char* wbOptions[]  = {"Auto", "Sunny", "Cloudy", "Office", "Home"};
MenuItem    whiteBalance = {"White", MENU_SELECT, 0, wbOptions, 5};

const char* effectOptions[] = {"None", "Invert", "Gray", "Red", "Green", "Blue", "Sepia"};
MenuItem    effect          = {"Effect", MENU_SELECT, 0, effectOptions, 7};

MenuItem hFlip = {"FlipH", MENU_TOGGLE, 0};
MenuItem vFlip = {"FlipV", MENU_TOGGLE, 0};

enum CaptureOption : uint8_t {
    CAPTURE_NO       = 0,
    CAPTURE_JPEG     = 1 << 0,
    CAPTURE_RAW      = 1 << 1,
    CAPTURE_RAW_JPEG = CAPTURE_RAW | CAPTURE_JPEG,
    CAPTURE_THUMB    = 1 << 2,
};

const char* captureOptions[] = {"OFF", "JPEG", "RAW", "RAW+JPEG"};

// all useful ASPECT_RATIO_4X3 modes
MenuItem captureQVGA = {"240p", MENU_SELECT, CAPTURE_JPEG, captureOptions, 4};
MenuItem captureVGA  = {"480p", MENU_SELECT, CAPTURE_JPEG, captureOptions, 4};
MenuItem captureSVGA = {"600p", MENU_SELECT, CAPTURE_NO, captureOptions, 4};
MenuItem captureXGA  = {"768p", MENU_SELECT, CAPTURE_JPEG, captureOptions, 4};
MenuItem captureUXGA = {"1200p", MENU_SELECT, CAPTURE_NO, captureOptions, 4};

constexpr size_t captureSizesCount = 5;

const framesize_t captureSizes[captureSizesCount] = {
    FRAMESIZE_QVGA,
    FRAMESIZE_VGA,
    FRAMESIZE_SVGA,
    FRAMESIZE_XGA,
    FRAMESIZE_UXGA,
};

const MenuItem* captureSettings[captureSizesCount] = {
    &captureQVGA,
    &captureVGA,
    &captureSVGA,
    &captureXGA,
    &captureUXGA,
};

MenuItem expCtrl  = {"ExpCtrl", MENU_TOGGLE, 1};
MenuItem gainCtrl = {"GainCtrl", MENU_TOGGLE, 1};
MenuItem colorBar = {"ColorBar", MENU_TOGGLE, 0};
MenuItem whiteBal = {"WhiteBal", MENU_TOGGLE, 1};
MenuItem gainAWB  = {"GainAWB", MENU_TOGGLE, 1};
MenuItem rawGMA   = {"RawGMA", MENU_TOGGLE, 1};
MenuItem aec2     = {"AEC2", MENU_TOGGLE, 0};
MenuItem lenc     = {"LenC", MENU_TOGGLE, 1};
MenuItem bpc      = {"BPC", MENU_TOGGLE, 0};
MenuItem wpc      = {"WPC", MENU_TOGGLE, 1};
MenuItem dcw      = {"DCW", MENU_TOGGLE, 1};

MenuItem menuScale = {"TextSize", MENU_INTEGER, 2, nullptr, 0, 2, 3, 1};

constexpr size_t menuItemsCount = 24;

MenuItem* menuItems[menuItemsCount] = {
    &contrast,
    &brightness,
    &saturation,
    &whiteBalance,
    &effect,
    &hFlip,
    &vFlip,
    &captureQVGA,
    &captureVGA,
    &captureSVGA,
    &captureXGA,
    &captureUXGA,
    &expCtrl,
    &gainCtrl,
    &colorBar,
    &whiteBal,
    &gainAWB,
    &rawGMA,
    &aec2,
    &lenc,
    &bpc,
    &wpc,
    &dcw,
    &menuScale,
};

Menu menu(menuItems, menuItemsCount);
