#include "touch_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "action.h"
#include "avatar.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "mapdata.h"
#include "options.h"
#include "translations.h"
#include "uilist.h"
#include "ui_manager.h"
#include "uistate.h"

#if defined(TILES)
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <vector>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "cached_options.h"
#include "cata_tiles.h"
#include "cursesport.h"
#include "input.h"
#include "mobile_hud.h"
#include "sdltiles.h"
#endif

namespace touch_input
{

// The same limits game.cpp uses for uistate.tileset_zoom.
static constexpr int min_zoom_level = 4;
static constexpr int max_zoom_level = 64;
// How far the view may be dragged away from the player, in tiles.
static constexpr int max_pan_distance = 60;

// --- Pure helpers ---

tap_target classify_tap( const point &delta )
{
    const int dist = std::max( std::abs( delta.x ), std::abs( delta.y ) );
    if( dist == 0 ) {
        return tap_target::self;
    }
    return dist == 1 ? tap_target::adjacent : tap_target::distant;
}

int snap_zoom( const int start_zoom, const float pinch_ratio, const int min_zoom,
               const int max_zoom )
{
    if( start_zoom <= 0 || pinch_ratio <= 0.0f ) {
        return std::clamp( start_zoom, min_zoom, max_zoom );
    }
    const float wanted = std::log2( static_cast<float>( start_zoom ) * pinch_ratio );
    const float lo = std::log2( static_cast<float>( min_zoom ) );
    const float hi = std::log2( static_cast<float>( max_zoom ) );
    const int step = static_cast<int>( std::lround( std::clamp( wanted, lo, hi ) ) );
    return 1 << step;
}

int step_zoom( const int zoom, const bool zoom_in, const int min_zoom, const int max_zoom,
               const bool wrap )
{
    if( zoom_in ) {
        if( zoom >= max_zoom ) {
            return wrap ? min_zoom : max_zoom;
        }
        return zoom * 2;
    }
    if( zoom <= min_zoom ) {
        return wrap ? max_zoom : min_zoom;
    }
    return zoom / 2;
}

void pan_accumulator::reset()
{
    rem_x = 0.0f;
    rem_y = 0.0f;
}

point pan_accumulator::add( const float dx, const float dy, const float tile_w,
                            const float tile_h )
{
    if( tile_w <= 0.0f || tile_h <= 0.0f ) {
        return point::zero;
    }
    rem_x += dx / tile_w;
    rem_y += dy / tile_h;
    const point whole( static_cast<int>( std::trunc( rem_x ) ),
                       static_cast<int>( std::trunc( rem_y ) ) );
    rem_x -= static_cast<float>( whole.x );
    rem_y -= static_cast<float>( whole.y );
    return whole;
}

drag_axis classify_drag( const float dx, const float dy, const float slop )
{
    if( dx * dx + dy * dy <= slop * slop ) {
        return drag_axis::none;
    }
    return std::abs( dx ) > std::abs( dy ) ? drag_axis::horizontal : drag_axis::vertical;
}

// --- Options ---

static bool bool_option( const std::string &name )
{
#if defined(TILES)
    return get_option<bool>( name );
#else
    // the options only exist in tiles builds
    static_cast<void>( name );
    return false;
#endif
}

bool tap_to_act()
{
    return bool_option( "TOUCH_TAP_TO_ACT" );
}

bool long_press_menu()
{
    return bool_option( "TOUCH_LONG_PRESS_MENU" );
}

int long_press_delay_ms()
{
#if defined(TILES)
    return get_option<int>( "TOUCH_LONG_PRESS_DELAY" );
#else
    return 500;
#endif
}

bool live_pinch_zoom()
{
    return bool_option( "TOUCH_LIVE_PINCH_ZOOM" );
}

bool two_finger_pan()
{
    return bool_option( "TOUCH_TWO_FINGER_PAN" );
}

bool recenter_on_move()
{
    return bool_option( "TOUCH_RECENTER_ON_MOVE" );
}

bool zoom_no_wrap()
{
    return bool_option( "TOUCH_ZOOM_NO_WRAP" );
}

bool imgui_pointer()
{
    return bool_option( "TOUCH_IMGUI_POINTER" );
}

bool menu_tap()
{
    return bool_option( "TOUCH_MENU_TAP" );
}

bool owns_two_finger_gestures()
{
    return live_pinch_zoom() || two_finger_pan();
}

bool hud_enabled()
{
    return bool_option( "MOBILE_HUD" );
}

// --- Map clicks ---

static click_kind pending_click = click_kind::none;
static std::optional<point> pointer_override;

void queue_map_click( const click_kind kind, const point &pos )
{
    pending_click = kind;
    pointer_override = pos;
}

std::optional<point> take_pointer_override()
{
    const std::optional<point> ret = pointer_override;
    pointer_override.reset();
    return ret;
}

click_kind take_pending_click()
{
    const click_kind ret = pending_click;
    pending_click = click_kind::none;
    return ret;
}

// A tap on a tile next to the player: walk, attack, open, or use what is there.
static action_id adjacent_tap_action( const tripoint_bub_ms &target, const point &delta )
{
    map &here = get_map();
    const bool creature_there = get_creature_tracker().creature_at( target ) != nullptr;
    if( !creature_there && here.impassable( target ) && !here.veh_at( target ) &&
        ( here.ter( target )->can_examine( target ) || here.furn( target )->can_examine( target ) ) ) {
        // Something like a vending machine or a locker: walking into it does nothing.
        return ACTION_EXAMINE;
    }
    return get_movement_action_from_delta( tripoint_rel_ms( delta.x, delta.y, 0 ), iso_rotate::yes );
}

static std::string describe_tile( const tripoint_bub_ms &target )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    if( const Creature *const critter = get_creature_tracker().creature_at( target ) ) {
        if( you.sees( here, *critter ) ) {
            return critter->disp_name();
        }
    }
    if( here.has_furn( target ) ) {
        return here.furnname( target );
    }
    return here.tername( target );
}

static std::optional<action_id> long_press_action( const tripoint_bub_ms &target,
        const point &delta, bool &travel )
{
    enum : int {
        opt_travel,
        opt_move,
        opt_examine,
        opt_pickup,
        opt_close,
        opt_fire,
        opt_throw,
        opt_look,
        opt_wait,
    };

    map &here = get_map();
    avatar &you = get_avatar();
    const tap_target where = classify_tap( delta );
    const Creature *const critter = get_creature_tracker().creature_at( target );
    const bool sees_critter = critter != nullptr && you.sees( here, *critter );
    const item_location weapon = you.get_wielded_item();

    uilist menu;
    menu.text = describe_tile( target );
    if( where == tap_target::distant ) {
        menu.addentry( opt_travel, true, MENU_AUTOASSIGN, _( "Travel here" ) );
    }
    if( where == tap_target::adjacent ) {
        menu.addentry( opt_move, true, MENU_AUTOASSIGN, sees_critter ? _( "Attack" ) : _( "Move here" ) );
        menu.addentry( opt_examine, true, MENU_AUTOASSIGN, _( "Examine" ) );
        if( here.close_door( target, !here.is_outside( you.pos_bub() ), true ) ) {
            menu.addentry( opt_close, true, MENU_AUTOASSIGN, _( "Close" ) );
        }
    }
    if( where != tap_target::distant && here.sees_some_items( target, you ) ) {
        menu.addentry( opt_pickup, true, MENU_AUTOASSIGN, _( "Pick up" ) );
    }
    if( sees_critter && where != tap_target::self ) {
        if( weapon && weapon->is_gun() ) {
            menu.addentry( opt_fire, true, MENU_AUTOASSIGN, _( "Fire weapon" ) );
        }
        menu.addentry( opt_throw, true, MENU_AUTOASSIGN, _( "Throw something" ) );
    }
    if( where == tap_target::self ) {
        menu.addentry( opt_wait, true, MENU_AUTOASSIGN, _( "Wait" ) );
    }
    menu.addentry( opt_look, true, MENU_AUTOASSIGN, _( "Look here" ) );
    menu.query();

    switch( menu.ret ) {
        case opt_travel:
            travel = true;
            return std::nullopt;
        case opt_move:
            return get_movement_action_from_delta( tripoint_rel_ms( delta.x, delta.y, 0 ),
                                                   iso_rotate::yes );
        case opt_examine:
            return ACTION_EXAMINE;
        case opt_pickup:
            return ACTION_PICKUP;
        case opt_close:
            return ACTION_CLOSE;
        case opt_fire:
            return ACTION_FIRE;
        case opt_throw:
            return ACTION_THROW;
        case opt_wait:
            return ACTION_WAIT;
        case opt_look: {
            tripoint_bub_ms center = target;
            g->look_around( true, center, target, false, false, false );
            return ACTION_NULL;
        }
        default:
            break;
    }
    return ACTION_NULL;
}

std::optional<action_id> resolve_map_click( const click_kind kind, const tripoint_bub_ms &target,
        bool &travel )
{
    travel = false;
    if( kind == click_kind::none ) {
        return std::nullopt;
    }
    const point delta = target.xy().raw() - get_avatar().pos_bub().xy().raw();
    if( kind == click_kind::long_press ) {
        return long_press_action( target, delta, travel );
    }
    switch( classify_tap( delta ) ) {
        case tap_target::self:
            return ACTION_PAUSE;
        case tap_target::adjacent:
            return adjacent_tap_action( target, delta );
        case tap_target::distant:
            break;
    }
    return std::nullopt;
}

// --- View panning ---

// True while the view offset is one this module set.
static bool panned_by_touch = false;
static tripoint_abs_ms pan_player_pos;

bool pan_view( const point &tiles )
{
    if( !g || tiles == point::zero ) {
        return false;
    }
    avatar &you = get_avatar();
    const point before = you.view_offset.xy().raw();
    const point base = g->driving_view_offset.raw();
    const point wanted = before + tiles;
    const point after( std::clamp( wanted.x, base.x - max_pan_distance, base.x + max_pan_distance ),
                       std::clamp( wanted.y, base.y - max_pan_distance, base.y + max_pan_distance ) );
    if( after == before ) {
        return false;
    }
    you.view_offset.x() = after.x;
    you.view_offset.y() = after.y;
    panned_by_touch = true;
    pan_player_pos = you.pos_abs();
    g->invalidate_main_ui_adaptor();
    return true;
}

bool view_is_panned()
{
    if( !g || !panned_by_touch ) {
        return false;
    }
    if( get_avatar().view_offset.xy() == g->driving_view_offset ) {
        panned_by_touch = false;
    }
    return panned_by_touch;
}

void recenter_view()
{
    panned_by_touch = false;
    if( !g ) {
        return;
    }
    avatar &you = get_avatar();
    you.view_offset.x() = g->driving_view_offset.x();
    you.view_offset.y() = g->driving_view_offset.y();
    you.view_offset.z() = 0;
    g->invalidate_main_ui_adaptor();
}

#if defined(TILES)

// --- Coordinates ---

static point window_size()
{
    int w = 0;
    int h = 0;
    if( SDL_Window *const win = get_sdl_window() ) {
        SDL_GetWindowSize( win, &w, &h );
    }
    return point( w, h );
}

static point to_buffer( const float x, const float y )
{
    const SDL_Point p = window_to_display_buffer_coords( SDL_Point{ static_cast<int>( x ),
                        static_cast<int>( y ) } );
    return point( p.x, p.y );
}

static ImVec2 to_imgui( const float x, const float y )
{
    const point p = to_buffer( x, y );
    const float scale = static_cast<float>( std::max( get_scaling_factor(), 1 ) );
    return ImVec2( static_cast<float>( p.x ) / scale, static_cast<float>( p.y ) / scale );
}

// How far a finger may wander and still count as a tap, in window pixels.
static float touch_slop()
{
    const point size = window_size();
    return std::max( 12.0f, 0.02f * static_cast<float>( std::min( size.x, size.y ) ) );
}

// Size of one map tile in display buffer pixels.
static point terrain_tile_size()
{
    const int scale = get_scaling_factor();
    if( use_tiles && tilecontext && scale > 1 ) {
        return point( tilecontext->get_tile_width() * scale, tilecontext->get_tile_height() * scale );
    }
    return get_window_dimensions( g->w_terrain ).scaled_font_size;
}

// --- Two-finger gestures ---

namespace
{
// A two-finger gesture either zooms or pans, whichever the fingers do first.
enum class two_finger_kind : int {
    undecided,
    zoom,
    pan,
};

struct two_finger_state {
    bool active = false;
    bool acted = false;
    // finger positions changed since the last flush
    bool dirty = false;
    view_mode mode = view_mode::map;
    two_finger_kind kind = two_finger_kind::undecided;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float start_dist = 0.0f;
    float start_mid_x = 0.0f;
    float start_mid_y = 0.0f;
    int start_zoom = 0;
    point last_mid;
    pan_accumulator pan;
};
} // namespace

static two_finger_state two_finger;

static int current_zoom( const view_mode mode )
{
    return mode == view_mode::map ? g->get_zoom() : uistate.overmap_tileset_zoom;
}

two_finger_gesture classify_two_finger( const float distance_ratio, const float midpoint_travel,
                                        const float slop )
{
    // a fifth more or less distance between the fingers is a pinch
    if( distance_ratio > 1.2f || ( distance_ratio > 0.0f && distance_ratio < 1.0f / 1.2f ) ) {
        return two_finger_gesture::zoom;
    }
    if( midpoint_travel > slop ) {
        return two_finger_gesture::pan;
    }
    return two_finger_gesture::undecided;
}

void two_finger_begin( const view_mode mode, const float x0, const float y0, const float x1,
                       const float y1 )
{
    two_finger = two_finger_state();
    if( !g || !owns_two_finger_gestures() ) {
        return;
    }
    two_finger.active = true;
    two_finger.mode = mode;
    two_finger.x0 = x0;
    two_finger.y0 = y0;
    two_finger.x1 = x1;
    two_finger.y1 = y1;
    two_finger.start_dist = std::hypot( x1 - x0, y1 - y0 );
    two_finger.start_mid_x = ( x0 + x1 ) * 0.5f;
    two_finger.start_mid_y = ( y0 + y1 ) * 0.5f;
    two_finger.start_zoom = current_zoom( mode );
}

void two_finger_update( const float x0, const float y0, const float x1, const float y1 )
{
    if( !two_finger.active ) {
        return;
    }
    // Fingers report one at a time.  Only remember where they are; the gesture
    // is worked out in two_finger_flush once every pending event has been read,
    // so that a pan is not mistaken for a pinch halfway through.
    two_finger.x0 = x0;
    two_finger.y0 = y0;
    two_finger.x1 = x1;
    two_finger.y1 = y1;
    two_finger.dirty = true;
}

static bool two_finger_zoom( const float ratio )
{
    const int now = current_zoom( two_finger.mode );
    // Only leave the current level once the fingers are clearly past the
    // halfway point to the next one, so it does not flicker between two levels.
    const float wanted = std::log2( static_cast<float>( two_finger.start_zoom ) * ratio );
    if( std::abs( wanted - std::log2( static_cast<float>( now ) ) ) <= 0.6f ) {
        return false;
    }
    const int target = snap_zoom( two_finger.start_zoom, ratio, min_zoom_level, max_zoom_level );
    if( target == now ) {
        return false;
    }
    if( two_finger.mode == view_mode::map ) {
        g->set_zoom( target );
    } else {
        g->set_overmap_zoom( target );
    }
    return true;
}

bool two_finger_flush()
{
    if( !two_finger.active || !two_finger.dirty || !g ) {
        return false;
    }
    two_finger.dirty = false;
    const float mid_x = ( two_finger.x0 + two_finger.x1 ) * 0.5f;
    const float mid_y = ( two_finger.y0 + two_finger.y1 ) * 0.5f;
    const float ratio = two_finger.start_dist > 0.0f ?
                        std::hypot( two_finger.x1 - two_finger.x0, two_finger.y1 - two_finger.y0 ) /
                        two_finger.start_dist : 1.0f;

    if( two_finger.kind == two_finger_kind::undecided ) {
        const two_finger_gesture seen = classify_two_finger( ratio,
                                        std::hypot( mid_x - two_finger.start_mid_x, mid_y - two_finger.start_mid_y ),
                                        touch_slop() );
        const bool can_zoom = live_pinch_zoom() && two_finger.start_dist > touch_slop();
        const bool can_pan = two_finger_pan() && two_finger.mode == view_mode::map;
        if( seen == two_finger_gesture::zoom && can_zoom ) {
            two_finger.kind = two_finger_kind::zoom;
        } else if( seen == two_finger_gesture::pan && can_pan ) {
            two_finger.kind = two_finger_kind::pan;
            two_finger.last_mid = to_buffer( two_finger.start_mid_x, two_finger.start_mid_y );
            two_finger.pan.reset();
        } else {
            return false;
        }
        // from here on it is not a two-finger tap or swipe any more
        two_finger.acted = true;
    }

    bool changed = false;
    if( two_finger.kind == two_finger_kind::zoom ) {
        changed = two_finger_zoom( ratio );
    } else {
        const point mid = to_buffer( mid_x, mid_y );
        const point tile = terrain_tile_size();
        const point tiles = two_finger.pan.add( static_cast<float>( mid.x - two_finger.last_mid.x ),
                                                static_cast<float>( mid.y - two_finger.last_mid.y ),
                                                static_cast<float>( tile.x ), static_cast<float>( tile.y ) );
        two_finger.last_mid = mid;
        // dragging the map to the right moves the view to the left
        changed = pan_view( -tiles );
    }

    if( changed ) {
        if( two_finger.mode == view_mode::map ) {
            g->invalidate_main_ui_adaptor();
        } else {
            ui_manager::invalidate_all_ui_adaptors();
        }
        ui_manager::redraw_invalidated();
    }
    return changed;
}

void two_finger_end()
{
    two_finger.active = false;
}

bool two_finger_acted()
{
    return two_finger.acted;
}

// --- Fingers owned by this module ---

namespace
{
enum class owner : int {
    none,
    imgui,
    hud_button,
};

struct owned_finger_state {
    owner who = owner::none;
    float down_x = 0.0f;
    float down_y = 0.0f;
    float last_y = 0.0f;
    drag_axis drag = drag_axis::none;
    // for owner::hud_button
    action_id act = ACTION_NULL;
};
} // namespace

static owned_finger_state owned_finger;
static int wake_frames = 0;
// ImGui ignores the mouse while the game has it switched off or hidden.  A
// finger on an ImGui window lifts that for as long as its tap is being handled.
static bool restore_no_mouse = false;

// Steps of a tap on an ImGui window that are still to be sent.
enum class tap_step : int {
    none,
    press,
    release,
    leave,
};
static tap_step tap_stage = tap_step::none;

static void enable_imgui_pointer()
{
    ImGuiIO &io = ImGui::GetIO();
    if( io.ConfigFlags & ImGuiConfigFlags_NoMouse ) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        restore_no_mouse = true;
    }
}

bool pointer_active()
{
    return owned_finger.who == owner::imgui || wake_frames > 0;
}

static void request_wake_frames( const int frames )
{
    wake_frames = std::max( wake_frames, frames );
}

static bool imgui_window_at( const ImVec2 &pos )
{
    if( ImGui::GetCurrentContext() == nullptr ) {
        return false;
    }
    for( const ImGuiWindow *const window : GImGui->Windows ) {
        if( window->Active && !window->Hidden &&
            !( window->Flags & ImGuiWindowFlags_NoMouseInputs ) &&
            window->Rect().Contains( pos ) ) {
            return true;
        }
    }
    return false;
}

bool finger_down( const float x, const float y, const bool default_mode )
{
    owned_finger = owned_finger_state();
    owned_finger.down_x = x;
    owned_finger.down_y = y;
    owned_finger.last_y = y;
    if( tap_stage != tap_step::none && ImGui::GetCurrentContext() != nullptr ) {
        // a new touch while the last tap is still being fed in: let go of it
        ImGui::GetIO().AddMouseButtonEvent( ImGuiMouseButton_Left, false );
        tap_stage = tap_step::none;
    }

    if( default_mode ) {
        if( const std::optional<action_id> act = mobile_hud::button_at( x, y ) ) {
            owned_finger.who = owner::hud_button;
            owned_finger.act = *act;
            return true;
        }
    }
    if( imgui_pointer() ) {
        const ImVec2 pos = to_imgui( x, y );
        if( imgui_window_at( pos ) ) {
            owned_finger.who = owner::imgui;
            enable_imgui_pointer();
            ImGuiIO &io = ImGui::GetIO();
            io.AddMouseSourceEvent( ImGuiMouseSource_TouchScreen );
            io.AddMousePosEvent( pos.x, pos.y );
            request_wake_frames( 2 );
            return true;
        }
    }
    return false;
}

void finger_motion( const float x, const float y )
{
    if( owned_finger.who != owner::imgui ) {
        return;
    }
    if( owned_finger.drag == drag_axis::none ) {
        owned_finger.drag = classify_drag( x - owned_finger.down_x, y - owned_finger.down_y,
                                           touch_slop() );
        owned_finger.last_y = y;
    }
    if( owned_finger.drag == drag_axis::vertical ) {
        // Dragging scrolls the window under the finger, like a mouse wheel.  One
        // wheel notch scrolls about five lines of text.
        const float moved = to_imgui( x, y ).y - to_imgui( x, owned_finger.last_y ).y;
        const float notch = 5.0f * std::max( ImGui::GetFontSize(), 1.0f );
        ImGuiIO &io = ImGui::GetIO();
        io.AddMouseSourceEvent( ImGuiMouseSource_TouchScreen );
        io.AddMouseWheelEvent( 0.0f, moved / notch );
        owned_finger.last_y = y;
        request_wake_frames( 2 );
    }
}

bool finger_up( const float x, const float y, input_event &out )
{
    const owned_finger_state finger = owned_finger;
    owned_finger = owned_finger_state();
    int key = 0;

    switch( finger.who ) {
        case owner::hud_button: {
            // only if the finger is still on the button it went down on
            const std::optional<action_id> act = mobile_hud::button_at( x, y );
            if( !act || *act != finger.act ) {
                break;
            }
            if( *act == ACTION_CENTER ) {
                recenter_view();
                ui_manager::redraw_invalidated();
                break;
            }
            // send the key the action is bound to
            const std::vector<input_event> keys = keys_bound_to( *act, -1, false );
            if( !keys.empty() ) {
                out = keys.front();
                return true;
            }
            break;
        }
        case owner::imgui: {
            ImGuiIO &io = ImGui::GetIO();
            io.AddMouseSourceEvent( ImGuiMouseSource_TouchScreen );
            if( finger.drag == drag_axis::none ) {
                // A tap is a click where the finger went down.  ImGui has to see
                // the press, the release and the pointer leaving in separate
                // frames, so they are fed in one per frame; see take_wake_frame.
                tap_stage = tap_step::press;
                request_wake_frames( 6 );
                break;
            }
            if( finger.drag == drag_axis::horizontal ) {
                // A sideways swipe keeps its old meaning: left or right arrow key.
                key = x > finger.down_x ? KEY_RIGHT : KEY_LEFT;
            }
            // A finger does not hover; park the pointer off screen afterwards.
            io.AddMousePosEvent( -FLT_MAX, -FLT_MAX );
            request_wake_frames( 2 );
            break;
        }
        case owner::none:
            break;
    }
    if( key != 0 ) {
        out = input_event( key, input_event_t::keyboard_char );
        return true;
    }
    return false;
}

void finger_cancel()
{
    if( owned_finger.who == owner::imgui && ImGui::GetCurrentContext() != nullptr ) {
        ImGui::GetIO().AddMousePosEvent( -FLT_MAX, -FLT_MAX );
    }
    owned_finger = owned_finger_state();
    two_finger_end();
}

// --- Housekeeping ---

// Fingers are blunter than a mouse pointer: give ImGui rows, buttons and
// scrollbars more room while the pointer option is on.
static void apply_touch_style()
{
    static bool applied = false;
    if( applied || !imgui_pointer() || ImGui::GetCurrentContext() == nullptr ) {
        return;
    }
    const float font = ImGui::GetFontSize();
    if( font <= 0.0f ) {
        return;
    }
    ImGuiStyle &style = ImGui::GetStyle();
    style.TouchExtraPadding = ImVec2( font * 0.25f, font * 0.25f );
    style.CellPadding.y = std::max( style.CellPadding.y, font * 0.35f );
    style.ItemSpacing.y = std::max( style.ItemSpacing.y, font * 0.45f );
    style.FramePadding.y = std::max( style.FramePadding.y, font * 0.35f );
    style.ScrollbarSize = std::max( style.ScrollbarSize, font * 1.2f );
    applied = true;
}

bool poll()
{
    apply_touch_style();
    if( !g || !panned_by_touch || !recenter_on_move() ) {
        return false;
    }
    if( get_avatar().pos_abs() == pan_player_pos ) {
        return false;
    }
    recenter_view();
    return true;
}

bool take_wake_frame()
{
    if( wake_frames <= 0 ) {
        if( restore_no_mouse && owned_finger.who != owner::imgui &&
            ImGui::GetCurrentContext() != nullptr ) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            restore_no_mouse = false;
        }
        return false;
    }
    --wake_frames;
    if( tap_stage != tap_step::none && ImGui::GetCurrentContext() != nullptr ) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMouseSourceEvent( ImGuiMouseSource_TouchScreen );
        switch( tap_stage ) {
            case tap_step::press:
                io.AddMouseButtonEvent( ImGuiMouseButton_Left, true );
                tap_stage = tap_step::release;
                break;
            case tap_step::release:
                io.AddMouseButtonEvent( ImGuiMouseButton_Left, false );
                tap_stage = tap_step::leave;
                break;
            case tap_step::leave:
                io.AddMousePosEvent( -FLT_MAX, -FLT_MAX );
                tap_stage = tap_step::none;
                break;
            case tap_step::none:
                break;
        }
    }
    return true;
}

#if !defined(__ANDROID__)

// --- Desktop stand-in for the Android gesture code ---

namespace
{
struct desktop_state {
    bool down[2] = { false, false };
    SDL_FingerID ids[2] = { 0, 0 };
    float x[2] = { 0.0f, 0.0f };
    float y[2] = { 0.0f, 0.0f };
    float down_x = 0.0f;
    float down_y = 0.0f;
    uint64_t down_time = 0;
    // taken by finger_down()
    bool owned = false;
    // left the slop radius
    bool moved = false;
    bool two_fingers = false;
    // already delivered a long press
    bool done = false;
};

struct scripted_event {
    uint64_t at = 0;
    Uint32 type = 0;
    int finger = 0;
    float x = 0.0f;
    float y = 0.0f;
};
} // namespace

static desktop_state desk;
static std::vector<scripted_event> touch_script;

static const char *touch_script_path()
{
    static const char *const path = std::getenv( "CATA_TOUCH_SCRIPT" );
    return path;
}

static void load_touch_script()
{
    const char *const path = touch_script_path();
    if( path == nullptr || !touch_script.empty() ) {
        return;
    }
    std::ifstream in( path );
    if( !in ) {
        return;
    }
    const uint64_t start = SDL_GetTicks();
    uint64_t at = 0;
    std::string kind;
    scripted_event evt;
    while( in >> at >> kind >> evt.finger >> evt.x >> evt.y ) {
        evt.at = start + at;
        if( kind == "down" ) {
            evt.type = SDL_EVENT_FINGER_DOWN;
        } else if( kind == "move" ) {
            evt.type = SDL_EVENT_FINGER_MOTION;
        } else {
            evt.type = SDL_EVENT_FINGER_UP;
        }
        touch_script.push_back( evt );
    }
    in.close();
    std::remove( path );
}

static void play_touch_script()
{
    const uint64_t now = SDL_GetTicks();
    while( !touch_script.empty() && touch_script.front().at <= now ) {
        const scripted_event &evt = touch_script.front();
        SDL_Event ev;
        SDL_zero( ev );
        ev.type = evt.type;
        ev.tfinger.touchID = 1;
        ev.tfinger.fingerID = static_cast<SDL_FingerID>( evt.finger + 1 );
        ev.tfinger.x = evt.x;
        ev.tfinger.y = evt.y;
        SDL_PushEvent( &ev );
        touch_script.erase( touch_script.begin() );
    }
}

static void desktop_click( const click_kind kind, input_event &out )
{
    queue_map_click( kind, to_buffer( desk.x[0], desk.y[0] ) );
    out = input_event( MouseInput::LeftButtonReleased, input_event_t::mouse );
}

static int desktop_slot( const SDL_FingerID id )
{
    for( int i = 0; i < 2; ++i ) {
        if( desk.down[i] && desk.ids[i] == id ) {
            return i;
        }
    }
    return -1;
}

bool desktop_finger_event( const SDL_Event &ev, const int window_w, const int window_h,
                           const bool default_mode, const bool menu_select, input_event &out )
{
    const float fx = ev.tfinger.x * static_cast<float>( window_w );
    const float fy = ev.tfinger.y * static_cast<float>( window_h );
    if( ev.type == SDL_EVENT_FINGER_CANCELED ) {
        finger_cancel();
        desk = desktop_state();
        return true;
    }
    if( ev.type == SDL_EVENT_FINGER_DOWN ) {
        const int slot = !desk.down[0] ? 0 : ( !desk.down[1] ? 1 : -1 );
        if( slot < 0 ) {
            return false;
        }
        if( slot == 0 ) {
            desk = desktop_state();
            desk.down_x = fx;
            desk.down_y = fy;
            desk.down_time = SDL_GetTicks();
            desk.owned = finger_down( fx, fy, default_mode );
        }
        desk.down[slot] = true;
        desk.ids[slot] = ev.tfinger.fingerID;
        desk.x[slot] = fx;
        desk.y[slot] = fy;
        if( slot == 1 && !desk.owned && default_mode ) {
            desk.two_fingers = true;
            two_finger_begin( view_mode::map, desk.x[0], desk.y[0], desk.x[1], desk.y[1] );
        }
        return true;
    }

    const int slot = desktop_slot( ev.tfinger.fingerID );
    if( slot < 0 ) {
        return false;
    }
    desk.x[slot] = fx;
    desk.y[slot] = fy;

    if( ev.type == SDL_EVENT_FINGER_MOTION ) {
        if( desk.owned ) {
            if( slot == 0 ) {
                finger_motion( fx, fy );
            }
            return true;
        }
        if( desk.two_fingers ) {
            two_finger_update( desk.x[0], desk.y[0], desk.x[1], desk.y[1] );
            return false;
        }
        if( classify_drag( fx - desk.down_x, fy - desk.down_y, touch_slop() ) != drag_axis::none ) {
            desk.moved = true;
        }
        return false;
    }

    // finger up
    if( slot == 1 ) {
        desk.down[1] = false;
        return false;
    }
    if( desk.owned ) {
        finger_up( fx, fy, out );
    } else if( desk.two_fingers ) {
        two_finger_end();
    } else if( !desk.moved && !desk.done && default_mode && tap_to_act() ) {
        desktop_click( click_kind::tap, out );
    } else if( !desk.moved && !default_mode && menu_select && menu_tap() ) {
        // a tap in a text menu that takes mouse selection
        desktop_click( click_kind::none, out );
    }
    desk = desktop_state();
    return true;
}

void desktop_poll( const bool default_mode, input_event &out )
{
    load_touch_script();
    play_touch_script();
    if( desk.down[0] && !desk.owned && !desk.two_fingers && !desk.moved && !desk.done &&
        default_mode && long_press_menu() &&
        SDL_GetTicks() - desk.down_time >= static_cast<uint64_t>( long_press_delay_ms() ) ) {
        desk.done = true;
        desktop_click( click_kind::long_press, out );
    }
}

std::optional<uint32_t> desktop_wait_ms()
{
    if( touch_script_path() != nullptr || desk.down[0] ) {
        return 20;
    }
    return std::nullopt;
}

#endif // !__ANDROID__

#endif // TILES

} // namespace touch_input
