#pragma once
#ifndef CATA_SRC_MOBILE_SETTINGS_H
#define CATA_SRC_MOBILE_SETTINGS_H

// The Settings screen of the UI canvas: the touch settings people change most,
// as switches, segmented choices and a slider, with upstream's full options
// screen one tap away under "All options".
namespace mobile_settings
{

// Shows the screen and waits until it is closed.  Changes are written to the
// options as they are made and saved when leaving, the way upstream's options
// screen does.
void show();
// Settings, Screen sizes: each kind of screen smaller or larger (mobile_ui.h).
void show_screen_sizes();

} // namespace mobile_settings

#endif // CATA_SRC_MOBILE_SETTINGS_H
