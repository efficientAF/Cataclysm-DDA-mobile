#pragma once
#ifndef CATA_SRC_TOUCH_INPUT_H
#define CATA_SRC_TOUCH_INPUT_H

#include <cstdint>
#include <optional>

#include "coords_fwd.h"
#include "point.h"

enum action_id : int;
struct input_event;
union SDL_Event;

// Touch controls of the mobile UI fork: taps that act on the map tile under the
// finger, a long-press action menu, live pinch zoom, two-finger panning and
// pointer input for ImGui windows.
//
// The gestures themselves are still recognised in sdltiles.cpp, which calls in
// here.  Everything is behind options (see add_options_android in options.cpp)
// so that it also runs in desktop tiles builds.
namespace touch_input
{

// --- Pure helpers, no game or SDL state (covered by tests/touch_input_test.cpp) ---

enum class tap_target : int {
    self,
    adjacent,
    distant,
};

// Where a tapped tile lies relative to the player; delta is in map tiles.
tap_target classify_tap( const point &delta );

// Zoom levels are powers of two from min_zoom to max_zoom.  Returns the level
// nearest to start_zoom scaled by pinch_ratio (finger distance now / at start).
int snap_zoom( int start_zoom, float pinch_ratio, int min_zoom, int max_zoom );

// The next zoom level in or out.  At either end it wraps around to the other end
// if wrap is true, as upstream does, and stays put otherwise.
int step_zoom( int zoom, bool zoom_in, int min_zoom, int max_zoom, bool wrap );

// Turns a drag in pixels into whole map tiles, carrying the remainder over.
class pan_accumulator
{
    public:
        void reset();
        // Returns the whole tiles covered since the last call.
        point add( float dx, float dy, float tile_w, float tile_h );
    private:
        float rem_x = 0.0f;
        float rem_y = 0.0f;
};

enum class drag_axis : int {
    none,
    horizontal,
    vertical,
};

// Which way a one-finger drag is heading once it has left the slop radius.
drag_axis classify_drag( float dx, float dy, float slop );

enum class two_finger_gesture : int {
    undecided,
    zoom,
    pan,
};

// What two fingers are doing.  distance_ratio is the distance between them now
// over the distance when they went down; midpoint_travel is how far the point
// between them has moved, in the same unit as slop.
two_finger_gesture classify_two_finger( float distance_ratio, float midpoint_travel, float slop );

// --- Options ---

bool tap_to_act();
bool long_press_menu();
// How long a finger has to rest on the map before the menu opens.
int long_press_delay_ms();
bool live_pinch_zoom();
bool two_finger_pan();
bool recenter_on_move();
bool zoom_no_wrap();
bool imgui_pointer();
bool menu_tap();
// True if any option that takes over two-finger gestures on the map is on.
bool owns_two_finger_gestures();
// The MOBILE_HUD option: no sidebar, the map fills the screen (see mobile_hud.h).
bool hud_enabled();
// The closest zoom level of the map: upstream's 64, or 256 with the mobile HUD,
// where small tiles on a dense phone screen need more.
int max_zoom();
// The zoom level a game starts at: upstream's 16, or with the mobile HUD the
// level that makes a tile about a sixteenth of the screen's short side.
int default_zoom();
// True if upstream's Android shortcut bar should stay out of the way: on the map
// and over the mobile screens.  It still shows on upstream's own text screens.
bool hide_shortcut_bar();

// --- Map clicks ---

enum class click_kind : int {
    none,
    tap,
    long_press,
};

// Remember that the next mouse-select input came from a finger at pos (display
// buffer pixels), so the game reads that position instead of the mouse cursor.
void queue_map_click( click_kind kind, const point &pos );
// Position of the queued click.  Cleared by the call.
std::optional<point> take_pointer_override();
// Kind of the queued click.  Cleared by the call.
click_kind take_pending_click();

// Decide what a touch on a visible map tile does.
// - std::nullopt: treat it like a left mouse click (route preview, then travel).
// - ACTION_NULL: handled here, nothing left to do.
// - anything else: perform that action; the tile stays the action's target.
// travel is set when the route should be followed at once, without a preview.
std::optional<action_id> resolve_map_click( click_kind kind, const tripoint_bub_ms &target,
        bool &travel );

// --- View panning ---

// Shift the view by whole tiles.  Returns true if it moved.
bool pan_view( const point &tiles );
bool view_is_panned();
void recenter_view();

#if defined(TILES)

// --- Two-finger gestures: pinch zoom and pan (window pixel coordinates) ---

enum class view_mode : int {
    map,
    overmap,
};

void two_finger_begin( view_mode mode, float x0, float y0, float x1, float y1 );
// Report where the two fingers are now.
void two_finger_update( float x0, float y0, float x1, float y1 );
// Act on the reported positions.  Call once all pending events have been read.
// Returns true if the view changed.
bool two_finger_flush();
void two_finger_end();
// True once the current or last two-finger gesture panned or zoomed, so that it
// must not also count as a two-finger tap or swipe.
bool two_finger_acted();

// --- Fingers owned by this module: ImGui windows and overlay buttons ---

// Call when the first finger goes down.  Returns true if this module takes the
// finger; the caller must then route its motion and release here and nowhere else.
bool finger_down( float x, float y, bool default_mode );
void finger_motion( float x, float y );
// A second finger went down while this module has the first.  Returns true if
// the two now pinch a raw drag area; report the second finger's moves with
// second_finger_motion then.
bool second_finger_down( float x, float y );
void second_finger_motion( float x, float y );
// Returns true if it set out to an input to deliver.
bool finger_up( float x, float y, input_event &out );
void finger_cancel();
// --- Raw drag areas ---
//
// A screen that pans its own content (the overmap) registers the area, in ImGui
// coordinates, every frame it wants it.  A finger that goes down inside it on
// an ImGui window is not turned into wheel scrolling or arrow keys: once it
// leaves the slop radius ImGui gets a held mouse drag from where it went down,
// released when it lifts.  A tap stays a click.  A second finger starts a
// two-finger gesture on pinch_mode, which zooms live.
void set_raw_drag_rect( float x, float y, float w, float h, view_mode pinch_mode );
void clear_raw_drag_rect();

// True while a finger is on an ImGui window or its tap is still being handled.
// ImGui must accept pointer input for that long, even with the mouse disabled.
bool pointer_active();

// --- Housekeeping, once per pass of the event loop ---

// Recenters the view after the player has moved.  Returns true if a redraw is needed.
bool poll();
// True while the UI loop should be woken up to draw another frame, so that
// ImGui can work through a queued tap.  Each call uses up one frame.
bool take_wake_frame();

#if !defined(__ANDROID__)

// --- Desktop tiles builds, for testing without a phone ---
//
// The Android gesture code in sdltiles.cpp is compiled out on desktop, so this is
// a small stand-in that turns SDL finger events into the same calls: tap, long
// press, two-finger pinch and pan, and fingers on ImGui windows.  There is no
// virtual joystick here: a one-finger drag does nothing.
//
// Gestures can be scripted.  Point the CATA_TOUCH_SCRIPT environment variable at
// a file path; whenever that file appears it is read, deleted and played back.
// Each line is "<milliseconds> <down|move|up> <finger> <x> <y>", with x and y as
// fractions of the window size.

// Returns true if the event needs a redraw.  May set out to an input to deliver.
// menu_select is true if the current input context takes mouse selection.
bool desktop_finger_event( const SDL_Event &ev, int window_w, int window_h, bool default_mode,
                           bool menu_select, input_event &out );
// Long-press timer and script playback.  May set out to an input to deliver.
void desktop_poll( bool default_mode, input_event &out );
// How long the event loop may sleep before desktop_poll has work, if at all.
std::optional<uint32_t> desktop_wait_ms();

#endif // !__ANDROID__

#endif // TILES

} // namespace touch_input

#endif // CATA_SRC_TOUCH_INPUT_H
