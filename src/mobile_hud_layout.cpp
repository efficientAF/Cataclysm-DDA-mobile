#include "mobile_hud_layout.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>

#include "cata_path.h"
#include "cata_utility.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "path_info.h"
#include "translations.h"

namespace mobile_hud
{

static constexpr std::array<piece_id, num_pieces> all_pieces = { {
        piece_id::status, piece_id::location, piece_id::log, piece_id::body, piece_id::wield,
        piece_id::quick, piece_id::zoom, piece_id::recenter, piece_id::joy, piece_id::pan
    }
};
static constexpr std::array<drawer_id, num_drawers> all_drawers = { { drawer_id::info, drawer_id::log, drawer_id::actions, drawer_id::nearby } };

const char *json_name( const piece_id id )
{
    switch( id ) {
        case piece_id::status:
            return "status";
        case piece_id::location:
            return "location";
        case piece_id::log:
            return "log";
        case piece_id::body:
            return "body";
        case piece_id::wield:
            return "wield";
        case piece_id::quick:
            return "quick";
        case piece_id::zoom:
            return "zoom";
        case piece_id::recenter:
            return "recenter";
        case piece_id::joy:
            return "joy";
        case piece_id::pan:
            return "pan";
        case piece_id::num:
            break;
    }
    return "";
}

const char *json_name( const drawer_id id )
{
    return id == drawer_id::info ? "info" : id == drawer_id::log ? "logd" :
           id == drawer_id::actions ? "actions" : "nearby";
}

std::string display_name( const piece_id id )
{
    switch( id ) {
        case piece_id::status:
            return _( "Status icons" );
        case piece_id::location:
            return _( "Location & time" );
        case piece_id::log:
            return _( "Message log" );
        case piece_id::body:
            return _( "Body" );
        case piece_id::wield:
            return _( "Wielded" );
        case piece_id::quick:
            return _( "Quick actions" );
        case piece_id::zoom:
            return _( "Zoom" );
        case piece_id::recenter:
            return _( "Recenter button" );
        case piece_id::joy:
            return _( "Movement pad" );
        case piece_id::pan:
            return _( "Pan" );
        case piece_id::num:
            break;
    }
    return std::string();
}

std::string display_name( const drawer_id id )
{
    return id == drawer_id::info ? _( "Info drawer" ) : id == drawer_id::log ? _( "Log drawer" ) :
           id == drawer_id::actions ? _( "Actions drawer" ) : _( "Nearby drawer" );
}

insets canvas_insets( const bool landscape )
{
    return landscape ? insets{ 48.0f, 0.0f, 48.0f, 18.0f } :
           insets{ 0.0f, 50.0f, 0.0f, 26.0f };
}

static bool pins_left( const anchor a )
{
    return a == anchor::top_left || a == anchor::bottom_left;
}

static bool pins_right( const anchor a )
{
    return a == anchor::top_right || a == anchor::bottom_right;
}

static bool pins_top( const anchor a )
{
    return a == anchor::top_left || a == anchor::top_center || a == anchor::top_right;
}

rect place( const piece &p, const float w, const float h, const rect &screen, const insets &safe,
            const float unit, const bool landscape )
{
    const insets canvas = canvas_insets( landscape );
    const float left = screen.x + safe.left;
    const float right = screen.x + screen.w - safe.right;
    const float top = screen.y + safe.top;
    const float bottom = screen.y + screen.h - safe.bottom;
    rect r;
    r.w = w;
    r.h = h;
    if( pins_left( p.pin ) ) {
        r.x = left + std::max( 0.0f, p.x - canvas.left ) * unit;
    } else if( pins_right( p.pin ) ) {
        r.x = right - std::max( 0.0f, p.x - canvas.right ) * unit - w;
    } else {
        r.x = ( left + right ) * 0.5f + p.x * unit - w * 0.5f;
    }
    if( pins_top( p.pin ) ) {
        r.y = top + std::max( 0.0f, p.y - canvas.top ) * unit;
    } else {
        r.y = bottom - std::max( 0.0f, p.y - canvas.bottom ) * unit - h;
    }
    // never off the screen, whatever the layout says
    r.x = std::max( left, std::min( r.x, right - w ) );
    r.y = std::max( top, std::min( r.y, bottom - h ) );
    return r;
}

void pin_to( piece &p, const rect &where, const rect &screen, const insets &safe, const float unit,
             const bool landscape )
{
    if( unit <= 0.0f ) {
        return;
    }
    const insets canvas = canvas_insets( landscape );
    const float left = screen.x + safe.left;
    const float right = screen.x + screen.w - safe.right;
    const float top = screen.y + safe.top;
    const float bottom = screen.y + screen.h - safe.bottom;
    const float cx = where.x + where.w * 0.5f;
    const float cy = where.y + where.h * 0.5f;
    const float third = ( right - left ) / 3.0f;
    const bool is_top = cy < ( top + bottom ) * 0.5f;
    if( cx < left + third ) {
        p.pin = is_top ? anchor::top_left : anchor::bottom_left;
        p.x = std::round( ( where.x - left ) / unit + canvas.left );
    } else if( cx > right - third ) {
        p.pin = is_top ? anchor::top_right : anchor::bottom_right;
        p.x = std::round( ( right - where.x - where.w ) / unit + canvas.right );
    } else {
        p.pin = is_top ? anchor::top_center : anchor::bottom_center;
        p.x = std::round( ( cx - ( left + right ) * 0.5f ) / unit );
    }
    if( is_top ) {
        p.y = std::round( ( where.y - top ) / unit + canvas.top );
    } else {
        p.y = std::round( ( bottom - where.y - where.h ) / unit + canvas.bottom );
    }
}

rect handle_rect( const drawer &d, const rect &screen, const insets &safe, const float unit )
{
    const float thick = 27.0f * unit;
    const float length = 66.0f * unit;
    const float left = screen.x + safe.left;
    const float right = screen.x + screen.w - safe.right;
    const float top = screen.y + safe.top;
    const float bottom = screen.y + screen.h - safe.bottom;
    const bool side = d.edge == screen_edge::left || d.edge == screen_edge::right;
    const float off = std::clamp( d.off, 0.1f, 0.9f );
    if( side ) {
        const float along = bottom - top - 16.0f * unit;
        const float cy = top + 8.0f * unit + off * along;
        const float y = std::clamp( cy - length * 0.5f, top, std::max( top, bottom - length ) );
        return { d.edge == screen_edge::left ? left : right - thick, y, thick, length };
    }
    const float along = right - left - 16.0f * unit;
    const float cx = left + 8.0f * unit + off * along;
    const float x = std::clamp( cx - length * 0.5f, left, std::max( left, right - length ) );
    return { x, d.edge == screen_edge::top ? top : bottom - thick, length, thick };
}

void move_handle( drawer &d, const float px, const float py, const rect &screen,
                  const insets &safe, const float unit )
{
    const float left = screen.x + safe.left;
    const float right = screen.x + screen.w - safe.right;
    const float top = screen.y + safe.top;
    const float bottom = screen.y + screen.h - safe.bottom;
    const std::array<std::pair<screen_edge, float>, 4> distance = { {
            { screen_edge::left, px - left }, { screen_edge::right, right - px },
            { screen_edge::top, py - top }, { screen_edge::bottom, bottom - py }
        }
    };
    const auto nearest = std::min_element( distance.begin(), distance.end(),
    []( const std::pair<screen_edge, float> &a, const std::pair<screen_edge, float> &b ) {
        return a.second < b.second;
    } );
    d.edge = nearest->first;
    const bool side = d.edge == screen_edge::left || d.edge == screen_edge::right;
    const float along = ( side ? bottom - top : right - left ) - 16.0f * unit;
    const float pos = ( side ? py - top : px - left ) - 8.0f * unit;
    const float off = along > 0.0f ? pos / along : 0.5f;
    d.off = std::clamp( std::round( off * 100.0f ) / 100.0f, 0.1f, 0.9f );
}

// --- The canvas's Balanced preset ---

static piece make_piece( const bool visible, const anchor pin, const float x, const float y )
{
    piece p;
    p.visible = visible;
    p.pin = pin;
    p.x = x;
    p.y = y;
    return p;
}

static preset make_balanced()
{
    preset b;
    b.id = "balanced";
    b.name = "Balanced";

    layout &p = b.portrait;
    p[piece_id::location] = make_piece( true, anchor::top_left, 12, 58 );
    p[piece_id::location].rows = 2;
    p[piece_id::location].width = 236;
    p[piece_id::status] = make_piece( true, anchor::top_left, 12, 112 );
    p[piece_id::status].size = 34;
    p[piece_id::status].count = 6;
    p[piece_id::body] = make_piece( true, anchor::top_right, 12, 58 );
    p[piece_id::body].size = 70;
    p[piece_id::wield] = make_piece( true, anchor::bottom_left, 12, 172 );
    p[piece_id::log] = make_piece( true, anchor::bottom_left, 12, 100 );
    p[piece_id::log].rows = 3;
    p[piece_id::log].width = 366;
    p[piece_id::quick] = make_piece( true, anchor::bottom_center, 0, 34 );
    p[piece_id::quick].count = 6;
    p[piece_id::zoom] = make_piece( true, anchor::top_right, 12, 150 );
    p[piece_id::zoom].size = 56;
    p[piece_id::pan] = make_piece( true, anchor::top_right, 12, 214 );
    p[piece_id::pan].size = 56;
    p[piece_id::recenter] = make_piece( true, anchor::top_right, 12, 278 );
    p[piece_id::joy] = make_piece( true, anchor::bottom_right, 14, 174 );
    p[piece_id::joy].size = 140;
    p[drawer_id::info] = { true, screen_edge::right, 0.42f, 0.8f };
    p[drawer_id::log] = { true, screen_edge::left, 0.5f, 0.8f };
    p[drawer_id::actions] = { true, screen_edge::right, 0.62f, 0.8f };
    p[drawer_id::nearby] = { true, screen_edge::left, 0.68f, 0.8f };

    layout &l = b.landscape;
    l[piece_id::location] = make_piece( true, anchor::top_left, 58, 12 );
    l[piece_id::location].rows = 2;
    l[piece_id::location].width = 236;
    l[piece_id::status] = make_piece( true, anchor::top_left, 58, 66 );
    l[piece_id::status].size = 32;
    l[piece_id::status].count = 8;
    l[piece_id::body] = make_piece( true, anchor::top_right, 58, 12 );
    l[piece_id::body].size = 64;
    l[piece_id::wield] = make_piece( true, anchor::top_right, 58, 90 );
    l[piece_id::log] = make_piece( true, anchor::bottom_left, 58, 20 );
    l[piece_id::log].rows = 3;
    l[piece_id::log].width = 320;
    l[piece_id::quick] = make_piece( true, anchor::bottom_right, 58, 20 );
    l[piece_id::quick].column = true;
    l[piece_id::quick].count = 4;
    l[piece_id::zoom] = make_piece( true, anchor::top_right, 126, 12 );
    l[piece_id::zoom].size = 56;
    l[piece_id::pan] = make_piece( true, anchor::top_right, 190, 12 );
    l[piece_id::pan].size = 56;
    l[piece_id::recenter] = make_piece( true, anchor::top_center, 0, 40 );
    l[piece_id::joy] = make_piece( true, anchor::bottom_left, 58, 100 );
    l[piece_id::joy].size = 130;
    l[drawer_id::info] = { true, screen_edge::top, 0.5f, 0.72f };
    l[drawer_id::log] = { true, screen_edge::left, 0.42f, 0.42f };
    l[drawer_id::actions] = { true, screen_edge::right, 0.5f, 0.5f };
    l[drawer_id::nearby] = { true, screen_edge::left, 0.76f, 0.42f };
    return b;
}

const preset &fallback_preset()
{
    static const preset balanced = make_balanced();
    return balanced;
}

// --- JSON ---

static bool read_flag( const JsonObject &jo, const std::string &name, const bool fallback )
{
    if( !jo.has_member( name ) ) {
        return fallback;
    }
    const JsonValue value = jo.get_member( name );
    if( value.test_bool() ) {
        return value.get_bool();
    }
    // the canvas writes 0 and 1
    return value.get_float() != 0.0;
}

static anchor read_anchor( const std::string &name, const anchor fallback )
{
    static const std::array<std::pair<const char *, anchor>, 6> names = { {
            { "tl", anchor::top_left }, { "tc", anchor::top_center }, { "tr", anchor::top_right },
            { "bl", anchor::bottom_left }, { "bc", anchor::bottom_center }, { "br", anchor::bottom_right }
        }
    };
    for( const std::pair<const char *, anchor> &entry : names ) {
        if( name == entry.first ) {
            return entry.second;
        }
    }
    return fallback;
}

static const char *anchor_name( const anchor a )
{
    switch( a ) {
        case anchor::top_left:
            return "tl";
        case anchor::top_center:
            return "tc";
        case anchor::top_right:
            return "tr";
        case anchor::bottom_left:
            return "bl";
        case anchor::bottom_center:
            return "bc";
        case anchor::bottom_right:
            return "br";
    }
    return "tl";
}

static const char *edge_name( const screen_edge e )
{
    switch( e ) {
        case screen_edge::left:
            return "left";
        case screen_edge::right:
            return "right";
        case screen_edge::top:
            return "top";
        case screen_edge::bottom:
            return "bottom";
    }
    return "right";
}

static const char *mode_name( const pad_mode m )
{
    switch( m ) {
        case pad_mode::floating:
            return "floating";
        case pad_mode::toggle:
            return "toggle";
        case pad_mode::always:
            return "always";
    }
    return "toggle";
}

static float read_number( const JsonObject &jo, const std::string &name, const float fallback )
{
    return jo.has_member( name ) ? static_cast<float>( jo.get_float( name ) ) : fallback;
}

static void read_piece( const JsonObject &jo, const piece_id id, piece &p )
{
    jo.allow_omitted_members();
    p.visible = read_flag( jo, jo.has_member( "v" ) ? "v" : "visible", p.visible );
    p.pin = read_anchor( jo.get_string( jo.has_member( "a" ) ? "a" : "anchor", "" ), p.pin );
    p.x = read_number( jo, "x", p.x );
    p.y = read_number( jo, "y", p.y );
    p.rows = std::clamp( static_cast<int>( read_number( jo, "rows", static_cast<float>( p.rows ) ) ),
                         1, id == piece_id::location ? 3 : 10 );
    p.width = std::clamp( read_number( jo, "width", p.width ), 120.0f, 800.0f );
    p.size = std::clamp( read_number( jo, "size", p.size ), 20.0f, 240.0f );
    if( id == piece_id::zoom || id == piece_id::pan ) {
        p.size = std::clamp( p.size, dial_size_min, dial_size_max );
    }
    if( id == piece_id::zoom ) {
        p.slider = jo.get_string( "style", p.slider ? "slider" : "dial" ) == "slider";
        if( p.slider ) {
            p.width = std::clamp( p.width, slider_length_min, slider_length_max );
        }
    }
    if( jo.has_member( "dir" ) ) {
        p.column = jo.get_string( "dir" ) == "col";
    }
    // the canvas calls it "max" for the status icons and "count" for the buttons
    const std::string count_name = jo.has_member( "max" ) ? "max" : "count";
    p.count = std::clamp( static_cast<int>( read_number( jo, count_name,
                                            static_cast<float>( p.count ) ) ), 1, 12 );
    if( jo.has_member( "mode" ) ) {
        const std::string mode = jo.get_string( "mode" );
        p.mode = mode == "floating" ? pad_mode::floating : mode == "always" ? pad_mode::always :
                 pad_mode::toggle;
    }
    p.opacity = std::clamp( read_number( jo, "op", p.opacity ), 0.2f, 1.0f );
}

static void read_drawer( const JsonObject &jo, drawer &d )
{
    jo.allow_omitted_members();
    d.visible = read_flag( jo, jo.has_member( "v" ) ? "v" : "visible", d.visible );
    if( jo.has_member( "edge" ) ) {
        const std::string edge = jo.get_string( "edge" );
        d.edge = edge == "left" ? screen_edge::left : edge == "top" ? screen_edge::top :
                 edge == "bottom" ? screen_edge::bottom : screen_edge::right;
    }
    d.off = std::clamp( read_number( jo, jo.has_member( "off" ) ? "off" : "offset", d.off ),
                        0.1f, 0.9f );
    d.size = std::clamp( read_number( jo, "size", d.size ), 0.3f, 0.9f );
}

static void read_layout( const JsonObject &jo, layout &l )
{
    jo.allow_omitted_members();
    for( const piece_id id : all_pieces ) {
        if( jo.has_object( json_name( id ) ) ) {
            const JsonObject piece_jo = jo.get_object( json_name( id ) );
            if( id == piece_id::zoom && !piece_jo.has_member( "size" ) ) {
                // A layout saved when zoom was a pair of buttons, usually
                // hidden: the dial keeps the place the preset gives it.
                piece_jo.allow_omitted_members();
                continue;
            }
            read_piece( piece_jo, id, l[id] );
        }
    }
    for( const drawer_id id : all_drawers ) {
        if( jo.has_object( json_name( id ) ) ) {
            read_drawer( jo.get_object( json_name( id ) ), l[id] );
        }
    }
}

preset read_preset( const JsonObject &jo, const preset &base )
{
    jo.allow_omitted_members();
    preset p = base;
    p.id = jo.get_string( "id", p.id );
    p.name = jo.get_string( "name", p.id );
    if( jo.has_object( "portrait" ) ) {
        read_layout( jo.get_object( "portrait" ), p.portrait );
    }
    if( jo.has_object( "landscape" ) ) {
        read_layout( jo.get_object( "landscape" ), p.landscape );
    }
    return p;
}

static void write_layout( JsonOut &jsout, const layout &l )
{
    jsout.start_object();
    for( const piece_id id : all_pieces ) {
        const piece &p = l[id];
        jsout.member( json_name( id ) );
        jsout.start_object();
        jsout.member( "v", p.visible ? 1 : 0 );
        jsout.member( "a", anchor_name( p.pin ) );
        jsout.member( "x", static_cast<int>( std::lround( p.x ) ) );
        jsout.member( "y", static_cast<int>( std::lround( p.y ) ) );
        switch( id ) {
            case piece_id::status:
                jsout.member( "dir", p.column ? "col" : "row" );
                jsout.member( "size", static_cast<int>( std::lround( p.size ) ) );
                jsout.member( "max", p.count );
                break;
            case piece_id::location:
            case piece_id::log:
                jsout.member( "rows", p.rows );
                jsout.member( "width", static_cast<int>( std::lround( p.width ) ) );
                break;
            case piece_id::body:
                jsout.member( "size", static_cast<int>( std::lround( p.size ) ) );
                break;
            case piece_id::quick:
                jsout.member( "dir", p.column ? "col" : "row" );
                jsout.member( "count", p.count );
                break;
            case piece_id::zoom:
                jsout.member( "style", p.slider ? "slider" : "dial" );
                jsout.member( "size", static_cast<int>( std::lround( p.size ) ) );
                if( p.slider ) {
                    jsout.member( "dir", p.column ? "col" : "row" );
                    jsout.member( "width", static_cast<int>( std::lround( p.width ) ) );
                }
                break;
            case piece_id::pan:
                jsout.member( "size", static_cast<int>( std::lround( p.size ) ) );
                break;
            case piece_id::joy:
                jsout.member( "mode", mode_name( p.mode ) );
                jsout.member( "size", static_cast<int>( std::lround( p.size ) ) );
                break;
            case piece_id::wield:
            case piece_id::recenter:
            case piece_id::num:
                break;
        }
        if( p.opacity < 1.0f ) {
            jsout.member( "op", std::round( p.opacity * 100.0f ) / 100.0f );
        }
        jsout.end_object();
    }
    for( const drawer_id id : all_drawers ) {
        const drawer &d = l[id];
        jsout.member( json_name( id ) );
        jsout.start_object();
        jsout.member( "v", d.visible ? 1 : 0 );
        jsout.member( "edge", edge_name( d.edge ) );
        jsout.member( "off", std::round( d.off * 100.0f ) / 100.0f );
        jsout.member( "size", std::round( d.size * 100.0f ) / 100.0f );
        jsout.end_object();
    }
    jsout.end_object();
}

void write_preset( JsonOut &jsout, const preset &p )
{
    jsout.start_object();
    jsout.member( "id", p.id );
    jsout.member( "name", p.name );
    jsout.member( "portrait" );
    write_layout( jsout, p.portrait );
    jsout.member( "landscape" );
    write_layout( jsout, p.landscape );
    jsout.end_object();
}

// --- Game data and the layout in use ---

static std::vector<preset> loaded_presets;
static std::optional<preset> active;

void load_preset( const JsonObject &jo, const std::string & )
{
    const preset p = read_preset( jo, fallback_preset() );
    const auto same = std::find_if( loaded_presets.begin(), loaded_presets.end(),
    [&p]( const preset & other ) {
        return other.id == p.id;
    } );
    if( same != loaded_presets.end() ) {
        *same = p;
    } else {
        loaded_presets.push_back( p );
    }
    active.reset();
}

void reset_presets()
{
    loaded_presets.clear();
    active.reset();
}

const std::vector<preset> &builtin_presets()
{
    static const std::vector<preset> fallback = { fallback_preset() };
    return loaded_presets.empty() ? fallback : loaded_presets;
}

static cata_path saved_path()
{
    return PATH_INFO::config_dir_path() / "mobile_hud.json";
}

const preset &active_preset()
{
    if( !active ) {
        // Balanced unless the player saved a layout of their own
        const std::vector<preset> &builtin = builtin_presets();
        const auto balanced = std::find_if( builtin.begin(), builtin.end(), []( const preset & p ) {
            return p.id == "balanced";
        } );
        preset chosen = balanced != builtin.end() ? *balanced : builtin.front();
        read_from_file_optional_json( saved_path(), [&chosen]( const JsonValue & jv ) {
            const JsonObject jo = jv;
            chosen = read_preset( jo, chosen );
        } );
        active = chosen;
    }
    return *active;
}

const layout &active_layout( const bool landscape )
{
    const preset &p = active_preset();
    return landscape ? p.landscape : p.portrait;
}

void set_active_preset( const preset &p )
{
    active = p;
    write_to_file( saved_path(), [&p]( std::ostream & out ) {
        JsonOut jsout( out, true );
        write_preset( jsout, p );
    }, _( "HUD layout" ) );
}

void invalidate_active_preset()
{
    active.reset();
}

} // namespace mobile_hud
