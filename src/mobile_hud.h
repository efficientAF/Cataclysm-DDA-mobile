#pragma once
#ifndef CATA_SRC_MOBILE_HUD_H
#define CATA_SRC_MOBILE_HUD_H

#if defined(TILES)

#include <optional>

#include "sdl_wrappers.h"

enum action_id : int;

class Font;

// Mobile HUD overlay of the mobile UI fork.  It is drawn over the finished
// frame at native window resolution, in the same place as the Android quick
// shortcuts and virtual joystick (see refresh_display in sdltiles.cpp).
//
// With the MOBILE_HUD option the sidebar is hidden, the map fills the screen and
// this overlay shows the pieces of the UI canvas instead.  So far those are the
// location and time, a strip of status chips and the last few messages, placed
// as in the canvas's "Balanced" preset.  The Recenter button of the touch
// controls is drawn here too, with or without the option.
//
// Sizes follow the UI canvas: one unit is 1/390 of the short side of the view.
namespace mobile_hud
{


// view is where the game screen was just drawn in the window; the HUD stays
// inside it, so it does not cover the Android quick shortcuts.
// default_mode is true while the game is waiting for a command on the main map.
void draw( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &view, int window_w,
           int window_h, bool default_mode );

// The action of the HUD button under a point in window pixels, as of the last
// draw.  The Recenter button reports ACTION_CENTER.
std::optional<action_id> button_at( float x, float y );

} // namespace mobile_hud

#endif // TILES

#endif // CATA_SRC_MOBILE_HUD_H
