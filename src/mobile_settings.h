#pragma once
#ifndef CATA_SRC_MOBILE_SETTINGS_H
#define CATA_SRC_MOBILE_SETTINGS_H

// The Settings screen of the settings canvas: every option of the game on one
// screen with tabs (Input, Display, Sound, Gameplay, Interface, World,
// Advanced).  The screen itself is mobile_options, which stands in for the
// loop of upstream's options screen; this file opens it and holds the choosers
// and side screens its rows lead to.
namespace mobile_settings
{

// Shows the screen and waits until it is closed.  Upstream's options screen
// compares, saves and applies what was changed.
void show();
// Settings, Screen sizes: each kind of screen smaller or larger (mobile_ui.h).
void show_screen_sizes();
// "Edit HUD" was tapped on the settings screen: the layout editor opens once
// the screen has closed and saved.
void request_edit_hud();

} // namespace mobile_settings

#endif // CATA_SRC_MOBILE_SETTINGS_H
