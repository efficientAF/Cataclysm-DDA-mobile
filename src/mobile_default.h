#pragma once
#ifndef CATA_SRC_MOBILE_DEFAULT_H
#define CATA_SRC_MOBILE_DEFAULT_H

// Whether the mobile UI's options (MOBILE_HUD, MOBILE_MENUS and the TOUCH_*
// switches) start out on.  They always do on Android.  Elsewhere they do on a
// handheld with a touchscreen and a controller, where the desktop UI is of
// little use:
// - when the game is started with --mobile-ui,
// - when the environment variable CATA_MOBILE_UI is 1 (0 keeps them off),
// - on a Steam Deck (Steam sets SteamDeck=1 for the games it starts),
// - in a build made with -DCATA_MOBILE_UI_DEFAULT.
// This only sets the defaults: values already saved in options.json stay as
// they are, and every option can still be changed in the game.
namespace mobile_default
{

// Asks for the defaults to be on (the command line switch).  Call before the
// options are set up.
void request();

bool on();

} // namespace mobile_default

#endif // CATA_SRC_MOBILE_DEFAULT_H
