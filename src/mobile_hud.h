#pragma once
#ifndef CATA_SRC_MOBILE_HUD_H
#define CATA_SRC_MOBILE_HUD_H

#if defined(TILES)

#include <array>
#include <optional>

#include "mobile_hud_layout.h"
#include "point.h"
#include "sdl_wrappers.h"

enum action_id : int;

class Font;

// Mobile HUD overlay of the mobile UI fork.  It is drawn over the finished
// frame at native window resolution, in the same place as the Android quick
// shortcuts and virtual joystick (see refresh_display in sdltiles.cpp).
//
// With the MOBILE_HUD option the sidebar is hidden, the map fills the screen and
// this overlay shows the pieces of the UI canvas instead: location and time,
// status icons, the body figure, the wielded item, the last messages, quick
// actions, zoom buttons, the movement pad and the handles of the two drawers.
// Where each piece sits comes from the layout in use (mobile_hud_layout.h).
// The Recenter button of the touch controls is drawn here too, with or without
// the option.
//
// Sizes follow the UI canvas: one unit is 1/390 of the short side of the view.
namespace mobile_hud
{

// view is where the game screen was just drawn in the window; the HUD stays
// inside it.  default_mode is true while the game is waiting for a command on
// the main map.
void draw( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &view, int window_w,
           int window_h, bool default_mode );

enum class hit_kind : int {
    // perform act
    action,
    recenter,
    // open the Log drawer on its Map tab
    map_drawer,
    // show or hide the movement pad
    pad_toggle,
    // the movement pad itself
    pad,
};

struct hit {
    hit_kind kind = hit_kind::action;
    action_id act;
};

// What is under a point in window pixels, as of the last draw.
std::optional<hit> hit_at( float x, float y );

// --- The movement pad ---

// How the pad shows; floating means upstream's drag-anywhere joystick is in use.
pad_mode movement_pad_mode();
void toggle_pad();
// A thumb on the pad at a point in window pixels: moves the knob and returns
// the direction to walk in, if it is past the dead zone.
std::optional<point> pad_direction( float x, float y );
void release_pad();

// --- For the editor (mobile_hud_edit.h) ---

// While editing, the HUD draws the working layout with a frame and a name tag
// around every piece, and nothing can be tapped.
void begin_edit( const preset &working );
// selection is a piece_id, or num_pieces plus a drawer_id, or -1.
void update_edit( const preset &working, int selection );
void end_edit();
bool is_editing();

struct edit_geometry {
    // where the HUD draws, in window pixels, and pixels per canvas unit
    rect screen;
    // room kept free at its edges, for the pieces and for the drawer handles
    insets safe;
    insets handle_safe;
    float unit = 1.0f;
    bool landscape = false;
    // where each piece and handle was drawn; empty if it was not
    std::array<rect, num_pieces> pieces;
    std::array<rect, num_drawers> handles;
};

edit_geometry edit_frame();

} // namespace mobile_hud

#endif // TILES

#endif // CATA_SRC_MOBILE_HUD_H
