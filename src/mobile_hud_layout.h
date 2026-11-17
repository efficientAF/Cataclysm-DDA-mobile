#pragma once
#ifndef CATA_SRC_MOBILE_HUD_LAYOUT_H
#define CATA_SRC_MOBILE_HUD_LAYOUT_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

class JsonObject;
class JsonOut;

// Layouts of the mobile HUD: where each piece sits, in the format the UI canvas
// exports ("View layout JSON").  A preset holds a portrait and a landscape
// layout.  Built-in presets are loaded from data/json/ui/mobile_hud/ with the
// rest of the game data; the player's own layout is kept in the config folder.
//
// Positions are in canvas units (1/390 of the short side of the screen),
// measured from the edge of the canvas's phone, which has a notch: 50 units at
// the top in portrait and 48 on either side in landscape.  place() swaps those
// for the insets of the real screen, so a layout made on the canvas fits a
// phone with a different cutout or none.
namespace mobile_hud
{

enum class anchor : int {
    top_left,
    top_center,
    top_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

enum class piece_id : int {
    status,
    location,
    log,
    body,
    wield,
    quick,
    zoom,
    recenter,
    joy,
    pan,
    num
};

enum class drawer_id : int {
    info,
    log,
    actions,
    nearby,
    num
};

enum class screen_edge : int {
    left,
    right,
    top,
    bottom,
};

enum class pad_mode : int {
    // appears where a finger drags on the map (upstream's joystick)
    floating,
    // a button shows and hides it
    toggle,
    always,
};

constexpr size_t num_pieces = static_cast<size_t>( piece_id::num );
constexpr size_t num_drawers = static_cast<size_t>( drawer_id::num );

struct piece {
    bool visible = true;
    anchor pin = anchor::top_left;
    float x = 0.0f;
    float y = 0.0f;
    // location, log
    int rows = 2;
    float width = 236.0f;
    // status (icon size), body, joy, zoom and pan (diameter)
    float size = 34.0f;
    // status, quick: a column instead of a row
    bool column = false;
    // status (max shown), quick (buttons)
    int count = 6;
    pad_mode mode = pad_mode::toggle;
    // zoom: a slider instead of the round dial; column and width are its
    // direction and length then
    bool slider = false;
    float opacity = 1.0f;
    // quick: the buttons in the player's order, by the names of quick_item_ids();
    // empty for the usual order.  The first count - 1 show, then Menu.
    std::vector<std::string> items;
};

struct drawer {
    bool visible = true;
    screen_edge edge = screen_edge::right;
    // where the handle sits along the edge, 0 to 1
    float off = 0.5f;
    // how far the drawer reaches into the screen, 0 to 1
    float size = 0.8f;
};

struct layout {
    std::array<piece, num_pieces> pieces;
    std::array<drawer, num_drawers> drawers;

    piece &operator[]( const piece_id id ) {
        return pieces[static_cast<size_t>( id )];
    }
    const piece &operator[]( const piece_id id ) const {
        return pieces[static_cast<size_t>( id )];
    }
    drawer &operator[]( const drawer_id id ) {
        return drawers[static_cast<size_t>( id )];
    }
    const drawer &operator[]( const drawer_id id ) const {
        return drawers[static_cast<size_t>( id )];
    }
};

struct preset {
    std::string id;
    std::string name;
    layout portrait;
    layout landscape;
};

struct rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Space kept free at each edge, in the unit of the rectangle it goes with.
struct insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// The zoom and pan dials are round; their diameter stays within these, in units.
constexpr float dial_size_min = 44.0f;
constexpr float dial_size_max = 120.0f;
// The zoom slider is 44 units thick and this long.
constexpr float slider_length_min = 120.0f;
constexpr float slider_length_max = 320.0f;

// The names used in the layout JSON.
const char *json_name( piece_id id );
const char *json_name( drawer_id id );
// The name shown to the player.
std::string display_name( piece_id id );
std::string display_name( drawer_id id );

// The buttons the quick actions bar can hold, in the usual order.  These names
// are the ones in "items" of the layout JSON.  Menu is not among them: it is
// always the last button.
const std::vector<std::string> &quick_item_ids();
// The order for a bar: its own items first (names that are unknown or repeated
// dropped), then the rest in the usual order.
std::vector<std::string> quick_order( const piece &p );
constexpr int quick_count_min = 3;
constexpr int quick_count_max = 9;

// The insets of the canvas's phone, in canvas units.
insets canvas_insets( bool landscape );

// Where a piece of the given size goes.  screen is the whole screen and safe
// the insets of the real device, both in pixels; unit is pixels per canvas unit.
rect place( const piece &p, float w, float h, const rect &screen, const insets &safe,
            float unit, bool landscape );

// The reverse, for the editor: pins the piece to the nearest corner or edge so
// that place() returns where.
void pin_to( piece &p, const rect &where, const rect &screen, const insets &safe, float unit,
             bool landscape );

// Where a drawer's handle sits: 27 units thick and 66 long, on its edge.
rect handle_rect( const drawer &d, const rect &screen, const insets &safe, float unit );
// The edge nearest to a point and the offset along it, for dragging a handle.
void move_handle( drawer &d, float px, float py, const rect &screen, const insets &safe,
                  float unit );

// The Balanced preset of the canvas; used when no data is loaded.
const preset &fallback_preset();

// --- Game data: data/json/ui/mobile_hud/ ("type": "mobile_hud_preset") ---

void load_preset( const JsonObject &jo, const std::string &src );
void reset_presets();
// The built-in presets, in loading order; never empty.
const std::vector<preset> &builtin_presets();

// Reads one preset in the canvas's format.  Missing pieces keep the values of base.
preset read_preset( const JsonObject &jo, const preset &base );
void write_preset( JsonOut &jsout, const preset &p );

// --- The layout in use ---

// The player's layout: the saved one, or the first built-in preset.
const preset &active_preset();
const layout &active_layout( bool landscape );
// Replaces the layout in use and saves it to the config folder.
void set_active_preset( const preset &p );
// Forgets the cached layout, so that it is read again (after data is loaded).
void invalidate_active_preset();

} // namespace mobile_hud

#endif // CATA_SRC_MOBILE_HUD_LAYOUT_H
