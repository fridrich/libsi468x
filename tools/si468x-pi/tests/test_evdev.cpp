// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
// Tests of the evdev key map: defaults, overrides and errors of
// the plugin's "input" entry. The device handling needs a remote and is not
// covered here.
#include "remote_config.hpp"

#include <cassert>
#include <iostream>
#include <linux/input-event-codes.h>
#include <string>

static int actionOf(const RemoteConfig& cfg, int code)
{
    int action = 0;
    assert(lookupKey(cfg, code, action));
    return action;
}

int main()
{
    RemoteConfig cfg;
    std::string error;

    // no config at all: both remotes work out of the box
    assert(parseRemoteConfig("", cfg, error));
    assert(actionOf(cfg, KEY_UP) == SI468X_PI_UP);
    assert(actionOf(cfg, KEY_DOWN) == SI468X_PI_DOWN);
    assert(actionOf(cfg, KEY_LEFT) == SI468X_PI_LEFT);
    assert(actionOf(cfg, KEY_RIGHT) == SI468X_PI_RIGHT);
    assert(actionOf(cfg, KEY_ENTER) == SI468X_PI_ENTER);
    assert(actionOf(cfg, KEY_MENU) == SI468X_PI_MENU);
    assert(actionOf(cfg, KEY_BACK) == SI468X_PI_LEFT);
    assert(actionOf(cfg, KEY_POWER) == SI468X_PI_QUIT);
    assert(cfg.grab && cfg.device.empty() && cfg.name.empty());

    // the codes the remote actually sends: on/off is KEY_SLEEP, home
    // KEY_HOMEPAGE, menu KEY_COMPOSE, back KEY_ESC, volume and mute the
    // consumer control codes, mouse mode KEY_F19
    assert(actionOf(cfg, KEY_SLEEP) == SI468X_PI_QUIT);
    assert(actionOf(cfg, KEY_HOMEPAGE) == SI468X_PI_HOME);
    assert(actionOf(cfg, KEY_HOME) == SI468X_PI_HOME);
    assert(actionOf(cfg, KEY_COMPOSE) == SI468X_PI_MENU);
    assert(actionOf(cfg, KEY_ESC) == SI468X_PI_LEFT);
    assert(actionOf(cfg, KEY_WAKEUP) == -1);        // the same button after resume
    assert(actionOf(cfg, KEY_F19) == -1);           // mouse mode switch
    assert(actionOf(cfg, KEY_VOLUMEUP) == SI468X_PI_VOLUME_UP);
    assert(actionOf(cfg, KEY_VOLUMEDOWN) == SI468X_PI_VOLUME_DOWN);
    assert(actionOf(cfg, KEY_MUTE) == SI468X_PI_MUTE);

    // the OSMC remote, which sends keyboard keys instead: volume on minus and
    // equal, context on C, info on I, and play/pause and stop
    assert(actionOf(cfg, KEY_MINUS) == SI468X_PI_VOLUME_DOWN);
    assert(actionOf(cfg, KEY_EQUAL) == SI468X_PI_VOLUME_UP);
    assert(actionOf(cfg, KEY_STOP) == -1);
    assert(actionOf(cfg, KEY_PLAYPAUSE) == -1);
    assert(actionOf(cfg, KEY_C) == SI468X_PI_MENU);
    assert(actionOf(cfg, KEY_I) == SI468X_PI_MENU);

    int action = 0;
    assert(!lookupKey(cfg, BTN_MOUSE, action));     // clicks in mouse mode
    assert(!lookupKey(cfg, BTN_RIGHT, action));
    assert(!lookupKey(cfg, 42, action));

    // the JSON object of the "input" array, "plugin" is not the plugin's
    // business
    assert(parseRemoteConfig(R"({"plugin":"evdev"})", cfg, error));
    assert(actionOf(cfg, KEY_UP) == SI468X_PI_UP);

    // remap for another remote: by name, by number, disabled, and a default
    // that is not mentioned stays
    assert(parseRemoteConfig(
            R"({"map":{"KEY_POWER":"menu","KEY_SLEEP":"right","108":"none",)"
            R"("KEY_ENTER":"up","KEY_STOP":"mute"}})",
            cfg, error));
    assert(actionOf(cfg, KEY_POWER) == SI468X_PI_MENU);
    assert(actionOf(cfg, KEY_SLEEP) == SI468X_PI_RIGHT);
    assert(actionOf(cfg, KEY_DOWN) == -1);
    assert(actionOf(cfg, KEY_ENTER) == SI468X_PI_UP);
    assert(actionOf(cfg, KEY_UP) == SI468X_PI_UP);
    assert(actionOf(cfg, KEY_STOP) == SI468X_PI_MUTE);
    assert(actionOf(cfg, KEY_VOLUMEUP) == SI468X_PI_VOLUME_UP);  // not mentioned: default

    // device selection
    assert(parseRemoteConfig(
            R"({"device":"/dev/input/event7","name":"remote","grab":false})", cfg, error));
    assert(cfg.device == "/dev/input/event7");
    assert(cfg.name == "remote");
    assert(!cfg.grab);

    // a mistake is reported and leaves the defaults in place
    assert(!parseRemoteConfig(R"({"map":{"KEY_NOSUCH":"up"}})", cfg, error));
    assert(!error.empty());
    assert(actionOf(cfg, KEY_UP) == SI468X_PI_UP);
    assert(!parseRemoteConfig(R"({"map":{"103":"wheee"}})", cfg, error));
    assert(!parseRemoteConfig(R"({"map":{"103":1}})", cfg, error));
    assert(!parseRemoteConfig(R"({"map":[]})", cfg, error));
    assert(!parseRemoteConfig(R"({"grab":"yes"})", cfg, error));
    assert(!parseRemoteConfig(R"({"device":7})", cfg, error));
    assert(!parseRemoteConfig(R"([{"plugin":"evdev"}])", cfg, error));

    // names used in "map" and in the log
    int code = 0;
    assert(keyCodeFromName("KEY_UP", code) && code == KEY_UP);
    assert(keyCodeFromName("103", code) && code == 103);
    assert(!keyCodeFromName("KEY_UP ", code));
    assert(!keyCodeFromName("", code));
    assert(!keyCodeFromName("ups", code));
    assert(!keyCodeFromName("9999", code));
    assert(std::string(keyName(KEY_ENTER)) == "KEY_ENTER");
    assert(keyName(42) == nullptr);
    assert(std::string(actionName(SI468X_PI_QUIT)) == "quit");
    assert(std::string(actionName(SI468X_PI_VOLUME_UP)) == "volume_up");
    assert(std::string(actionName(SI468X_PI_MUTE)) == "mute");
    assert(std::string(actionName(SI468X_PI_HOME)) == "home");
    assert(actionName(-1) != nullptr);
    assert(actionName(SI468X_PI_ACTION_COUNT) == nullptr);
    int named = 0;
    assert(actionFromName("volume_down", named) && named == SI468X_PI_VOLUME_DOWN);
    assert(actionFromName("mute", named) && named == SI468X_PI_MUTE);
    assert(!actionFromName("volume", named));

    // a held button only repeats where a second step is what the user means:
    // the station list and the volume, not a screen, the mute or the radio
    assert(repeatableAction(SI468X_PI_UP));
    assert(repeatableAction(SI468X_PI_DOWN));
    assert(repeatableAction(SI468X_PI_VOLUME_UP));
    assert(repeatableAction(SI468X_PI_VOLUME_DOWN));
    assert(!repeatableAction(SI468X_PI_LEFT));
    assert(!repeatableAction(SI468X_PI_RIGHT));
    assert(!repeatableAction(SI468X_PI_ENTER));
    assert(!repeatableAction(SI468X_PI_MENU));
    assert(!repeatableAction(SI468X_PI_HOME));
    assert(!repeatableAction(SI468X_PI_MUTE));
    assert(!repeatableAction(SI468X_PI_QUIT));
    assert(!repeatableAction(-1));

    std::cout << "evdev_tests: OK" << std::endl;
    return 0;
}
