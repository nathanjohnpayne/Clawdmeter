#pragma once
#include <lvgl.h>
#include <stdint.h>

// Design tokens — single source of truth for UI colors.
//
// Petmeter shows more than one provider's usage, and each gets its own
// palette so a glance at the screen tells you which plan you are looking at.
// Colors are stored as plain hex rather than lv_color_t so the tables stay
// constant-initialized: lv_color_hex() is a function, and a table of calls
// would run at static-init time, before LVGL is up.
//
// Call sites convert at point of use — see the COL_* macros in ui.cpp.

enum theme_mode_t {
    THEME_MODE_CLAUDE = 0,
    THEME_MODE_CODEX,
    THEME_MODE_COUNT,
};

struct Theme {
    uint32_t bg;        // screen background
    uint32_t panel;     // card/zone fill
    uint32_t text;      // primary text
    uint32_t dim;       // secondary text
    uint32_t accent;    // brand color
    uint32_t green;
    uint32_t amber;
    uint32_t red;
    uint32_t bar_bg;    // unfilled bar track
    const char* name;   // shown on the usage screen

    // Claude's identity is a serif display face over sans body copy; ChatGPT's
    // is sans throughout. Set here rather than duplicating every breakpoint's
    // font table -- ui.cpp swaps the serif slots for their nearest sans size
    // after the layout has picked sizes.
    bool sans_only;
};

// The palettes differ in temperature as much as in hue, which is what makes
// them readable apart at arm's length: Claude's greys are warm and its accent
// is terra-cotta; the Codex greys are neutral and its accent is green.
static const Theme THEMES[THEME_MODE_COUNT] = {
    // Claude — Anthropic-inspired, AMOLED-friendly (true black bg).
    {
        .bg     = 0x000000,
        .panel  = 0x1f1f1e,
        .text   = 0xfaf9f5,
        .dim    = 0xb0aea5,
        .accent = 0xd97757,   // brand terra-cotta
        .green  = 0x788c5d,
        .amber  = 0xd97757,
        .red    = 0xc0392b,
        .bar_bg = 0x2a2a28,
        .name   = "Claude",
        .sans_only = false,
    },
    // Codex — ChatGPT-inspired: neutral greys, signature green.
    {
        .bg     = 0x000000,
        .panel  = 0x1a1a1a,
        .text   = 0xececec,
        .dim    = 0x9b9b9b,
        .accent = 0x10a37f,   // ChatGPT green
        .green  = 0x10a37f,
        .amber  = 0xe8a33d,
        .red    = 0xef4146,
        .bar_bg = 0x2a2a2a,
        .name   = "Codex",
        .sans_only = true,
    },
};

// Active palette. Mode is runtime state so the button handler can cycle it
// without a rebuild.
const Theme& theme(void);
theme_mode_t theme_mode(void);
void theme_set_mode(theme_mode_t mode);
theme_mode_t theme_next_mode(void);   // cycles, wrapping at THEME_MODE_COUNT
