#include "mobile_controls.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>

#include "action.h"
#include "input.h"
#include "input_context.h"
#include "localized_comparator.h"
#include "mobile_entry.h"
#include "mobile_list.h"
#include "mobile_pad.h"
#include "mobile_prompt.h"
#include "output.h"
#include "popup.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

#if defined(TILES)
#include <cmath>
#include <set>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "mobile_ui.h"
#endif

namespace mobile_controls
{

// Turns upstream's short key names into words: "ALT_X" is "L2 + X", the left
// trigger being upstream's modifier.
std::string button_name( const int code )
{
    std::string key = inp_mngr.get_keyname( code, input_event_t::gamepad, true );
    std::string prefix;
    if( key.rfind( "ALT_", 0 ) == 0 ) {
        prefix = "L2 + ";
        key = key.substr( 4 );
    }
    static const std::map<std::string, std::string> names = {
        { "LB", "L1" }, { "RB", "R1" }, { "LT", "L2" }, { "RT", "R2" },
        { "LS", translate_marker( "Left stick click" ) }, { "RS", translate_marker( "Right stick click" ) },
        { "UP", translate_marker( "D-pad up" ) }, { "DOWN", translate_marker( "D-pad down" ) },
        { "LEFT", translate_marker( "D-pad left" ) }, { "RIGHT", translate_marker( "D-pad right" ) },
        { "START", translate_marker( "Start" ) }, { "BACK", translate_marker( "Select" ) },
    };
    const auto found = names.find( key );
    if( found != names.end() ) {
        return prefix + _( found->second );
    }
    // sticks and their rings: LS_UP_RIGHT, R_RADIAL_NE
    static const std::map<std::string, std::string> stems = {
        { "LS_", translate_marker( "Left stick" ) }, { "RS_", translate_marker( "Right stick" ) },
        { "L_RADIAL_", translate_marker( "Left stick ring" ) },
        { "R_RADIAL_", translate_marker( "Right stick ring" ) },
    };
    for( const std::pair<const std::string, std::string> &stem : stems ) {
        if( key.rfind( stem.first, 0 ) == 0 ) {
            std::string way = to_lower_case( key.substr( stem.first.size() ) );
            std::replace( way.begin(), way.end(), '_', '-' );
            // the rings name their eight directions by compass point
            static const std::map<std::string, std::string> compass = {
                { "n", translate_marker( "up" ) }, { "ne", translate_marker( "up-right" ) },
                { "e", translate_marker( "right" ) }, { "se", translate_marker( "down-right" ) },
                { "s", translate_marker( "down" ) }, { "sw", translate_marker( "down-left" ) },
                { "w", translate_marker( "left" ) }, { "nw", translate_marker( "up-left" ) },
                { "up", translate_marker( "up" ) }, { "down", translate_marker( "down" ) },
                { "left", translate_marker( "left" ) }, { "right", translate_marker( "right" ) },
                { "up-right", translate_marker( "up-right" ) }, { "down-right", translate_marker( "down-right" ) },
                { "up-left", translate_marker( "up-left" ) }, { "down-left", translate_marker( "down-left" ) },
            };
            const auto named = compass.find( way );
            return prefix + string_format( "%s %s", _( stem.second ),
                                           named != compass.end() ? _( named->second ) : way );
        }
    }
    return prefix + key;
}

// "ADVANCED_INVENTORY" as "Advanced inventory".
static std::string context_name( const std::string &category )
{
    if( category == "DEFAULTMODE" ) {
        return _( "Play screen" );
    }
    if( category == "default" ) {
        return _( "Everywhere (menus and lists)" );
    }
    std::string name = to_lower_case( category );
    std::replace( name.begin(), name.end(), '_', ' ' );
    if( !name.empty() ) {
        name[0] = static_cast<char>( std::toupper( static_cast<unsigned char>( name[0] ) ) );
    }
    return name;
}

#if defined(TILES)

// --- The controller page, the action picker, the menu editor and the ring ---
//
// Drawn from the controller canvas, whose boards are 1280 by 800: positions
// and sizes below are that canvas's pixels, scaled to fit the screen.

using mobile_ui::face;

namespace
{

constexpr ImU32 col_part = IM_COL32( 42, 49, 39, 255 );
constexpr ImU32 col_part_top = IM_COL32( 58, 67, 53, 255 );
constexpr ImU32 col_body = IM_COL32( 23, 28, 21, 255 );
constexpr ImU32 col_well = IM_COL32( 9, 11, 8, 255 );
constexpr ImU32 col_chip = IM_COL32( 22, 27, 20, 255 );
constexpr ImU32 col_line_strong = IM_COL32( 232, 236, 222, 90 );
constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );

// A board of the canvas on the screen.
struct board {
    ImVec2 origin;
    // screen pixels per canvas pixel
    float c = 1.0f;
    // screen pixels per unit of mobile_ui
    float u = 1.0f;
    ImDrawList *draw = nullptr;
    // false while a dropdown is open: the controls under it do not react
    bool live = true;

    ImVec2 at( const float x, const float y ) const {
        return ImVec2( origin.x + x * c, origin.y + y * c );
    }
    // a text size in canvas pixels as mobile_ui wants it.  The canvas gives the
    // em size, the fonts here are sized by their line: a fifth more.
    float ts( const float px ) const {
        return px * 1.18f * c / u;
    }
    ImVec2 measure( const face f, const float px, const std::string &s,
                    const float wrap = 0.0f ) const {
        const ImVec2 size = mobile_ui::text_size( f, ts( px ), s, wrap * c );
        return ImVec2( size.x / c, size.y / c );
    }
    void text( const face f, const float px, const float x, const float y, const ImU32 col,
               const std::string &s, const float wrap = 0.0f ) const {
        mobile_ui::text( draw, f, ts( px ), at( x, y ), col, s, wrap * c );
    }
    // the text cut to a width, with an ellipsis
    std::string fit( const face f, const float px, const std::string &s, const float w ) const {
        if( measure( f, px, s ).x <= w ) {
            return s;
        }
        std::string cut = s;
        while( !cut.empty() ) {
            // not into the middle of a UTF-8 character
            do {
                cut.pop_back();
            } while( !cut.empty() && ( static_cast<unsigned char>( cut.back() ) & 0xC0 ) == 0x80 );
            if( measure( f, px, cut + "…" ).x <= w ) {
                return cut + "…";
            }
        }
        return cut;
    }
    void text_centered( const face f, const float px, const float x, const float y, const float w,
                        const float h, const ImU32 col, const std::string &s ) const {
        const std::string shown = fit( f, px, s, w );
        const ImVec2 size = measure( f, px, shown );
        text( f, px, x + ( w - size.x ) * 0.5f, y + ( h - size.y ) * 0.5f, col, shown );
    }
    // the same on up to two lines, broken at a space
    void text_centered_2( const face f, const float px, const float x, const float y, const float w,
                          const float h, const ImU32 col, const std::string &s ) const {
        size_t cut = std::string::npos;
        if( measure( f, px, s ).x > w ) {
            for( size_t at = s.find( ' ' ); at != std::string::npos; at = s.find( ' ', at + 1 ) ) {
                if( measure( f, px, s.substr( 0, at ) ).x <= w ) {
                    cut = at;
                }
            }
        }
        if( cut == std::string::npos ) {
            text_centered( f, px, x, y, w, h, col, s );
            return;
        }
        const float line = measure( f, px, "A" ).y;
        text_centered( f, px, x, y + h * 0.5f - line, w, line, col, s.substr( 0, cut ) );
        text_centered( f, px, x, y + h * 0.5f, w, line, col, s.substr( cut + 1 ) );
    }
    void box( const float x, const float y, const float w, const float h, const ImU32 fill,
              const ImU32 border, const float radius ) const {
        mobile_ui::box( draw, at( x, y ), at( x + w, y + h ), fill, border, radius * c / u );
    }
    bool press( const std::string &id, const float x, const float y, const float w, const float h,
                bool *const held = nullptr ) const {
        if( !live ) {
            if( held != nullptr ) {
                *held = false;
            }
            return false;
        }
        return mobile_ui::pressable( id, at( x, y ), ImVec2( w * c, h * c ), held );
    }
};

enum class button_look : int {
    primary,
    plain,
    quiet
};

bool text_button( const board &b, const std::string &id, const float x, const float y,
                  const float w, const float h, const std::string &label,
                  const button_look look = button_look::plain, const bool enabled = true )
{
    bool held = false;
    const bool tapped = enabled && b.press( id, x, y, w, h, &held );
    const bool primary = look == button_look::primary;
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       held ? mobile_ui::col_button_down :
                       look == button_look::quiet ? 0 : mobile_ui::col_fill_soft;
    b.box( x, y, w, h, fill, primary ? 0 : mobile_ui::col_line, 12.0f );
    b.text_centered( face::bold, 15.0f, x + 8.0f, y, w - 16.0f, h,
                     !enabled ? mobile_ui::col_muted : primary ? mobile_ui::col_on_accent :
                     mobile_ui::col_fg, label );
    return tapped;
}

// The whole screen as a board: the canvas scaled to fit and centred.
board fit_board( const float canvas_w, const float canvas_h )
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec4 safe = mobile_ui::safe_insets();
    const float w = viewport->Size.x - safe.x - safe.z;
    const float h = viewport->Size.y - safe.y - safe.w;
    board b;
    b.u = mobile_ui::unit();
    b.c = std::min( w / canvas_w, h / canvas_h );
    b.origin = ImVec2( viewport->Pos.x + safe.x + ( w - canvas_w * b.c ) * 0.5f,
                       viewport->Pos.y + safe.y );
    b.draw = ImGui::GetWindowDrawList();
    b.draw->PushClipRectFullScreen();
    b.draw->AddRectFilled( viewport->Pos, ImVec2( viewport->Pos.x + viewport->Size.x,
                           viewport->Pos.y + viewport->Size.y ), mobile_ui::col_screen );
    b.draw->PopClipRect();
    return b;
}

// The bar at the top of a board.  Returns true if back was tapped.
bool top_bar( const board &b, const float width, const std::string &title, float *const title_end )
{
    bool held = false;
    const bool back = b.press( "##pad_back", 10.0f, 10.0f, 44.0f, 44.0f, &held );
    if( held ) {
        b.box( 10.0f, 10.0f, 44.0f, 44.0f, mobile_ui::col_fill_soft, 0, 12.0f );
    }
    mobile_ui::icon( b.draw, "back", b.at( 19.0f, 19.0f ), 26.0f * b.c / b.u, mobile_ui::col_fg );
    const ImVec2 size = b.measure( face::display, 28.0f, title );
    b.text( face::display, 28.0f, 64.0f, ( 64.0f - size.y ) * 0.5f, mobile_ui::col_fg, title );
    b.draw->AddLine( b.at( 0.0f, 64.0f ), b.at( width, 64.0f ), mobile_ui::col_line,
                     std::max( 1.0f, b.c ) );
    if( title_end != nullptr ) {
        *title_end = 64.0f + size.x;
    }
    return back;
}

constexpr ImGuiWindowFlags page_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoBackground;

// --- Names ---

std::string action_name( const std::string &id )
{
    if( id.empty() ) {
        return _( "Nothing" );
    }
    if( id == mobile_pad::action_step ) {
        return _( "Step / wait a turn" );
    }
    // as the keybindings name them: the play screen's own, then the shared ones
    static std::map<std::string, std::string> names;
    if( names.empty() ) {
        const std::vector<binding> all = bindings();
        for( const char *const category : {
                 "DEFAULTMODE", "default"
             } ) {
            for( const binding &b : all ) {
                if( b.category == category && !b.name.empty() ) {
                    names.emplace( b.action, b.name );
                }
            }
        }
    }
    const auto found = names.find( id );
    return found != names.end() ? found->second : id;
}

// One control of the controller: a button of the layout, or a stick.
struct control {
    // button::count for a stick
    mobile_pad::button b;
    // 0 left, 1 right, -1 not a stick
    int stick;
    // what its badge shows; empty for a drawn glyph
    const char *glyph;
    const char *name;
    // its label on the page
    float x;
    float y;
    float w;
};

const std::array<control, 18> controls = { {
        { mobile_pad::button::l2, -1, "L2", translate_marker( "Left trigger" ), 20, 60, 220 },
        { mobile_pad::button::l1, -1, "L1", translate_marker( "Left bumper" ), 20, 124, 220 },
        { mobile_pad::button::count, 0, "L", translate_marker( "Left stick" ), 20, 276, 220 },
        { mobile_pad::button::l3, -1, "L3", translate_marker( "Left stick click" ), 20, 340, 220 },
        { mobile_pad::button::up, -1, "", translate_marker( "D-pad up" ), 20, 432, 220 },
        { mobile_pad::button::left, -1, "", translate_marker( "D-pad left" ), 20, 496, 220 },
        { mobile_pad::button::right, -1, "", translate_marker( "D-pad right" ), 20, 560, 220 },
        { mobile_pad::button::down, -1, "", translate_marker( "D-pad down" ), 20, 624, 220 },
        { mobile_pad::button::select, -1, "", translate_marker( "Select" ), 250, 124, 165 },
        { mobile_pad::button::start, -1, "", translate_marker( "Start" ), 425, 124, 165 },
        { mobile_pad::button::r2, -1, "R2", translate_marker( "Right trigger" ), 620, 60, 220 },
        { mobile_pad::button::r1, -1, "R1", translate_marker( "Right bumper" ), 620, 124, 220 },
        { mobile_pad::button::y, -1, "Y", translate_marker( "Y button" ), 620, 216, 220 },
        { mobile_pad::button::x, -1, "X", translate_marker( "X button" ), 620, 280, 220 },
        { mobile_pad::button::b, -1, "B", translate_marker( "B button" ), 620, 344, 220 },
        { mobile_pad::button::a, -1, "A", translate_marker( "A button" ), 620, 408, 220 },
        { mobile_pad::button::count, 1, "R", translate_marker( "Right stick" ), 620, 500, 220 },
        { mobile_pad::button::r3, -1, "R3", translate_marker( "Right stick click" ), 620, 564, 220 },
    }
};

int control_of( const mobile_pad::button b )
{
    for( size_t i = 0; i < controls.size(); ++i ) {
        if( controls[i].b == b && controls[i].stick < 0 ) {
            return static_cast<int>( i );
        }
    }
    return 0;
}

// A short name: "A", "L2", "D-pad up".
std::string short_name( const mobile_pad::button b )
{
    const control &c = controls[static_cast<size_t>( control_of( b ) )];
    return c.glyph[0] != '\0' ? std::string( c.glyph ) : std::string( _( c.name ) );
}

// The glyph of a control inside a square at x, y.
void draw_glyph( const board &b, const control &c, const float x, const float y, const float size,
                 const ImU32 col )
{
    if( c.glyph[0] != '\0' ) {
        b.text_centered( face::mono, size * 0.4f, x, y, size, size, col, c.glyph );
        return;
    }
    const float mid_x = x + size * 0.5f;
    const float mid_y = y + size * 0.5f;
    const float r = size * 0.2f;
    switch( c.b ) {
        case mobile_pad::button::up:
            b.draw->AddTriangleFilled( b.at( mid_x, mid_y - r ), b.at( mid_x + r, mid_y + r ),
                                       b.at( mid_x - r, mid_y + r ), col );
            break;
        case mobile_pad::button::down:
            b.draw->AddTriangleFilled( b.at( mid_x, mid_y + r ), b.at( mid_x - r, mid_y - r ),
                                       b.at( mid_x + r, mid_y - r ), col );
            break;
        case mobile_pad::button::left:
            b.draw->AddTriangleFilled( b.at( mid_x - r, mid_y ), b.at( mid_x + r, mid_y - r ),
                                       b.at( mid_x + r, mid_y + r ), col );
            break;
        case mobile_pad::button::right:
            b.draw->AddTriangleFilled( b.at( mid_x + r, mid_y ), b.at( mid_x - r, mid_y + r ),
                                       b.at( mid_x - r, mid_y - r ), col );
            break;
        case mobile_pad::button::select:
            // two windows
            b.draw->AddRect( b.at( mid_x - r * 1.2f, mid_y - r * 0.4f ), b.at( mid_x + r * 0.4f,
                             mid_y + r * 1.2f ), col, 0.0f, 0, std::max( 1.0f, 1.6f * b.c ) );
            b.draw->AddRect( b.at( mid_x - r * 0.4f, mid_y - r * 1.2f ), b.at( mid_x + r * 1.2f,
                             mid_y + r * 0.4f ), col, 0.0f, 0, std::max( 1.0f, 1.6f * b.c ) );
            break;
        default:
            // three lines
            for( int i = -1; i <= 1; ++i ) {
                b.draw->AddLine( b.at( mid_x - r * 1.2f, mid_y + i * r * 0.8f ),
                                 b.at( mid_x + r * 1.2f, mid_y + i * r * 0.8f ), col,
                                 std::max( 1.0f, 1.8f * b.c ) );
            }
            break;
    }
}

// Where each action of the layout is: "A", "X, long press", "menu Activities".
std::map<std::string, std::vector<std::string>> places_of_actions( const mobile_pad::layout &l )
{
    std::map<std::string, std::vector<std::string>> places;
    for( int i = 0; i < mobile_pad::num_buttons; ++i ) {
        const mobile_pad::button b = static_cast<mobile_pad::button>( i );
        const mobile_pad::assignment &a = l.of( b );
        if( !a.press.empty() ) {
            places[a.press].push_back( string_format( _( "on %s" ), short_name( b ) ) );
        }
        if( l.has_long_press( b ) ) {
            places[a.long_press].push_back( string_format( _( "on %s, long press" ), short_name( b ) ) );
        }
    }
    for( const mobile_pad::menu &m : l.menus ) {
        for( const std::string &slot : m.slots ) {
            if( !slot.empty() ) {
                places[slot].push_back( string_format( _( "in menu %s" ), m.name ) );
            }
        }
    }
    return places;
}

// The buttons a menu is on: "L2 + left stick".
std::vector<std::string> users_of_menu( const mobile_pad::layout &l, const std::string &name,
                                        const bool with_stick )
{
    std::vector<std::string> users;
    for( int i = 0; i < mobile_pad::num_buttons; ++i ) {
        const mobile_pad::button b = static_cast<mobile_pad::button>( i );
        const mobile_pad::assignment &a = l.of( b );
        if( a.menu_left == name ) {
            users.push_back( with_stick ? string_format( _( "%s + left stick" ), short_name( b ) ) :
                             short_name( b ) );
        }
        if( a.menu_right == name ) {
            users.push_back( with_stick ? string_format( _( "%s + right stick" ), short_name( b ) ) :
                             short_name( b ) );
        }
    }
    if( !with_stick ) {
        users.erase( std::unique( users.begin(), users.end() ), users.end() );
    }
    return users;
}

// --- The action picker ---

struct action_group {
    const char *name;
    const char *note;
    std::vector<const char *> ids;
};

const std::vector<action_group> &action_groups()
{
    static const std::vector<action_group> groups = {
        {
            translate_marker( "Common" ), translate_marker( "What most players put on a button first" ),
            { "interact", "examine", "pickup", "inventory", "autoattack", "fire", "reload_weapon", "wait", "eat", "apply", "look", "map" }
        },
        {
            translate_marker( "Movement" ), translate_marker( "Walking, running, stairs, vehicles" ),
            { "pad_step", "open_movement", "cycle_move", "toggle_run", "toggle_crouch", "toggle_prone", "LEVEL_UP", "LEVEL_DOWN", "grab", "haul", "control_vehicle", "auto_travel_mode", "pause", "sleep" }
        },
        {
            translate_marker( "Looking and map" ), translate_marker( "Look around, zoom, the map" ),
            { "look", "peek", "listitems", "zoom_in", "zoom_out", "center", "map", "sky", "missions", "zones", "messages" }
        },
        {
            translate_marker( "Combat" ), translate_marker( "Fighting and weapons" ),
            { "autoattack", "fire", "fire_burst", "throw", "throw_wielded", "reload_weapon", "reload_wielded", "select_fire_mode", "select_default_ammo", "wield", "pick_style", "cast_spell", "recast_spell", "ignore_enemy", "whitelist_enemy", "safemode", "autosafe" }
        },
        {
            translate_marker( "Items" ), translate_marker( "Inventory, wearing, using" ),
            { "inventory", "apply", "apply_wielded", "item_action_menu", "eat", "open_consume", "wear", "take_off", "wield", "read", "drop", "drop_adj", "unload", "unload_container", "insert", "reload_item", "mend", "compare", "organize", "advinv", "sort_armor", "loot" }
        },
        {
            translate_marker( "Interact" ), translate_marker( "Doors, furniture, people" ),
            { "interact", "examine", "examine_and_pickup", "pickup", "pickup_all", "open", "close", "smash", "chat", "butcher", "control_vehicle" }
        },
        {
            translate_marker( "Making" ), translate_marker( "Crafting and building" ),
            { "craft", "recraft", "long_craft", "construct", "disassemble", "butcher", "workout" }
        },
        {
            translate_marker( "Character" ), translate_marker( "Body, skills, powers" ),
            { "player_data", "medical", "bodystatus", "morale", "bionics", "mutations", "cast_spell", "factions", "diary", "sleep" }
        },
        {
            translate_marker( "Game" ), translate_marker( "Menus, saving, safe mode" ),
            { "main_menu", "action_menu", "quicksave", "save", "open_options", "open_autopickup", "open_safemode", "open_autonotes", "open_distraction_manager", "toggle_auto_features", "toggle_auto_pickup", "toggle_thief_mode", "help", "messages" }
        },
    };
    return groups;
}

// The actions no group names, less those a button is no use for.
const std::vector<std::string> &other_actions()
{
    static std::vector<std::string> others;
    static bool made = false;
    if( !made ) {
        made = true;
        std::set<std::string> known = {
            "TIMEOUT", "SELECT", "SEC_SELECT", "CLICK_AND_DRAG", "HELP_KEYBINDINGS", "null", "unknown",
            "SUICIDE", "toggle_language_to_en", "toggle_fullscreen", "open_color", "panel_mgmt",
            "toggle_panel_adm", "reload_tileset", "open_world_mods", "quickload"
        };
        for( const action_group &g : action_groups() ) {
            known.insert( g.ids.begin(), g.ids.end() );
        }
        for( int i = 1; i < NUM_ACTIONS; ++i ) {
            const std::string id = action_ident( static_cast<action_id>( i ) );
            if( id.rfind( "debug", 0 ) == 0 || known.count( id ) > 0 ) {
                continue;
            }
            known.insert( id );
            others.push_back( id );
        }
    }
    return others;
}

// What the picker offers and for what.
struct pick_group {
    std::string name;
    std::string note;
    std::vector<std::string> ids;
};

struct pick_spec {
    // "A button, press"
    std::string title;
    std::string badge;
    // the action it has now, or empty
    std::string now;
    // "Play screen"
    std::string scope;
    std::vector<pick_group> groups;
    // the actions' names; the play screen's where this is empty
    std::map<std::string, std::string> names;
    // where each action is now: "on A", "in menu Activities"
    std::map<std::string, std::vector<std::string>> places;

    std::string name_of( const std::string &id ) const {
        if( names.empty() || id.empty() ) {
            return action_name( id );
        }
        const auto found = names.find( id );
        return found != names.end() ? found->second : id;
    }
};

// The picker for the play screen's layout.  own_place: where the action being
// chosen for is, as places_of_actions says it.
pick_spec play_pick( const std::string &title, const std::string &badge, const std::string &now,
                     const std::string &own_place )
{
    pick_spec spec;
    spec.title = title;
    spec.badge = badge;
    spec.now = now;
    spec.scope = _( "Play screen" );
    for( const action_group &g : action_groups() ) {
        spec.groups.push_back( { _( g.name ), _( g.note ), std::vector<std::string>( g.ids.begin(), g.ids.end() ) } );
    }
    spec.groups.push_back( { _( "Other" ), _( "Everything else the play screen can do" ), other_actions() } );
    spec.places = places_of_actions( mobile_pad::current() );
    for( std::pair<const std::string, std::vector<std::string>> &place : spec.places ) {
        std::vector<std::string> &where = place.second;
        where.erase( std::remove( where.begin(), where.end(), own_place ), where.end() );
    }
    return spec;
}

class picker_window : public cataimgui::window
{
    public:
        explicit picker_window( const pick_spec &spec ) :
            cataimgui::window( "MOBILE_PAD_PICKER", page_flags ), picked( spec.now ), spec( spec ),
            title( spec.title ), badge( spec.badge ), places( spec.places ) {
        }
        bool done = false;
        bool accepted = false;
        bool want_search = false;
        std::string picked;
        std::string filter;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        pick_spec spec;
        std::string title;
        std::string badge;
        std::map<std::string, std::vector<std::string>> places;
        int group = 0;
        bool rewind = true;
};

void picker_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const bool wide = viewport->Size.x >= viewport->Size.y;
    const float cw = wide ? 1280.0f : 880.0f;
    const float ch = wide ? 800.0f : 1500.0f;
    const board b = fit_board( cw, ch );
    float title_end = 0.0f;
    // the badge sits between the arrow and the title
    if( top_bar( b, cw, "", nullptr ) ) {
        done = true;
    }
    b.box( 64.0f, 12.0f, 40.0f, 40.0f, mobile_ui::col_accent, 0, 11.0f );
    b.text_centered( face::mono, badge.size() > 2 ? 11.0f : 16.0f, 64.0f, 12.0f, 40.0f, 40.0f,
                     mobile_ui::col_on_accent, badge );
    const ImVec2 title_size = b.measure( face::display, 28.0f, title );
    b.text( face::display, 28.0f, 116.0f, ( 64.0f - title_size.y ) * 0.5f, mobile_ui::col_fg, title );
    title_end = 116.0f + title_size.x;
    // search
    const float search_w = wide ? 300.0f : 240.0f;
    const float search_x = cw - 20.0f - search_w;
    b.text( face::body, 14.0f, title_end + 14.0f, 24.0f, mobile_ui::col_muted,
            b.fit( face::body, 14.0f, string_format( _( "%1$s · now: %2$s" ), spec.scope,
                    spec.name_of( picked ) ), search_x - title_end - 28.0f ) );
    bool search_held = false;
    if( b.press( "##pad_search", search_x, 10.0f, search_w, 44.0f, &search_held ) ) {
        want_search = true;
    }
    b.box( search_x, 10.0f, search_w, 44.0f, search_held ? mobile_ui::col_button_down : col_well,
           mobile_ui::col_line, 12.0f );
    mobile_ui::icon( b.draw, "search", b.at( search_x + 12.0f, 23.0f ), 18.0f * b.c / b.u,
                     mobile_ui::col_muted );
    b.text( face::body, 15.0f, search_x + 38.0f, 22.0f,
            filter.empty() ? mobile_ui::col_muted : mobile_ui::col_fg,
            b.fit( face::body, 15.0f, filter.empty() ? std::string( _( "Search actions" ) ) : filter,
                   search_w - 50.0f ) );

    const std::vector<pick_group> &groups = spec.groups;
    const int group_count = static_cast<int>( groups.size() );
    group = std::max( 0, std::min( group, group_count - 1 ) );
    // which actions the list shows
    std::vector<std::string> shown;
    std::string note;
    if( !filter.empty() ) {
        std::set<std::string> seen;
        for( const pick_group &g : groups ) {
            for( const std::string &id : g.ids ) {
                if( seen.insert( id ).second && lcmatch( spec.name_of( id ), filter ) ) {
                    shown.push_back( id );
                }
            }
        }
        note = string_format( _( "Search: %s" ), filter );
    } else if( group_count > 0 ) {
        shown = groups[static_cast<size_t>( group )].ids;
        note = groups[static_cast<size_t>( group )].note;
    }

    // the groups: a column on a wide screen, a row of chips on a tall one
    const float list_x = wide ? 250.0f : 0.0f;
    float list_y = 64.0f;
    if( wide ) {
        b.draw->AddLine( b.at( 250.0f, 64.0f ), b.at( 250.0f, ch ), mobile_ui::col_line,
                         std::max( 1.0f, b.c ) );
    }
    for( int i = 0; i < group_count; ++i ) {
        const std::string &name = groups[static_cast<size_t>( i )].name;
        const int count = static_cast<int>( groups[static_cast<size_t>( i )].ids.size() );
        const bool on = filter.empty() && i == group;
        float x;
        float y;
        float w;
        if( wide ) {
            x = 12.0f;
            y = 76.0f + i * 56.0f;
            w = 226.0f;
        } else {
            w = ( cw - 24.0f - 4.0f * 8.0f ) / 5.0f;
            x = 12.0f + ( i % 5 ) * ( w + 8.0f );
            y = 76.0f + ( i / 5 ) * 60.0f;
        }
        bool held = false;
        if( b.press( string_format( "##pad_group%d", i ), x, y, w, 52.0f, &held ) ) {
            group = i;
            filter.clear();
            rewind = true;
        }
        b.box( x, y, w, 52.0f, on ? mobile_ui::col_accent : held ? mobile_ui::col_button_down : 0, 0,
               12.0f );
        const ImU32 col = on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft;
        const std::string count_text = string_format( "%d", count );
        const float count_w = b.measure( face::mono, 12.0f, count_text ).x;
        b.text( face::bold, 15.0f, x + 14.0f, y + 17.0f, col,
                b.fit( face::bold, 15.0f, name, w - 36.0f - count_w ) );
        b.text( face::mono, 12.0f, x + w - 14.0f - count_w, y + 19.0f, col, count_text );
    }
    if( !wide ) {
        list_y = 76.0f + 2.0f * 60.0f + 4.0f;
        b.draw->AddLine( b.at( 0.0f, list_y ), b.at( cw, list_y ), mobile_ui::col_line,
                         std::max( 1.0f, b.c ) );
    }
    const float list_w = cw - list_x;

    // "Nothing" and what the group is
    if( text_button( b, "##pad_nothing", list_x + 16.0f, list_y + 12.0f, 120.0f, 48.0f,
                     _( "Nothing" ), picked.empty() ? button_look::primary : button_look::plain ) ) {
        picked.clear();
    }
    const float note_w = b.measure( face::body, 13.0f, note ).x;
    b.text( face::body, 13.0f, list_x + list_w - 16.0f - note_w, list_y + 28.0f,
            mobile_ui::col_muted, note );
    const float rows_y = list_y + 72.0f;
    b.draw->AddLine( b.at( list_x, rows_y ), b.at( cw, rows_y ), col_row_line,
                     std::max( 1.0f, b.c ) );
    const float footer_y = ch - 76.0f;

    // the actions
    ImGui::SetCursorScreenPos( b.at( list_x, rows_y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##pad_actions", ImVec2( list_w * b.c, ( footer_y - rows_y ) * b.c ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    if( rewind ) {
        ImGui::SetScrollY( 0.0f );
        rewind = false;
    }
    {
        board rows = b;
        rows.draw = ImGui::GetWindowDrawList();
        rows.origin = ImGui::GetCursorScreenPos();
        const float row_h = 60.0f;
        float y = 0.0f;
        for( const std::string &id : shown ) {
            bool held = false;
            if( rows.press( "##pad_action_" + id, 0.0f, y, list_w, row_h, &held ) ) {
                picked = id;
            }
            const bool on = id == picked;
            if( on || held ) {
                rows.draw->AddRectFilled( rows.at( 0.0f, y ), rows.at( list_w, y + row_h ),
                                          on ? mobile_ui::col_accent_soft : mobile_ui::col_fill_soft );
            }
            std::string where;
            const auto found = places.find( id );
            if( found != places.end() && !found->second.empty() ) {
                where = string_format( _( "now %s" ), enumerate_as_string( found->second ) );
            }
            const float where_w = std::min( rows.measure( face::mono, 12.0f, where ).x,
                                            list_w * 0.45f );
            rows.text( face::bold, 16.0f, 18.0f, y + 20.0f, mobile_ui::col_fg,
                       rows.fit( face::bold, 16.0f, spec.name_of( id ), list_w - 84.0f - where_w ) );
            if( !where.empty() ) {
                const std::string where_fit = rows.fit( face::mono, 12.0f, where, where_w );
                rows.text( face::mono, 12.0f, list_w - 48.0f - rows.measure( face::mono, 12.0f,
                           where_fit ).x, y + 23.0f, mobile_ui::col_muted, where_fit );
            }
            rows.draw->AddLine( rows.at( 0.0f, y + row_h ), rows.at( list_w, y + row_h ), col_row_line,
                                std::max( 1.0f, b.c ) );
            y += row_h;
        }
        if( shown.empty() ) {
            rows.text( face::body, 15.0f, 18.0f, 20.0f, mobile_ui::col_muted,
                       _( "No action has that in its name." ) );
        }
        ImGui::SetCursorScreenPos( rows.origin );
        ImGui::Dummy( ImVec2( 1.0f, ( y + 12.0f ) * b.c ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // the footer
    b.draw->AddRectFilled( b.at( list_x, footer_y ), b.at( cw, ch ), mobile_ui::col_panel );
    b.draw->AddLine( b.at( list_x, footer_y ), b.at( cw, footer_y ), mobile_ui::col_line,
                     std::max( 1.0f, b.c ) );
    std::string footer = string_format( _( "%1$s will do: %2$s" ), title, spec.name_of( picked ) );
    const auto elsewhere = places.find( picked );
    if( elsewhere != places.end() && !elsewhere->second.empty() ) {
        footer += "  " + string_format( _( "It is also %s." ), enumerate_as_string( elsewhere->second ) );
    }
    b.text( face::body, 14.0f, list_x + 16.0f, footer_y + 20.0f, mobile_ui::col_text_soft, footer,
            list_w - 16.0f - 290.0f );
    if( text_button( b, "##pad_cancel", cw - 16.0f - 130.0f - 12.0f - 110.0f, footer_y + 14.0f,
                     110.0f, 48.0f, _( "Cancel" ) ) ) {
        done = true;
    }
    if( text_button( b, "##pad_use", cw - 16.0f - 130.0f, footer_y + 14.0f, 130.0f, 48.0f,
                     _( "Use this" ), button_look::primary ) ) {
        done = true;
        accepted = true;
    }
}

// Asks for an action.  Nothing if cancelled; an empty string is "Nothing".
std::optional<std::string> pick_action( const pick_spec &spec )
{
    picker_window window( spec );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    ui_manager::redraw();
    while( !window.done ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            break;
        }
        if( window.want_search ) {
            window.want_search = false;
            mobile_entry::spec what;
            what.title = _( "Search actions" );
            what.text = window.filter;
            if( const std::optional<std::string> typed = mobile_entry::query( what ) ) {
                window.filter = *typed;
            }
        }
    }
    if( !window.accepted ) {
        return std::nullopt;
    }
    return window.picked;
}

// --- The menu editor ---

const std::array<const char *, mobile_pad::num_slots> slot_names = { {
        translate_marker( "up" ), translate_marker( "up-right" ), translate_marker( "right" ),
        translate_marker( "down-right" ), translate_marker( "down" ), translate_marker( "down-left" ),
        translate_marker( "left" ), translate_marker( "up-left" )
    }
};

class menus_window : public cataimgui::window
{
    public:
        explicit menus_window( const std::string &start ) : cataimgui::window( "MOBILE_PAD_MENUS",
                    page_flags ) {
            const mobile_pad::layout &l = mobile_pad::current();
            for( size_t i = 0; i < l.menus.size(); ++i ) {
                if( l.menus[i].name == start ) {
                    menu = static_cast<int>( i );
                }
            }
        }
        enum class ask : int {
            none,
            rename,
            choose,
            remove
        };
        bool done = false;
        ask asked = ask::none;
        int menu = 0;
        int slot = 0;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
};

void menus_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const bool wide = viewport->Size.x >= viewport->Size.y;
    const float cw = wide ? 1280.0f : 880.0f;
    const float ch = wide ? 800.0f : 1500.0f;
    const board b = fit_board( cw, ch );
    mobile_pad::layout &l = mobile_pad::current();
    if( top_bar( b, cw, _( "Radial menus" ), nullptr ) ) {
        done = true;
    }
    menu = std::max( 0, std::min( menu, static_cast<int>( l.menus.size() ) - 1 ) );
    const bool any = !l.menus.empty();
    if( any && text_button( b, "##pad_menu_delete", cw - 20.0f - 190.0f, 10.0f, 190.0f, 44.0f,
                            _( "Delete this menu" ) ) ) {
        asked = ask::remove;
    }

    // the menus: a column on a wide screen, rows on top on a tall one
    const float list_w = wide ? 250.0f : cw;
    float y = 76.0f;
    for( size_t i = 0; i < l.menus.size(); ++i ) {
        const mobile_pad::menu &m = l.menus[i];
        const bool on = static_cast<int>( i ) == menu;
        bool held = false;
        if( b.press( string_format( "##pad_menu%d", static_cast<int>( i ) ), 12.0f, y, list_w - 24.0f,
                     64.0f, &held ) ) {
            menu = static_cast<int>( i );
            slot = 0;
        }
        b.box( 12.0f, y, list_w - 24.0f, 64.0f,
               on ? mobile_ui::col_accent : held ? mobile_ui::col_button_down : 0, 0, 12.0f );
        const int filled = static_cast<int>( std::count_if( m.slots.begin(),
        m.slots.end(), []( const std::string & s ) {
            return !s.empty();
        } ) );
        const std::vector<std::string> users = users_of_menu( l, m.name, false );
        const std::string info = string_format( _( "%1$d of 8 slots · %2$s" ), filled,
                                                users.empty() ? std::string( _( "not on a button" ) ) :
                                                string_format( _( "on %s" ), enumerate_as_string( users ) ) );
        b.text( face::bold, 16.0f, 26.0f, y + 12.0f, on ? mobile_ui::col_on_accent : mobile_ui::col_fg,
                b.fit( face::bold, 16.0f, m.name, list_w - 52.0f ) );
        b.text( face::body, 13.0f, 26.0f, y + 35.0f,
                on ? mobile_ui::col_on_accent : mobile_ui::col_muted,
                b.fit( face::body, 13.0f, info, list_w - 52.0f ) );
        y += 70.0f;
    }
    if( text_button( b, "##pad_menu_new", 12.0f, y, list_w - 24.0f, 52.0f, _( "+ New menu" ) ) ) {
        mobile_pad::menu fresh;
        fresh.name = l.free_menu_name( _( "Menu" ) );
        l.menus.push_back( fresh );
        mobile_pad::save();
        menu = static_cast<int>( l.menus.size() ) - 1;
        slot = 0;
    }
    y += 64.0f;
    float pane_x = 0.0f;
    float pane_y = 64.0f;
    float pane_w = cw;
    float pane_h = ch - 64.0f;
    if( wide ) {
        b.draw->AddLine( b.at( 250.0f, 64.0f ), b.at( 250.0f, ch ), mobile_ui::col_line,
                         std::max( 1.0f, b.c ) );
        b.text( face::body, 13.0f, 12.0f, ch - 70.0f, mobile_ui::col_muted,
                _( "Menus are not tied to a button or a stick.  One menu can be used by several buttons." ),
                226.0f );
        pane_x = 250.0f;
        pane_w = cw - 250.0f;
    } else {
        b.draw->AddLine( b.at( 0.0f, y ), b.at( cw, y ), mobile_ui::col_line, std::max( 1.0f, b.c ) );
        pane_y = y;
        pane_h = ch - y;
    }
    if( !any ) {
        b.text( face::body, 15.0f, pane_x + 20.0f, pane_y + 24.0f, mobile_ui::col_muted,
                _( "There is no menu yet.  Make one with \"New menu\"." ), pane_w - 40.0f );
        return;
    }
    mobile_pad::menu &m = l.menus[static_cast<size_t>( menu )];

    // the name
    b.text( face::mono, 11.0f, pane_x + 20.0f, pane_y + 32.0f, mobile_ui::col_muted, _( "NAME" ) );
    bool name_held = false;
    if( b.press( "##pad_menu_name", pane_x + 64.0f, pane_y + 14.0f, 250.0f, 48.0f, &name_held ) ) {
        asked = ask::rename;
    }
    b.box( pane_x + 64.0f, pane_y + 14.0f, 250.0f, 48.0f,
           name_held ? mobile_ui::col_button_down : col_well, mobile_ui::col_line, 12.0f );
    b.text( face::bold, 17.0f, pane_x + 78.0f, pane_y + 27.0f, mobile_ui::col_fg,
            b.fit( face::bold, 17.0f, m.name, 222.0f ) );
    const std::vector<std::string> users = users_of_menu( l, m.name, true );
    const std::string used = string_format( _( "Used by: %s" ),
                                            users.empty() ? std::string( _( "no button yet" ) ) : enumerate_as_string( users ) );
    const std::string used_fit = b.fit( face::body, 14.0f, used, pane_w - 360.0f );
    b.text( face::body, 14.0f, pane_x + pane_w - 20.0f - b.measure( face::body, 14.0f, used_fit ).x,
            pane_y + 30.0f, mobile_ui::col_text_soft, used_fit );
    b.text( face::body, 14.0f, pane_x + 20.0f, pane_y + 76.0f, mobile_ui::col_muted,
            _( "A menu is a name and eight slots.  To use it, pick a button on the controller page and choose this menu for its left or right stick." ),
            pane_w - 40.0f );

    // the ring of slots
    const float mid_x = pane_x + pane_w * 0.5f;
    const float mid_y = pane_y + 120.0f + ( pane_h - 120.0f - 80.0f ) * 0.5f;
    const float reach_x = std::min( 255.0f, pane_w * 0.5f - 105.0f );
    const float reach_y = std::min( 200.0f, ( pane_h - 200.0f ) * 0.5f - 44.0f );
    b.draw->AddCircle( b.at( mid_x, mid_y ), 100.0f * b.c, col_line_strong, 64,
                       std::max( 1.0f, 2.0f * b.c ) );
    b.text_centered( face::display, 22.0f, mid_x - 80.0f, mid_y - 20.0f, 160.0f, 40.0f,
                     mobile_ui::col_text_soft, m.name );
    slot = std::max( 0, std::min( slot, mobile_pad::num_slots - 1 ) );
    for( int i = 0; i < mobile_pad::num_slots; ++i ) {
        const float turn = ( static_cast<float>( i ) * 45.0f - 90.0f ) * 3.14159265f / 180.0f;
        const float x = mid_x + std::cos( turn ) * reach_x - 85.0f;
        const float sy = mid_y + std::sin( turn ) * reach_y - 34.0f;
        const std::string &id = m.slots[static_cast<size_t>( i )];
        const bool on = i == slot;
        bool held = false;
        if( b.press( string_format( "##pad_slot%d", i ), x, sy, 170.0f, 68.0f, &held ) ) {
            if( on ) {
                // a second tap on the slot chooses for it
                asked = ask::choose;
            }
            slot = i;
        }
        b.box( x, sy, 170.0f, 68.0f,
               on ? mobile_ui::col_accent_soft : held ? mobile_ui::col_button_down :
               id.empty() ? 0 : col_chip, on ? mobile_ui::col_accent : mobile_ui::col_line, 14.0f );
        b.text_centered( face::mono, 10.0f, x + 6.0f, sy + 7.0f, 158.0f, 14.0f, mobile_ui::col_muted,
                         to_upper_case( _( slot_names[static_cast<size_t>( i )] ) ) );
        b.text_centered_2( face::bold, 14.0f, x + 6.0f, sy + 25.0f, 158.0f, 38.0f,
                           id.empty() ? mobile_ui::col_muted : mobile_ui::col_fg,
                           id.empty() ? std::string( _( "Empty" ) ) : action_name( id ) );
    }

    // what the selected slot holds
    const float bar_y = pane_y + pane_h - 64.0f;
    const std::string &now = m.slots[static_cast<size_t>( slot )];
    b.text( face::body, 14.0f, pane_x + 20.0f, bar_y + 15.0f, mobile_ui::col_text_soft,
            b.fit( face::body, 14.0f, string_format( _( "Slot %1$s: %2$s" ),
                    _( slot_names[static_cast<size_t>( slot )] ),
                    now.empty() ? std::string( _( "empty" ) ) : action_name( now ) ), pane_w - 330.0f ) );
    if( text_button( b, "##pad_slot_choose", pane_x + pane_w - 20.0f - 110.0f - 10.0f - 160.0f, bar_y,
                     160.0f, 48.0f, _( "Choose action" ), button_look::primary ) ) {
        asked = ask::choose;
    }
    if( text_button( b, "##pad_slot_empty", pane_x + pane_w - 20.0f - 110.0f, bar_y, 110.0f, 48.0f,
                     _( "Empty it" ), button_look::plain, !now.empty() ) ) {
        m.slots[static_cast<size_t>( slot )].clear();
        mobile_pad::save();
    }
}

void show_menus( const std::string &start )
{
    menus_window window( start );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    ui_manager::redraw();
    while( !window.done ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            break;
        }
        const menus_window::ask asked = window.asked;
        window.asked = menus_window::ask::none;
        mobile_pad::layout &l = mobile_pad::current();
        if( asked == menus_window::ask::none || window.menu < 0 ||
            static_cast<size_t>( window.menu ) >= l.menus.size() ) {
            continue;
        }
        mobile_pad::menu &m = l.menus[static_cast<size_t>( window.menu )];
        if( asked == menus_window::ask::rename ) {
            mobile_entry::spec what;
            what.title = _( "Name of the menu" );
            what.text = m.name;
            what.max_length = 24;
            const std::optional<std::string> typed = mobile_entry::query( what );
            if( typed && !typed->empty() && *typed != m.name ) {
                if( l.find_menu( *typed ) != nullptr ) {
                    popup( _( "Another menu has that name." ) );
                } else {
                    const std::string old_name = m.name;
                    l.rename_menu( old_name, *typed );
                    mobile_pad::save();
                }
            }
        } else if( asked == menus_window::ask::choose ) {
            const std::string title = string_format( _( "%1$s, slot %2$s" ), m.name,
                                      _( slot_names[static_cast<size_t>( window.slot )] ) );
            const std::optional<std::string> chosen = pick_action( play_pick( title,
                    string_format( "%d", window.slot + 1 ), m.slots[static_cast<size_t>( window.slot )],
                    string_format( _( "in menu %s" ), m.name ) ) );
            if( chosen ) {
                m.slots[static_cast<size_t>( window.slot )] = *chosen;
                mobile_pad::save();
            }
        } else if( asked == menus_window::ask::remove ) {
            const std::vector<std::string> users = users_of_menu( l, m.name, true );
            if( query_yn( users.empty() ? _( "Delete the menu \"%1$s\"?%2$s" ) :
                          _( "Delete the menu \"%1$s\"?  It is used by: %2$s." ), m.name,
                          enumerate_as_string( users ) ) ) {
                const std::string gone = m.name;
                l.remove_menu( gone );
                mobile_pad::save();
                window.menu = 0;
                window.slot = 0;
            }
        }
    }
}

// --- The controller page ---

// The parts of the picture: a control, its shape and where it is.
struct picture_part {
    int control;
    const char *label;
    float x;
    float y;
    float w;
    float h;
    // 0 a box, 1 round, 2 a stick's well
    int shape;
    float radius;
    bool top;
};

const std::array<picture_part, 18> picture = { {
        { 0, "L2", 266, 182, 88, 24, 0, 8, true },
        { 1, "L1", 258, 209, 104, 24, 0, 6, false },
        { 10, "R2", 486, 182, 88, 24, 0, 8, true },
        { 11, "R1", 478, 209, 104, 24, 0, 6, false },
        { 2, "", 285, 275, 78, 78, 2, 0, false },
        { 3, "L3", 305, 295, 38, 38, 1, 0, false },
        { 16, "", 480, 355, 78, 78, 2, 0, false },
        { 17, "R3", 500, 375, 38, 38, 1, 0, false },
        { 4, "", 318, 378, 26, 30, 0, 5, false },
        { 7, "", 318, 430, 26, 30, 0, 5, false },
        { 5, "", 290, 406, 30, 26, 0, 5, false },
        { 6, "", 342, 406, 30, 26, 0, 5, false },
        { 12, "Y", 504, 252, 30, 30, 1, 0, false },
        { 15, "A", 504, 316, 30, 30, 1, 0, false },
        { 13, "X", 472, 284, 30, 30, 1, 0, false },
        { 14, "B", 536, 284, 30, 30, 1, 0, false },
        { 8, "", 382, 264, 30, 16, 0, 8, false },
        { 9, "", 428, 264, 30, 16, 0, 8, false },
    }
};

bool is_dpad( const mobile_pad::button b )
{
    return b == mobile_pad::button::up || b == mobile_pad::button::down ||
           b == mobile_pad::button::left || b == mobile_pad::button::right;
}

std::string stick_mode_name( const mobile_pad::stick_mode m )
{
    switch( m ) {
        case mobile_pad::stick_mode::move:
            return _( "Move" );
        case mobile_pad::stick_mode::direction:
            return _( "Pick a direction" );
        case mobile_pad::stick_mode::pan:
            return _( "Pan the view" );
        case mobile_pad::stick_mode::zoom:
            return _( "Zoom" );
        default:
            return _( "Nothing" );
    }
}

// --- Screens other than the play screen ---
//
// They keep upstream's scheme: a button is bound to an action of the screen's
// input context in the keybindings, the left trigger is a modifier (a second
// action per button), and in menus some buttons are fixed.  The page shows and
// changes those bindings.

// upstream's code for a button, plain and with the left trigger held (or -1)
int joy_code( const mobile_pad::button b, const bool with_l2 )
{
    switch( b ) {
        case mobile_pad::button::a:
            return with_l2 ? JOY_ALT_A : JOY_A;
        case mobile_pad::button::b:
            return with_l2 ? JOY_ALT_B : JOY_B;
        case mobile_pad::button::x:
            return with_l2 ? JOY_ALT_X : JOY_X;
        case mobile_pad::button::y:
            return with_l2 ? JOY_ALT_Y : JOY_Y;
        case mobile_pad::button::l1:
            return with_l2 ? JOY_ALT_LB : JOY_LB;
        case mobile_pad::button::r1:
            return with_l2 ? JOY_ALT_RB : JOY_RB;
        case mobile_pad::button::r2:
            return with_l2 ? JOY_ALT_RT : JOY_RT;
        case mobile_pad::button::l3:
            return with_l2 ? JOY_ALT_LS : JOY_LS;
        case mobile_pad::button::r3:
            return with_l2 ? JOY_ALT_RS : JOY_RS;
        case mobile_pad::button::up:
            return with_l2 ? JOY_ALT_UP : JOY_UP;
        case mobile_pad::button::down:
            return with_l2 ? JOY_ALT_DOWN : JOY_DOWN;
        case mobile_pad::button::left:
            return with_l2 ? JOY_ALT_LEFT : JOY_LEFT;
        case mobile_pad::button::right:
            return with_l2 ? JOY_ALT_RIGHT : JOY_RIGHT;
        case mobile_pad::button::select:
            return with_l2 ? JOY_ALT_BACK : JOY_BACK;
        case mobile_pad::button::start:
            return with_l2 ? JOY_ALT_START : JOY_START;
        default:
            return -1;
    }
}

// What a button always does in a menu, whatever is bound; empty if nothing.
std::string fixed_in_menus( const mobile_pad::button b )
{
    switch( b ) {
        case mobile_pad::button::a:
            return _( "Confirm" );
        case mobile_pad::button::b:
        case mobile_pad::button::select:
        case mobile_pad::button::start:
            return _( "Back" );
        case mobile_pad::button::l1:
            return _( "Previous tab" );
        case mobile_pad::button::r1:
            return _( "Next tab" );
        case mobile_pad::button::up:
        case mobile_pad::button::down:
        case mobile_pad::button::left:
        case mobile_pad::button::right:
            return _( "Move the selection" );
        default:
            return "";
    }
}

// The gamepad bindings of one input context.
struct screen_bindings {
    std::string category;
    std::vector<binding> actions;
    // upstream's button code to the action bound to it
    std::map<int, std::string> by_code;
    std::map<std::string, std::string> names;

    std::string action_on( const int code ) const {
        const auto found = by_code.find( code );
        return found != by_code.end() ? found->second : std::string();
    }
    std::string name_of( const std::string &id ) const {
        const auto found = names.find( id );
        return found != names.end() ? found->second : id;
    }
};

screen_bindings load_screen( const std::string &category )
{
    screen_bindings screen;
    screen.category = category;
    for( const binding &b : bindings() ) {
        if( b.category != category ) {
            continue;
        }
        screen.names[b.action] = b.name;
        for( const int code : b.buttons ) {
            screen.by_code[code] = b.action;
        }
        screen.actions.push_back( b );
    }
    return screen;
}

// Gives a button code to an action of a context (or to none), and saves.
void bind_code( const std::string &category, const int code, const std::string &action )
{
    for( const binding &b : bindings() ) {
        if( b.category != category ) {
            continue;
        }
        const bool has = std::find( b.buttons.begin(), b.buttons.end(), code ) != b.buttons.end();
        std::vector<int> wanted = b.buttons;
        if( b.action == action && !has ) {
            wanted.push_back( code );
        } else if( b.action != action && has ) {
            wanted.erase( std::remove( wanted.begin(), wanted.end(), code ), wanted.end() );
        } else {
            continue;
        }
        set_buttons( category, b.action, wanted );
    }
    save();
}

// The contexts a controller reaches, the menus' shared one first.
std::vector<std::string> other_screens()
{
    std::vector<std::string> categories;
    for( const binding &b : bindings() ) {
        if( b.category != "DEFAULTMODE" &&
            std::find( categories.begin(), categories.end(), b.category ) == categories.end() ) {
            categories.push_back( b.category );
        }
    }
    std::stable_sort( categories.begin(), categories.end(), []( const std::string & a,
    const std::string & b ) {
        const bool a_first = a == "default";
        const bool b_first = b == "default";
        return a_first != b_first ? a_first : context_name( a ) < context_name( b );
    } );
    return categories;
}

class pad_window : public cataimgui::window
{
    public:
        pad_window() : cataimgui::window( "MOBILE_PAD", page_flags ) {
        }
        enum class ask : int {
            none,
            press,
            long_press,
            with_l2,
            menus,
            screen,
            clear,
            defaults
        };
        bool done = false;
        ask asked = ask::none;
        // index into controls
        int selected = 0;
        // the input context shown; empty for the play screen and its layout
        std::string category;
        // the bindings of that context, read again when they change
        screen_bindings screen;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        enum class drop : int {
            none,
            menu_left,
            menu_right,
            stick,
            dpad
        };
        // the open dropdown, where it is (canvas pixels) and what it offers
        drop dropdown = drop::none;
        float drop_x = 0.0f;
        float drop_y = 0.0f;
        float drop_w = 0.0f;
        std::vector<std::string> choices;
        int drop_now = 0;

        // A dropdown's closed box.  Opens it when tapped.
        void drop_box( const board &b, drop which, float x, float y, float w, const std::string &shown,
                       bool muted );
        void open_choices( const control &sel );
        void choose( const control &sel, int index );
        void draw_panel( const board &b, float px, float py, float pw );
        void draw_play_panel( const board &b, const control &sel, float x, float y, float w );
        void draw_other_panel( const board &b, const control &sel, float x, float y, float w );
};

void pad_window::open_choices( const control &sel )
{
    const mobile_pad::layout &l = mobile_pad::current();
    choices.clear();
    drop_now = 0;
    if( dropdown == drop::menu_left || dropdown == drop::menu_right ) {
        const mobile_pad::assignment &a = l.of( sel.b );
        const std::string &now = dropdown == drop::menu_left ? a.menu_left : a.menu_right;
        choices.emplace_back( _( "None" ) );
        for( const mobile_pad::menu &m : l.menus ) {
            if( m.name == now ) {
                drop_now = static_cast<int>( choices.size() );
            }
            choices.push_back( m.name );
        }
    } else if( dropdown == drop::stick ) {
        for( int i = 0; i < static_cast<int>( mobile_pad::stick_mode::count ); ++i ) {
            choices.push_back( stick_mode_name( static_cast<mobile_pad::stick_mode>( i ) ) );
        }
        drop_now = static_cast<int>( l.sticks[static_cast<size_t>( sel.stick )] );
    } else if( dropdown == drop::dpad ) {
        choices.emplace_back( _( "Four buttons" ) );
        choices.emplace_back( _( "Move" ) );
        drop_now = l.dpad_moves ? 1 : 0;
    }
}

void pad_window::choose( const control &sel, const int index )
{
    mobile_pad::layout &l = mobile_pad::current();
    if( dropdown == drop::menu_left || dropdown == drop::menu_right ) {
        mobile_pad::assignment &a = l.of( sel.b );
        ( dropdown == drop::menu_left ? a.menu_left : a.menu_right ) =
            index == 0 ? std::string() : choices[static_cast<size_t>( index )];
    } else if( dropdown == drop::stick ) {
        l.sticks[static_cast<size_t>( sel.stick )] = static_cast<mobile_pad::stick_mode>( index );
    } else if( dropdown == drop::dpad ) {
        l.dpad_moves = index == 1;
    }
    mobile_pad::save();
}

void pad_window::drop_box( const board &b, const drop which, const float x, const float y,
                           const float w, const std::string &shown, const bool muted )
{
    bool held = false;
    if( b.press( string_format( "##pad_dropdown%d", static_cast<int>( which ) ), x, y, w, 48.0f,
                 &held ) ) {
        dropdown = which;
        drop_x = x;
        drop_y = y + 50.0f;
        drop_w = w;
    }
    b.box( x, y, w, 48.0f, held ? mobile_ui::col_button_down : col_well,
           dropdown == which ? mobile_ui::col_accent : col_line_strong, 12.0f );
    b.text( face::bold, 16.0f, x + 12.0f, y + 13.0f, muted ? mobile_ui::col_muted : mobile_ui::col_fg,
            b.fit( face::bold, 16.0f, shown, w - 48.0f ) );
    const float ax = x + w - 24.0f;
    const float ay = y + 21.0f;
    b.draw->AddTriangleFilled( b.at( ax - 6.0f, ay ), b.at( ax + 6.0f, ay ), b.at( ax, ay + 7.0f ),
                               mobile_ui::col_text_soft );
}

void pad_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const bool wide = viewport->Size.x >= viewport->Size.y;
    const float cw = wide ? 1280.0f : 880.0f;
    const float ch = wide ? 800.0f : 1500.0f;
    board b = fit_board( cw, ch );
    mobile_pad::layout &l = mobile_pad::current();
    const control &sel = controls[static_cast<size_t>( selected )];
    const bool play = category.empty();
    const float panel_x = wide ? 880.0f : 0.0f;
    const float panel_y = wide ? 64.0f : 64.0f + 700.0f;
    const float panel_w = wide ? 400.0f : 880.0f;
    const float panel_h = ch - panel_y;

    // An open dropdown comes first so that it gets the tap, and the rest of the
    // page does not react under it.  It is drawn last.
    const float drop_row = 48.0f;
    float list_y = 0.0f;
    if( dropdown != drop::none ) {
        open_choices( sel );
        list_y = std::max( 70.0f, std::min( drop_y, ch - 8.0f - drop_row * choices.size() ) );
        for( size_t i = 0; i < choices.size(); ++i ) {
            if( b.press( string_format( "##pad_drop%d", static_cast<int>( i ) ), drop_x,
                         list_y + drop_row * i, drop_w, drop_row ) ) {
                choose( sel, static_cast<int>( i ) );
                dropdown = drop::none;
            }
        }
        if( dropdown != drop::none && b.press( "##pad_drop_close", 0.0f, 0.0f, cw, ch ) ) {
            dropdown = drop::none;
        }
        b.live = false;
    }
    const bool show_dropdown = !b.live;

    if( top_bar( b, cw, _( "Controller" ), nullptr ) ) {
        done = true;
    }
    float bar_x = cw - 20.0f;
    if( play ) {
        const std::string menus_label = string_format( _( "Radial menus (%d)" ),
                                        static_cast<int>( l.menus.size() ) );
        bar_x -= 190.0f;
        if( text_button( b, "##pad_menus", bar_x, 10.0f, 190.0f, 44.0f, menus_label ) ) {
            asked = ask::menus;
        }
        bar_x -= 10.0f;
    }
    // which screen: a button that opens the list of them
    const float screen_w = wide ? 330.0f : 280.0f;
    bar_x -= screen_w;
    {
        bool held = false;
        if( b.press( "##pad_screen", bar_x, 10.0f, screen_w, 44.0f, &held ) ) {
            asked = ask::screen;
        }
        b.box( bar_x, 10.0f, screen_w, 44.0f, held ? mobile_ui::col_button_down : col_well,
               mobile_ui::col_accent, 12.0f );
        b.text( face::mono, 11.0f, bar_x + 12.0f, 25.0f, mobile_ui::col_muted, _( "SCREEN" ) );
        b.text( face::bold, 15.0f, bar_x + 70.0f, 22.0f, mobile_ui::col_fg,
                b.fit( face::bold, 15.0f, play ? std::string( _( "Play screen" ) ) : context_name( category ),
                       screen_w - 108.0f ) );
        const float ax = bar_x + screen_w - 22.0f;
        b.draw->AddTriangleFilled( b.at( ax - 6.0f, 29.0f ), b.at( ax + 6.0f, 29.0f ),
                                   b.at( ax, 36.0f ), mobile_ui::col_text_soft );
    }

    // --- the picture and the labels ---
    board pane = b;
    pane.origin = b.at( 0.0f, 64.0f );
    pane.text( face::body, 14.0f, 20.0f, 14.0f, mobile_ui::col_muted,
               play ? _( "Each control shows what it does on the play screen.  Tap one, here or on the picture, to change it." )
               : _( "Each control shows what it does on this screen.  Tap one, here or on the picture, to change it." ),
               840.0f );
    pane.box( 250.0f, 235.0f, 340.0f, 250.0f, col_body, col_line_strong, 70.0f );
    pane.draw->AddRectFilled( pane.at( 318.0f, 406.0f ), pane.at( 344.0f, 432.0f ), col_part );
    for( size_t i = 0; i < picture.size(); ++i ) {
        const picture_part &p = picture[i];
        const bool on = p.control == selected;
        if( pane.press( string_format( "##pad_part%d", static_cast<int>( i ) ), p.x, p.y, p.w, p.h ) ) {
            selected = p.control;
        }
        const ImU32 fill = p.shape == 2 ? col_well : on ? mobile_ui::col_accent : p.top ? col_part_top :
                           col_part;
        if( p.shape == 0 ) {
            pane.box( p.x, p.y, p.w, p.h, fill, 0, p.radius );
        } else {
            const ImVec2 mid = pane.at( p.x + p.w * 0.5f, p.y + p.h * 0.5f );
            pane.draw->AddCircleFilled( mid, p.w * 0.5f * pane.c, fill, 48 );
            if( p.shape == 2 ) {
                pane.draw->AddCircle( mid, p.w * 0.5f * pane.c, on ? mobile_ui::col_accent : col_line_strong,
                                      48, std::max( 1.0f, 3.0f * pane.c ) );
            }
        }
        if( p.label[0] != '\0' ) {
            pane.text_centered( face::mono, 11.0f, p.x, p.y, p.w, p.h,
                                on ? mobile_ui::col_on_accent : mobile_ui::col_fg, p.label );
        }
    }
    for( size_t i = 0; i < controls.size(); ++i ) {
        const control &c = controls[i];
        const bool on = static_cast<int>( i ) == selected;
        bool held = false;
        if( pane.press( string_format( "##pad_chip%d", static_cast<int>( i ) ), c.x, c.y, c.w, 54.0f,
                        &held ) ) {
            selected = static_cast<int>( i );
        }
        pane.box( c.x, c.y, c.w, 54.0f,
                  on ? mobile_ui::col_accent_soft : held ? mobile_ui::col_button_down : col_chip,
                  on ? mobile_ui::col_accent : mobile_ui::col_line, 12.0f );
        pane.box( c.x + 10.0f, c.y + 10.0f, 34.0f, 34.0f, on ? mobile_ui::col_accent : col_part, 0,
                  9.0f );
        draw_glyph( pane, c, c.x + 10.0f, c.y + 10.0f, 34.0f,
                    on ? mobile_ui::col_on_accent : mobile_ui::col_fg );
        std::string main;
        bool main_muted = false;
        std::vector<std::string> subs;
        if( play ) {
            if( c.stick >= 0 ) {
                main = stick_mode_name( l.sticks[static_cast<size_t>( c.stick )] );
            } else if( l.dpad_moves && is_dpad( c.b ) ) {
                main = _( "Move" );
            } else {
                const mobile_pad::assignment &a = l.of( c.b );
                main = action_name( a.press );
                if( const mobile_pad::menu *const m = l.menu_on( c.b, 0 ) ) {
                    subs.push_back( string_format( _( "Hold + L stick: %s" ), m->name ) );
                }
                if( const mobile_pad::menu *const m = l.menu_on( c.b, 1 ) ) {
                    subs.push_back( string_format( _( "Hold + R stick: %s" ), m->name ) );
                }
                if( l.has_long_press( c.b ) ) {
                    subs.push_back( string_format( _( "Long press: %s" ), action_name( a.long_press ) ) );
                }
            }
        } else if( c.stick >= 0 ) {
            main = c.stick == 0 ? _( "With L2: a ring" ) : _( "Move the selection" );
            main_muted = true;
            if( c.stick == 1 ) {
                subs.emplace_back( _( "With L2: a ring" ) );
            }
        } else if( c.b == mobile_pad::button::l2 ) {
            main = _( "Second action of a button" );
            main_muted = true;
        } else {
            const std::string bound = screen.action_on( joy_code( c.b, false ) );
            const std::string fixed = fixed_in_menus( c.b );
            if( !bound.empty() ) {
                main = screen.name_of( bound );
            } else if( !fixed.empty() ) {
                main = fixed;
                main_muted = true;
            } else {
                main = _( "Nothing" );
                main_muted = true;
            }
            const std::string second = screen.action_on( joy_code( c.b, true ) );
            if( !second.empty() ) {
                subs.push_back( string_format( _( "With L2: %s" ), screen.name_of( second ) ) );
            }
        }
        const float text_w = c.w - 64.0f;
        const float lines_h = 17.0f + 13.0f * subs.size();
        float ty = c.y + ( 54.0f - lines_h ) * 0.5f;
        pane.text( face::bold, 14.0f, c.x + 54.0f, ty,
                   main_muted ? mobile_ui::col_muted : mobile_ui::col_fg,
                   pane.fit( face::bold, 14.0f, main, text_w ) );
        ty += 17.0f;
        for( const std::string &sub : subs ) {
            pane.text( face::body, 11.0f, c.x + 54.0f, ty, mobile_ui::col_accent,
                       pane.fit( face::body, 11.0f, sub, text_w ) );
            ty += 13.0f;
        }
    }

    // --- the selected control ---
    b.draw->AddRectFilled( b.at( panel_x, panel_y ), b.at( panel_x + panel_w, panel_y + panel_h ),
                           mobile_ui::col_panel );
    if( wide ) {
        b.draw->AddLine( b.at( panel_x, panel_y ), b.at( panel_x, ch ), mobile_ui::col_line,
                         std::max( 1.0f, b.c ) );
    } else {
        b.draw->AddLine( b.at( 0.0f, panel_y ), b.at( cw, panel_y ), mobile_ui::col_line,
                         std::max( 1.0f, b.c ) );
    }
    draw_panel( b, panel_x, panel_y, panel_w );

    if( show_dropdown ) {
        const float h = drop_row * choices.size();
        b.draw->AddRectFilled( b.at( 0.0f, 0.0f ), b.at( cw, ch ), IM_COL32( 0, 0, 0, 90 ) );
        b.box( drop_x, list_y, drop_w, h, mobile_ui::col_sheet, mobile_ui::col_accent, 12.0f );
        for( size_t i = 0; i < choices.size(); ++i ) {
            const bool on = static_cast<int>( i ) == drop_now;
            if( on ) {
                b.box( drop_x + 4.0f, list_y + drop_row * i + 4.0f, drop_w - 8.0f, drop_row - 8.0f,
                       mobile_ui::col_accent_soft, 0, 9.0f );
            }
            b.text( face::bold, 16.0f, drop_x + 14.0f, list_y + drop_row * i + 13.0f,
                    on ? mobile_ui::col_accent : mobile_ui::col_fg,
                    b.fit( face::bold, 16.0f, choices[i], drop_w - 28.0f ) );
        }
    }
}

void pad_window::draw_panel( const board &b, const float px, const float py, const float pw )
{
    const control &sel = controls[static_cast<size_t>( selected )];
    const float x = px + 18.0f;
    const float w = pw - 36.0f;
    // which control
    b.box( x, py + 18.0f, 52.0f, 52.0f, mobile_ui::col_accent, 0, 14.0f );
    draw_glyph( b, sel, x, py + 18.0f, 52.0f, mobile_ui::col_on_accent );
    b.text( face::display, 24.0f, x + 64.0f, py + 18.0f, mobile_ui::col_fg, _( sel.name ) );
    b.text( face::body, 13.0f, x + 64.0f, py + 50.0f, mobile_ui::col_muted,
            b.fit( face::body, 13.0f, category.empty() ? std::string( _( "Play screen" ) ) :
                   context_name( category ), w - 64.0f ) );
    if( category.empty() ) {
        draw_play_panel( b, sel, x, py + 84.0f, w );
    } else {
        draw_other_panel( b, sel, x, py + 84.0f, w );
    }
    // at the foot of the panel
    const float foot = py + ( 736.0f - 18.0f - 48.0f );
    const float half = ( w - 8.0f ) * 0.5f;
    if( text_button( b, "##pad_clear", x, foot, half, 48.0f, _( "Clear this button" ),
                     button_look::plain, sel.stick < 0 ) ) {
        asked = ask::clear;
    }
    if( text_button( b, "##pad_defaults", x + half + 8.0f, foot, half, 48.0f,
                     category.empty() ? _( "All to default" ) : _( "Screen to default" ) ) ) {
        asked = ask::defaults;
    }
}

void pad_window::draw_play_panel( const board &b, const control &sel, const float x, float y,
                                  const float w )
{
    mobile_pad::layout &l = mobile_pad::current();
    const auto card = [&]( const float cy, const float h, const std::string & label,
    const ImU32 border, const ImU32 label_col ) {
        b.box( x, cy, w, h, 0, border, 14.0f );
        b.text( face::mono, 11.0f, x + 14.0f, cy + 12.0f, label_col, label );
    };
    const auto note = [&]( const float cy, const std::string & text ) {
        const float h = b.measure( face::body, 14.0f, text, w - 28.0f ).y + 28.0f;
        b.box( x, cy, w, h, 0, mobile_ui::col_line, 14.0f );
        b.text( face::body, 14.0f, x + 14.0f, cy + 14.0f, mobile_ui::col_text_soft, text, w - 28.0f );
        return h;
    };

    if( sel.stick >= 0 ) {
        const mobile_pad::stick_mode mode = l.sticks[static_cast<size_t>( sel.stick )];
        card( y, 92.0f, _( "PUSHING THE STICK" ), IM_COL32( 240, 145, 63, 100 ), mobile_ui::col_accent );
        drop_box( b, drop::stick, x + 14.0f, y + 32.0f, w - 28.0f, stick_mode_name( mode ), false );
        y += 106.0f;
        std::string what;
        switch( mode ) {
            case mobile_pad::stick_mode::move:
                what = _( "Push the stick to walk that way; hold it to keep walking." );
                break;
            case mobile_pad::stick_mode::direction:
                what = _( "The stick points at a tile next to you.  \"Step / wait a turn\" walks there, and Interact, Smash, Grab and the like act on it." );
                break;
            case mobile_pad::stick_mode::pan:
                what = _( "Push the stick to scroll the view." );
                break;
            case mobile_pad::stick_mode::zoom:
                what = _( "Up zooms in, down zooms out." );
                break;
            default:
                what = _( "The stick does nothing by itself." );
                break;
        }
        y += note( y, what ) + 14.0f;
        note( y, _( "While a button with a radial menu is held, the stick works that menu instead.  To put a menu on this stick, select a button and choose the menu there." ) );
        return;
    }
    if( is_dpad( sel.b ) ) {
        card( y, 92.0f, _( "THE D-PAD" ), IM_COL32( 240, 145, 63, 100 ), mobile_ui::col_accent );
        drop_box( b, drop::dpad, x + 14.0f, y + 32.0f, w - 28.0f,
                  l.dpad_moves ? _( "Move" ) : _( "Four buttons" ), false );
        y += 106.0f;
        if( l.dpad_moves ) {
            note( y, _( "The d-pad walks: one direction, or two at once for a diagonal.  What its four buttons were set to rests until this is back on \"Four buttons\"." ) );
            return;
        }
    }
    mobile_pad::assignment &a = l.of( sel.b );
    const float change_w = 96.0f;
    // press
    card( y, 78.0f, _( "PRESS" ), mobile_ui::col_line, mobile_ui::col_muted );
    b.text( face::bold, 17.0f, x + 14.0f, y + 36.0f, mobile_ui::col_fg,
            b.fit( face::bold, 17.0f, action_name( a.press ), w - 42.0f - change_w ) );
    if( text_button( b, "##pad_change_press", x + w - 14.0f - change_w, y + 22.0f, change_w, 44.0f,
                     _( "Change" ), button_look::primary ) ) {
        asked = ask::press;
    }
    y += 92.0f;
    // long press
    const bool menus = l.has_menus( sel.b );
    card( y, 78.0f, _( "LONG PRESS" ), mobile_ui::col_line, mobile_ui::col_muted );
    if( menus ) {
        b.draw->AddRectFilled( b.at( x, y ), b.at( x + w, y + 78.0f ), IM_COL32( 255, 255, 255, 8 ),
                               14.0f * b.c );
        b.text( face::body, 13.0f, x + 14.0f, y + 30.0f, mobile_ui::col_muted,
                _( "Not available: holding this button opens a menu.  Set both menus below to None to use a long press." ),
                w - 28.0f );
    } else {
        b.text( face::bold, 17.0f, x + 14.0f, y + 36.0f, mobile_ui::col_fg,
                b.fit( face::bold, 17.0f, action_name( a.long_press ), w - 42.0f - change_w ) );
        if( text_button( b, "##pad_change_long", x + w - 14.0f - change_w, y + 22.0f, change_w, 44.0f,
                         _( "Change" ) ) ) {
            asked = ask::long_press;
        }
    }
    y += 92.0f;
    // menus
    card( y, 194.0f, _( "RADIAL MENUS WHILE HELD" ), IM_COL32( 240, 145, 63, 100 ),
          mobile_ui::col_accent );
    for( int stick = 0; stick < 2; ++stick ) {
        const float row_y = y + ( stick == 0 ? 34.0f : 90.0f );
        b.text( face::bold, 15.0f, x + 14.0f, row_y + 14.0f, mobile_ui::col_fg,
                stick == 0 ? _( "Left stick" ) : _( "Right stick" ) );
        const mobile_pad::menu *const m = l.menu_on( sel.b, stick );
        drop_box( b, stick == 0 ? drop::menu_left : drop::menu_right, x + 14.0f + 96.0f, row_y,
                  w - 28.0f - 96.0f, m != nullptr ? m->name : std::string( _( "None" ) ), m == nullptr );
    }
    if( text_button( b, "##pad_edit_menus", x + 14.0f, y + 146.0f, w - 28.0f, 40.0f,
                     _( "Edit menus or make a new one" ), button_look::quiet ) ) {
        asked = ask::menus;
    }
    y += 208.0f;
    if( !is_dpad( sel.b ) ) {
        b.text( face::body, 13.0f, x, y, mobile_ui::col_muted,
                _( "Hold the button and its menus come on screen; push a stick to pick.  The same menu can be on several buttons." ),
                w );
    }
}

void pad_window::draw_other_panel( const board &b, const control &sel, const float x, float y,
                                   const float w )
{
    const auto card = [&]( const float cy, const float h, const std::string & label ) {
        b.box( x, cy, w, h, 0, mobile_ui::col_line, 14.0f );
        b.text( face::mono, 11.0f, x + 14.0f, cy + 12.0f, mobile_ui::col_muted, label );
    };
    const auto note = [&]( const float cy, const std::string & text ) {
        const float h = b.measure( face::body, 14.0f, text, w - 28.0f ).y + 28.0f;
        b.box( x, cy, w, h, 0, mobile_ui::col_line, 14.0f );
        b.text( face::body, 14.0f, x + 14.0f, cy + 14.0f, mobile_ui::col_text_soft, text, w - 28.0f );
        return h;
    };
    if( sel.stick >= 0 ) {
        // the ring upstream gives this stick while the left trigger is held
        y += note( y, sel.stick == 0 ?
                   _( "On this screen the left stick only works with L2 held: it then picks from this screen's ring." )
                   :
                   _( "On this screen the right stick moves the selection.  With L2 held it picks from this screen's second ring." ) )
             + 14.0f;
        const int first = sel.stick == 0 ? JOY_L_RADIAL_N : JOY_R_RADIAL_N;
        std::string ring;
        for( int i = 0; i < mobile_pad::num_slots; ++i ) {
            const std::string id = screen.action_on( first + i );
            if( !id.empty() ) {
                ring += string_format( "%s: %s\n", _( slot_names[static_cast<size_t>( i )] ),
                                       screen.name_of( id ) );
            }
        }
        if( ring.empty() ) {
            ring = _( "The ring is empty on this screen." );
        } else {
            ring.pop_back();
        }
        card( y, b.measure( face::body, 14.0f, ring, w - 28.0f ).y + 46.0f, _( "RING WITH L2 HELD" ) );
        b.text( face::body, 14.0f, x + 14.0f, y + 32.0f, mobile_ui::col_fg, ring, w - 28.0f );
        return;
    }
    if( sel.b == mobile_pad::button::l2 ) {
        note( y, _( "On this screen L2 is held together with another button for that button's second action, or with a stick for a ring.  Radial menus of your own and long presses are for the play screen." ) );
        return;
    }
    const float change_w = 96.0f;
    const std::string fixed = fixed_in_menus( sel.b );
    for( int with_l2 = 0; with_l2 < 2; ++with_l2 ) {
        const std::string bound = screen.action_on( joy_code( sel.b, with_l2 == 1 ) );
        card( y, 78.0f, with_l2 == 1 ? _( "WITH L2 HELD" ) : _( "PRESS" ) );
        const bool muted = bound.empty();
        b.text( face::bold, 17.0f, x + 14.0f, y + 36.0f, muted ? mobile_ui::col_muted : mobile_ui::col_fg,
                b.fit( face::bold, 17.0f, !bound.empty() ? screen.name_of( bound ) :
                       with_l2 == 0 && !fixed.empty() ? fixed : std::string( _( "Nothing" ) ),
                       w - 42.0f - change_w ) );
        if( text_button( b, string_format( "##pad_change_other%d", with_l2 ), x + w - 14.0f - change_w,
                         y + 22.0f, change_w, 44.0f, _( "Change" ),
                         with_l2 == 0 ? button_look::primary : button_look::plain ) ) {
            asked = with_l2 == 1 ? ask::with_l2 : ask::press;
        }
        y += 92.0f;
    }
    if( !fixed.empty() ) {
        y += note( y, string_format(
                       _( "In menus and lists this button always does \"%s\", whatever is set here.  What is set here counts where the screen is not a menu." ),
                       fixed ) ) + 14.0f;
    }
    b.text( face::body, 13.0f, x, y, mobile_ui::col_muted,
            _( "Radial menus of your own and long presses are for the play screen.  Here a button has one action, and a second one with L2 held." ),
            w );
}

} // namespace

void show()
{
    pad_window window;
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    ui_manager::redraw();
    while( !window.done ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            break;
        }
        const pad_window::ask asked = window.asked;
        window.asked = pad_window::ask::none;
        if( asked == pad_window::ask::none ) {
            continue;
        }
        mobile_pad::layout &l = mobile_pad::current();
        const control &sel = controls[static_cast<size_t>( window.selected )];
        const bool play = window.category.empty();
        // the drawn glyphs have no letter: the d-pad, Select and Start
        const std::string badge = sel.glyph[0] != '\0' ? sel.glyph :
                                  sel.b == mobile_pad::button::select ? "SEL" :
                                  sel.b == mobile_pad::button::start ? "ST" : "D";
        switch( asked ) {
            case pad_window::ask::press:
            case pad_window::ask::long_press:
            case pad_window::ask::with_l2: {
                if( sel.stick >= 0 ) {
                    break;
                }
                if( play ) {
                    const bool press = asked == pad_window::ask::press;
                    mobile_pad::assignment &a = l.of( sel.b );
                    const std::string title = string_format( press ? _( "%s, press" ) : _( "%s, long press" ),
                                              _( sel.name ) );
                    const std::optional<std::string> chosen = pick_action( play_pick( title, badge,
                            press ? a.press : a.long_press,
                            string_format( press ? _( "on %s" ) : _( "on %s, long press" ), short_name( sel.b ) ) ) );
                    if( chosen ) {
                        ( press ? a.press : a.long_press ) = *chosen;
                        mobile_pad::save();
                    }
                    break;
                }
                // another screen: one of its own actions, in the keybindings
                const bool with_l2 = asked == pad_window::ask::with_l2;
                const int code = joy_code( sel.b, with_l2 );
                if( code < 0 ) {
                    break;
                }
                pick_spec spec;
                spec.title = string_format( with_l2 ? _( "%s with L2" ) : _( "%s, press" ), _( sel.name ) );
                spec.badge = badge;
                spec.now = window.screen.action_on( code );
                spec.scope = context_name( window.category );
                pick_group all;
                all.name = _( "This screen" );
                all.note = _( "What this screen can do" );
                std::vector<binding> sorted = window.screen.actions;
                std::sort( sorted.begin(), sorted.end(), []( const binding & x, const binding & y ) {
                    return localized_compare( x.name, y.name );
                } );
                for( const binding &bound : sorted ) {
                    all.ids.push_back( bound.action );
                    spec.names[bound.action] = bound.name;
                    for( const int other : bound.buttons ) {
                        if( other != code ) {
                            spec.places[bound.action].push_back( string_format( _( "on %s" ),
                                                                 button_name( other ) ) );
                        }
                    }
                }
                spec.groups.push_back( all );
                const std::optional<std::string> chosen = pick_action( spec );
                if( chosen ) {
                    bind_code( window.category, code, *chosen );
                    window.screen = load_screen( window.category );
                }
                break;
            }
            case pad_window::ask::menus: {
                std::string start;
                if( sel.stick < 0 ) {
                    const mobile_pad::assignment &a = l.of( sel.b );
                    start = a.menu_left.empty() ? a.menu_right : a.menu_left;
                }
                show_menus( start );
                break;
            }
            case pad_window::ask::screen: {
                const std::vector<std::string> others = other_screens();
                mobile_ui::sheet_spec sheet;
                sheet.title = _( "Which screen?" );
                sheet.description =
                    _( "The play screen has the layout with long presses and radial menus.  Every other screen has its own plain buttons." );
                sheet.rows.push_back( { "", _( "Play screen" ), "" } );
                for( const std::string &other : others ) {
                    sheet.rows.push_back( { "", context_name( other ), "" } );
                }
                const int row = mobile_ui::show_sheet( sheet );
                if( row == 0 ) {
                    window.category.clear();
                } else if( row > 0 && static_cast<size_t>( row ) <= others.size() ) {
                    window.category = others[static_cast<size_t>( row ) - 1];
                    window.screen = load_screen( window.category );
                }
                break;
            }
            case pad_window::ask::clear:
                if( sel.stick >= 0 ) {
                    break;
                }
                if( play ) {
                    l.of( sel.b ) = mobile_pad::assignment();
                    mobile_pad::save();
                } else {
                    bind_code( window.category, joy_code( sel.b, false ), "" );
                    if( joy_code( sel.b, true ) != joy_code( sel.b, false ) ) {
                        bind_code( window.category, joy_code( sel.b, true ), "" );
                    }
                    window.screen = load_screen( window.category );
                }
                break;
            case pad_window::ask::defaults:
                if( play ) {
                    if( query_yn(
                            _( "Put every button, both sticks and every menu back to the default layout?  Menus you made are lost." ) ) ) {
                        l = mobile_pad::default_layout();
                        mobile_pad::save();
                    }
                } else if( query_yn( _( "Put every controller button of \"%s\" back to its default?" ),
                                     context_name( window.category ) ) ) {
                    // defaults that moved between actions do not fight: clear all, then set
                    const std::vector<binding> all = bindings();
                    for( const binding &bound : all ) {
                        if( bound.category == window.category && bound.buttons != bound.defaults ) {
                            set_buttons( window.category, bound.action, {} );
                        }
                    }
                    for( const binding &bound : all ) {
                        if( bound.category == window.category && bound.buttons != bound.defaults ) {
                            set_buttons( window.category, bound.action, bound.defaults );
                        }
                    }
                    save();
                    window.screen = load_screen( window.category );
                }
                break;
            case pad_window::ask::none:
                break;
        }
    }
}

// One ring of a held button: the menu's eight slots round its name.
static void draw_one_ring( const board &b, const mobile_pad::menu &m, const int pointed,
                           const bool with_dot, const float dot_x, const float dot_y, const std::string &caption )
{
    const float mid_x = 640.0f;
    const float mid_y = 380.0f;
    b.draw->AddCircleFilled( b.at( mid_x, mid_y ), 210.0f * b.c, IM_COL32( 14, 17, 13, 235 ), 96 );
    b.draw->AddCircle( b.at( mid_x, mid_y ), 210.0f * b.c, mobile_ui::col_line, 96,
                       std::max( 1.0f, 2.0f * b.c ) );
    b.draw->AddCircleFilled( b.at( mid_x, mid_y ), 80.0f * b.c, col_chip, 64 );
    b.draw->AddCircle( b.at( mid_x, mid_y ), 80.0f * b.c, mobile_ui::col_line, 64,
                       std::max( 1.0f, b.c ) );
    b.text_centered( face::display, 22.0f, mid_x - 72.0f, mid_y - 24.0f, 144.0f, 28.0f,
                     mobile_ui::col_fg, m.name );
    b.text_centered( face::body, 12.0f, mid_x - 76.0f, mid_y + 6.0f, 152.0f, 18.0f,
                     mobile_ui::col_muted, caption );
    for( int i = 0; i < mobile_pad::num_slots; ++i ) {
        const float turn = ( static_cast<float>( i ) * 45.0f - 90.0f ) * 3.14159265f / 180.0f;
        const bool on = i == pointed;
        const float w = on ? 180.0f : 160.0f;
        const float h = on ? 76.0f : 64.0f;
        const float x = mid_x + std::cos( turn ) * 285.0f - w * 0.5f;
        const float y = mid_y + std::sin( turn ) * 238.0f - h * 0.5f;
        const std::string &id = m.slots[static_cast<size_t>( i )];
        if( id.empty() ) {
            b.box( x, y, w, h, on ? mobile_ui::col_button_down : IM_COL32( 14, 17, 13, 200 ),
                   mobile_ui::col_line, 14.0f );
            b.text_centered( face::body, 15.0f, x + 6.0f, y, w - 12.0f, h, mobile_ui::col_muted,
                             _( "empty" ) );
        } else {
            b.box( x, y, w, h, on ? mobile_ui::col_accent : col_chip, on ? 0 : mobile_ui::col_line,
                   on ? 16.0f : 14.0f );
            b.text_centered_2( face::bold, on ? 17.0f : 15.0f, x + 8.0f, y, w - 16.0f, h,
                               on ? mobile_ui::col_on_accent : mobile_ui::col_fg, action_name( id ) );
        }
    }
    if( with_dot ) {
        const ImVec2 dot = b.at( mid_x + dot_x * 120.0f, mid_y + dot_y * 120.0f );
        b.draw->AddCircleFilled( dot, 15.0f * b.c, mobile_ui::col_accent, 32 );
        b.draw->AddCircle( dot, 15.0f * b.c, mobile_ui::col_on_accent, 32, std::max( 1.0f, 3.0f * b.c ) );
    }
}

// The rings in play: on screen from the moment a button with menus counts as
// held.  Both of its menus show side by side until a stick is pushed; then
// that stick's ring has the screen.
void draw_ring()
{
    const mobile_pad::machine &pad = mobile_pad::state();
    if( !pad.showing() || ImGui::GetCurrentContext() == nullptr || !mobile_pad::enabled() ) {
        return;
    }
    const mobile_pad::layout &l = mobile_pad::current();
    const std::array<const mobile_pad::menu *, 2> menus = { {
            l.menu_on( pad.held_button(), 0 ), l.menu_on( pad.held_button(), 1 )
        }
    };
    if( menus[0] == nullptr && menus[1] == nullptr ) {
        return;
    }
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    board b;
    b.u = mobile_ui::unit();
    b.c = std::min( viewport->Size.x / 1280.0f, viewport->Size.y / 800.0f );
    b.origin = ImVec2( viewport->Pos.x + ( viewport->Size.x - 1280.0f * b.c ) * 0.5f,
                       viewport->Pos.y + ( viewport->Size.y - 800.0f * b.c ) * 0.5f );
    b.draw = ImGui::GetForegroundDrawList();
    b.draw->AddRectFilled( viewport->Pos, ImVec2( viewport->Pos.x + viewport->Size.x,
                           viewport->Pos.y + viewport->Size.y ), IM_COL32( 0, 0, 0, 115 ) );
    const std::string holder = short_name( pad.held_button() );
    const int stick = pad.ring_stick_index();
    const int pointed = pad.ring_slot();
    std::vector<std::string> pills;
    if( stick >= 0 && menus[static_cast<size_t>( stick )] != nullptr ) {
        const mobile_pad::menu &m = *menus[static_cast<size_t>( stick )];
        draw_one_ring( b, m, pointed, true, pad.stick_x( stick ), pad.stick_y( stick ),
                       _( "flick or release to run" ) );
        pills.push_back( string_format( stick == 0 ? _( "%s held · left stick picks" ) :
                                        _( "%s held · right stick picks" ), holder ) );
        if( pointed >= 0 ) {
            const std::string &id = m.slots[static_cast<size_t>( pointed )];
            pills.push_back( string_format( _( "Let the stick snap back, or release %1$s: %2$s" ), holder,
                                            id.empty() ? std::string( _( "nothing" ) ) : action_name( id ) ) );
        }
        pills.emplace_back( _( "Ease the stick back slowly, or B: cancel" ) );
    } else {
        // nothing pushed yet: what each stick would open
        const bool both = menus[0] != nullptr && menus[1] != nullptr;
        for( int s = 0; s < 2; ++s ) {
            if( menus[static_cast<size_t>( s )] == nullptr ) {
                continue;
            }
            board ring = b;
            if( both ) {
                // smaller, the left stick's on the left
                ring.c = b.c * 0.7f;
                const ImVec2 mid = b.at( s == 0 ? 325.0f : 955.0f, 380.0f );
                ring.origin = ImVec2( mid.x - 640.0f * ring.c, mid.y - 380.0f * ring.c );
            }
            draw_one_ring( ring, *menus[static_cast<size_t>( s )], -1, false, 0.0f, 0.0f,
                           s == 0 ? _( "push the left stick" ) : _( "push the right stick" ) );
        }
        pills.push_back( string_format( _( "%s held · push a stick to pick" ), holder ) );
        pills.push_back( string_format( _( "Release %s or B: cancel" ), holder ) );
    }
    float total = -10.0f;
    for( const std::string &pill : pills ) {
        total += b.measure( face::body, 15.0f, pill ).x + 32.0f + 10.0f;
    }
    float x = ( 1280.0f - total ) * 0.5f;
    for( const std::string &pill : pills ) {
        const float w = b.measure( face::body, 15.0f, pill ).x + 32.0f;
        b.box( x, 728.0f, w, 42.0f, IM_COL32( 14, 17, 13, 235 ), mobile_ui::col_line, 21.0f );
        b.text( face::body, 15.0f, x + 16.0f, 738.0f, mobile_ui::col_fg, pill );
        x += w + 10.0f;
    }
}

#else // !TILES

void show() {}
void draw_ring() {}

#endif // TILES

} // namespace mobile_controls
