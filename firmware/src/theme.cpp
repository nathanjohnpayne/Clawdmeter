#include "theme.h"
#include <Preferences.h>
#include <Arduino.h>

// Shares the "clawdmeter" namespace with brightness.cpp -- one namespace for
// the handful of user-chosen settings that should outlive a reboot.
#define PREF_NS   "clawdmeter"
#define PREF_KEY  "mode"
#define PREF_AUTO "autoflip"

static theme_mode_t active_mode = THEME_MODE_CLAUDE;
static bool autoflip = false;

void theme_init(void) {
    Preferences prefs;
    prefs.begin(PREF_NS, true);
    uint8_t saved = prefs.getUChar(PREF_KEY, 0xFF);
    prefs.end();

    if (saved < THEME_MODE_COUNT) active_mode = (theme_mode_t)saved;

    prefs.begin(PREF_NS, true);
    autoflip = prefs.getUChar(PREF_AUTO, 0) != 0;
    prefs.end();

    Serial.printf("Theme init: mode=%s autoflip=%s\n",
                  THEMES[active_mode].name, autoflip ? "on" : "off");
}

const Theme& theme(void) {
    return THEMES[active_mode];
}

theme_mode_t theme_mode(void) {
    return active_mode;
}

void theme_set_mode(theme_mode_t mode) {
    if (mode < 0 || mode >= THEME_MODE_COUNT) return;
    if (mode == active_mode) return;         // no write when nothing changed
    active_mode = mode;

    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putUChar(PREF_KEY, (uint8_t)active_mode);
    prefs.end();
}

bool theme_autoflip(void) {
    return autoflip;
}

void theme_set_autoflip(bool on) {
    if (on == autoflip) return;              // no write when nothing changed
    autoflip = on;

    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putUChar(PREF_AUTO, autoflip ? 1 : 0);
    prefs.end();
}

theme_mode_t theme_next_mode(void) {
    return (theme_mode_t)((active_mode + 1) % THEME_MODE_COUNT);
}
