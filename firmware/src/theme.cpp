#include "theme.h"

static theme_mode_t active_mode = THEME_MODE_CLAUDE;

const Theme& theme(void) {
    return THEMES[active_mode];
}

theme_mode_t theme_mode(void) {
    return active_mode;
}

void theme_set_mode(theme_mode_t mode) {
    if (mode >= 0 && mode < THEME_MODE_COUNT) active_mode = mode;
}

theme_mode_t theme_next_mode(void) {
    return (theme_mode_t)((active_mode + 1) % THEME_MODE_COUNT);
}
