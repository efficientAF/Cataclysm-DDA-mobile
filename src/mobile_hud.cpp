#if defined(TILES)

#include "mobile_hud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "bodypart.h"
#include "cached_options.h"
#include "color.h"
#include "creature.h"
#include "cursesport.h"
#include "display.h"
#include "filesystem.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "math_defines.h"
#include "messages.h"
#include "mobile_hud_layout.h"
#include "mobile_ui.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "touch_input.h"
#include "translations.h"

static const efftype_id effect_bleed( "bleed" );

namespace mobile_hud
{

// --- Drawing kit: the canvas look on the SDL renderer ---

namespace
{
struct rgba {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
};

enum class face : int {
    body,
    bold,
    mono,
};

struct text_key {
    int face_index;
    int pixels;
    std::string text;
    bool operator<( const text_key &other ) const {
        return std::tie( face_index, pixels, text ) < std::tie( other.face_index, other.pixels,
                other.text );
    }
};

struct text_entry {
    SDL_Texture *texture = nullptr;
    float w = 0.0f;
    float h = 0.0f;
};

struct hud_button {
    SDL_Rect where;
    hit what;
};

struct strip_button {
    action_id act;
    const char *icon;
    std::string label;
};
} // namespace

// the canvas's game palette
static constexpr rgba col_panel = { 13, 16, 12, 204 };
static constexpr rgba col_panel_solid = { 20, 24, 18, 255 };
static constexpr rgba col_line = { 232, 236, 222, 36 };
static constexpr rgba col_fg = { 233, 236, 223, 255 };
static constexpr rgba col_muted = { 154, 163, 146, 255 };
static constexpr rgba col_accent = { 240, 145, 63, 255 };
static constexpr rgba col_hp_good = { 108, 191, 85, 255 };
static constexpr rgba col_hp_ok = { 216, 187, 66, 255 };
static constexpr rgba col_hp_low = { 230, 137, 45, 255 };
static constexpr rgba col_hp_bad = { 224, 73, 43, 255 };

static SDL_Renderer *sdl = nullptr;
// pixels per canvas unit for the frame being drawn
static float unit = 1.0f;

static uint64_t cache_generation = 0;
static std::map<std::pair<int, int>, TTF_Font *> font_cache;
static std::map<text_key, text_entry> text_cache;

// what can be tapped, as of the last draw
static std::vector<hud_button> buttons;
static std::array<SDL_FRect, num_pieces> piece_rects;
static std::array<SDL_FRect, num_drawers> handle_rects;

// the frame being drawn
static rect frame_screen;
// room kept free at its edges for notches, system bars and drawer handles
static insets frame_safe;
// the same without the room for handles
static insets frame_handle_safe;
static bool frame_landscape = false;
static const layout *frame_layout = nullptr;
// of the piece being drawn; see piece::opacity
static float opacity = 1.0f;

// the movement pad
static bool pad_open = false;
static SDL_FRect pad_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
// where the knob is, -1 to 1 each way
static float pad_knob_x = 0.0f;
static float pad_knob_y = 0.0f;

// the zoom and pan dials: index 0 is zoom, 1 is pan
namespace
{
struct dial_state {
    bool held = false;
    float knob_x = 0.0f;
    float knob_y = 0.0f;
    float radius = 0.0f;
};
} // namespace
static std::array<dial_state, 2> dials;
static zoom_drag zoom_drag_now;

static dial_state &dial_of( const piece_id which )
{
    return dials[which == piece_id::pan ? 1 : 0];
}

// the editor, see mobile_hud_edit.h
static bool editing = false;
static preset edit_preset;
static int edit_selection = -1;

static bool contains( const SDL_Rect &rect, const float x, const float y )
{
    return rect.w > 0 && rect.h > 0 &&
           x >= static_cast<float>( rect.x ) && x < static_cast<float>( rect.x + rect.w ) &&
           y >= static_cast<float>( rect.y ) && y < static_cast<float>( rect.y + rect.h );
}

static void flush_caches_if_renderer_changed()
{
    const uint64_t generation = renderer_resource_generation();
    if( generation == cache_generation && text_cache.size() < 400 ) {
        return;
    }
    // the textures died with the old renderer, or there are too many of them
    for( std::pair<const text_key, text_entry> &entry : text_cache ) {
        if( generation == cache_generation && entry.second.texture != nullptr ) {
            SDL_DestroyTexture( entry.second.texture );
        }
    }
    text_cache.clear();
    cache_generation = generation;
}

static TTF_Font *font_for( const face f, const int pixels )
{
    static const std::array<const char *, 3> files = {
        "Barlow-Medium.ttf", "Barlow-Bold.ttf", "JetBrainsMono-Medium.ttf"
    };
    const std::pair<int, int> key( static_cast<int>( f ), pixels );
    const auto found = font_cache.find( key );
    if( found != font_cache.end() ) {
        return found->second;
    }
    const std::string path = PATH_INFO::fontdir() + files[static_cast<size_t>( f )];
    TTF_Font *const font = file_exist( path ) ? TTF_OpenFont( path.c_str(),
                           static_cast<float>( pixels ) ) : nullptr;
    font_cache[key] = font;
    return font;
}

static const text_entry &text_texture( const face f, const float size_units,
                                       const std::string &text )
{
    const int pixels = std::max( 6, static_cast<int>( std::lround( size_units * unit ) ) );
    const text_key key{ static_cast<int>( f ), pixels, text };
    const auto found = text_cache.find( key );
    if( found != text_cache.end() ) {
        return found->second;
    }
    text_entry entry;
    TTF_Font *const font = font_for( f, pixels );
    if( font != nullptr && !text.empty() ) {
        if( SDL_Surface *const surface = TTF_RenderText_Blended( font, text.c_str(), 0,
                                         SDL_Color{ 255, 255, 255, 255 } ) ) {
            entry.texture = SDL_CreateTextureFromSurface( sdl, surface );
            entry.w = static_cast<float>( surface->w );
            entry.h = static_cast<float>( surface->h );
            SDL_DestroySurface( surface );
        }
    }
    return text_cache.emplace( key, entry ).first->second;
}

// Shortens text with an ellipsis until it fits into max_width pixels.
static std::string fit_text( const face f, const float size_units, const std::string &text,
                             const float max_width )
{
    const int pixels = std::max( 6, static_cast<int>( std::lround( size_units * unit ) ) );
    TTF_Font *const font = font_for( f, pixels );
    int w = 0;
    size_t fits = 0;
    if( font == nullptr ||
        !TTF_MeasureString( font, text.c_str(), 0, static_cast<int>( max_width ), &w, &fits ) ||
        fits >= text.size() ) {
        return text;
    }
    // leave room for the ellipsis, and do not cut a UTF-8 sequence in half
    size_t cut = fits > 2 ? fits - 2 : 0;
    while( cut > 0 && ( static_cast<unsigned char>( text[cut] ) & 0xC0 ) == 0x80 ) {
        --cut;
    }
    return text.substr( 0, cut ) + "…";
}

static float text_width( const face f, const float size_units, const std::string &text )
{
    return text_texture( f, size_units, text ).w;
}

// Draws one line of text with its top left corner at x, y.
static void draw_text( const face f, const float size_units, const std::string &text, const float x,
                       const float y, const rgba &color )
{
    const text_entry &entry = text_texture( f, size_units, text );
    if( entry.texture == nullptr ) {
        return;
    }
    SDL_SetTextureColorMod( entry.texture, color.r, color.g, color.b );
    SDL_SetTextureAlphaMod( entry.texture,
                            static_cast<Uint8>( static_cast<float>( color.a ) * opacity ) );
    const SDL_FRect dst = { std::floor( x ), std::floor( y ), entry.w, entry.h };
    SDL_RenderTexture( sdl, entry.texture, nullptr, &dst );
}

// Height of a line of text of this size, for vertical centering.
static float text_height( const face f, const float size_units )
{
    return text_texture( f, size_units, "Ag" ).h;
}

static void draw_icon( const std::string &name, const float x, const float y, const float size,
                       const rgba &color )
{
    SDL_Texture *const texture = mobile_ui::icon_texture( name );
    if( texture == nullptr ) {
        return;
    }
    SDL_SetTextureColorMod( texture, color.r, color.g, color.b );
    SDL_SetTextureAlphaMod( texture, static_cast<Uint8>( static_cast<float>( color.a ) * opacity ) );
    const SDL_FRect dst = { x, y, size, size };
    SDL_RenderTexture( sdl, texture, nullptr, &dst );
}

// Outline of a rounded rectangle, clockwise.
static std::vector<SDL_FPoint> round_outline( const SDL_FRect &rect, float radius )
{
    radius = std::max( 0.0f, std::min( radius, std::min( rect.w, rect.h ) * 0.5f ) );
    static constexpr int steps = 6;
    const std::array<SDL_FPoint, 4> centers = { {
            { rect.x + rect.w - radius, rect.y + radius },
            { rect.x + rect.w - radius, rect.y + rect.h - radius },
            { rect.x + radius, rect.y + rect.h - radius },
            { rect.x + radius, rect.y + radius }
        }
    };
    std::vector<SDL_FPoint> points;
    for( int corner = 0; corner < 4; ++corner ) {
        for( int i = 0; i <= steps; ++i ) {
            const float angle = ( static_cast<float>( corner ) - 1.0f + static_cast<float>( i ) /
                                  static_cast<float>( steps ) ) * static_cast<float>( M_PI ) * 0.5f;
            points.push_back( { centers[corner].x + radius * std::cos( angle ),
                                centers[corner].y + radius * std::sin( angle )
                              } );
        }
    }
    return points;
}

static SDL_FColor to_fcolor( const rgba &color )
{
    return { static_cast<float>( color.r ) / 255.0f, static_cast<float>( color.g ) / 255.0f,
             static_cast<float>( color.b ) / 255.0f, static_cast<float>( color.a ) / 255.0f * opacity
           };
}

// A filled rounded rectangle; radius in pixels.
static void fill_round( const SDL_FRect &rect, const float radius, const rgba &color )
{
    if( rect.w <= 0.0f || rect.h <= 0.0f || color.a == 0 ) {
        return;
    }
    const std::vector<SDL_FPoint> outline = round_outline( rect, radius );
    const SDL_FColor fc = to_fcolor( color );
    std::vector<SDL_Vertex> vertices;
    vertices.push_back( { { rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f }, fc, { 0.0f, 0.0f } } );
    for( const SDL_FPoint &p : outline ) {
        vertices.push_back( { p, fc, { 0.0f, 0.0f } } );
    }
    std::vector<int> indices;
    const int count = static_cast<int>( outline.size() );
    for( int i = 0; i < count; ++i ) {
        indices.push_back( 0 );
        indices.push_back( 1 + i );
        indices.push_back( 1 + ( i + 1 ) % count );
    }
    SDL_SetRenderDrawBlendMode( sdl, SDL_BLENDMODE_BLEND );
    SDL_RenderGeometry( sdl, nullptr, vertices.data(), static_cast<int>( vertices.size() ),
                        indices.data(), static_cast<int>( indices.size() ) );
}

// The border of a rounded rectangle, drawn inside it.
static void stroke_round( const SDL_FRect &rect, const float radius, const float width,
                          const rgba &color )
{
    if( rect.w <= 2.0f * width || rect.h <= 2.0f * width || color.a == 0 ) {
        return;
    }
    const std::vector<SDL_FPoint> outer = round_outline( rect, radius );
    const SDL_FRect inner_rect = { rect.x + width, rect.y + width, rect.w - 2.0f * width,
                                   rect.h - 2.0f * width
                                 };
    const std::vector<SDL_FPoint> inner = round_outline( inner_rect, std::max( 0.0f,
                                          radius - width ) );
    const SDL_FColor fc = to_fcolor( color );
    std::vector<SDL_Vertex> vertices;
    for( size_t i = 0; i < outer.size(); ++i ) {
        vertices.push_back( { outer[i], fc, { 0.0f, 0.0f } } );
        vertices.push_back( { inner[i], fc, { 0.0f, 0.0f } } );
    }
    std::vector<int> indices;
    const int count = static_cast<int>( outer.size() );
    for( int i = 0; i < count; ++i ) {
        const int next = ( i + 1 ) % count;
        indices.insert( indices.end(), { 2 * i, 2 * next, 2 * i + 1, 2 * i + 1, 2 * next, 2 * next + 1 } );
    }
    SDL_SetRenderDrawBlendMode( sdl, SDL_BLENDMODE_BLEND );
    SDL_RenderGeometry( sdl, nullptr, vertices.data(), static_cast<int>( vertices.size() ),
                        indices.data(), static_cast<int>( indices.size() ) );
}

// The canvas's panel background: translucent dark, hairline border, 10 unit corners.
static void draw_panel( const SDL_FRect &rect )
{
    fill_round( rect, 10.0f * unit, col_panel );
    stroke_round( rect, 10.0f * unit, std::max( 1.0f, unit ), col_line );
}

static SDL_Rect to_rect( const SDL_FRect &rect )
{
    return { static_cast<int>( rect.x ), static_cast<int>( rect.y ), static_cast<int>( rect.w ),
             static_cast<int>( rect.h )
           };
}

// Where the HUD may draw: the game view, minus notches and system bars.
static SDL_FRect hud_area( const SDL_Rect &view, const int window_w, const int window_h )
{
    SDL_Rect area = { 0, 0, window_w, window_h };
    if( view.w > 0 && view.h > 0 ) {
        area = view;
    }
    if( SDL_Window *const win = get_sdl_window() ) {
        SDL_Rect safe;
        SDL_Rect both;
        if( SDL_GetWindowSafeArea( win, &safe ) && safe.w > 0 && safe.h > 0 &&
            SDL_GetRectIntersection( &area, &safe, &both ) ) {
            area = both;
        }
    }
    return { static_cast<float>( area.x ), static_cast<float>( area.y ), static_cast<float>( area.w ),
             static_cast<float>( area.h )
           };
}

// --- Pieces, placed by the layout in use (mobile_hud_layout.h) ---

static SDL_FRect to_frect( const rect &r )
{
    return { r.x, r.y, r.w, r.h };
}

// Puts a piece of the given size where the layout wants it and remembers the
// place for the editor.
static SDL_FRect place_piece( const piece_id id, const float w, const float h )
{
    const rect r = place( ( *frame_layout )[id], w, h, frame_screen, frame_safe, unit,
                          frame_landscape );
    const SDL_FRect result = to_frect( r );
    piece_rects[static_cast<size_t>( id )] = result;
    return result;
}

static void add_button( const SDL_FRect &where, const hit_kind kind, const action_id act )
{
    if( !editing ) {
        buttons.push_back( { to_rect( where ), { kind, act } } );
    }
}

// Place on the first row, date and time on the second, weather on the third.
static void draw_location( const piece &cfg )
{
    const avatar &you = get_avatar();
    const float w = std::min( cfg.width * unit, frame_screen.w - 20.0f * unit );
    const float line = 17.0f * unit;
    const int rows = std::clamp( cfg.rows, 1, 3 );
    const SDL_FRect rect = place_piece( piece_id::location, w,
                                        static_cast<float>( rows ) * line + 12.0f * unit );
    draw_panel( rect );
    const float room = w - 20.0f * unit;
    const float x = rect.x + 10.0f * unit;
    const float pad_y = ( line - text_height( face::body, 13.0f ) ) * 0.5f;
    const std::string place_name = remove_color_tags( display::current_position_text(
                                       you.pos_abs_omt() ) );
    draw_text( face::bold, 13.0f, fit_text( face::bold, 13.0f, place_name, room ), x,
               rect.y + 6.0f * unit + pad_y, col_fg );
    if( rows >= 2 ) {
        const std::string when = string_format( "%s · %s", display::date_string(),
                                                remove_color_tags( display::time_string( you ) ) );
        draw_text( face::body, 13.0f, fit_text( face::body, 13.0f, when, room ), x,
                   rect.y + 6.0f * unit + line + pad_y, col_fg );
    }
    if( rows >= 3 ) {
        const std::string weather = string_format( "%s · %s",
                                    remove_color_tags( display::weather_text_color( you ).first ),
                                    remove_color_tags( display::get_temp( you ) ) );
        draw_text( face::body, 13.0f, fit_text( face::body, 13.0f, weather, room ), x,
                   rect.y + 6.0f * unit + 2.0f * line + pad_y, col_muted );
    }
}

namespace
{
struct condition {
    const char *icon;
    // 1 to 4
    int level;
};
} // namespace

// Index into the font palette for a curses color.
static int palette_index( const nc_color &color )
{
    int fg = static_cast<int>( cata_cursesport::colorpairs[color.to_color_pair_index()].FG );
    if( color.is_bold() ) {
        fg += 8;
    }
    return std::clamp( fg, 0, 15 );
}

// How bad the game's own color for a value says it is: 0 for fine, up to 3.
static int level_from_color( const nc_color &color )
{
    switch( palette_index( color ) ) {
        case 3:  // brown
        case 11: // yellow
        case 14: // light cyan
            return 1;
        case 9:  // light red
        case 6:  // cyan
        case 12: // light blue
        case 13: // pink
            return 2;
        case 1:  // red
        case 4:  // blue
        case 5:  // magenta
            return 3;
        default:
            return 0;
    }
}

static bool is_cold_color( const nc_color &color )
{
    const int index = palette_index( color );
    return index == 4 || index == 6 || index == 12 || index == 14;
}

// What needs attention right now, worst first.  The values and their colors
// are the ones the sidebar widgets show.
static std::vector<condition> active_conditions()
{
    const avatar &you = get_avatar();
    std::vector<condition> found;
    const auto add = [&found]( const char *icon, const int level ) {
        if( level > 0 ) {
            found.push_back( { icon, std::min( level, 4 ) } );
        }
    };
    int bleeding = 0;
    for( const bodypart_id &part : you.get_all_body_parts() ) {
        bleeding = std::max( bleeding, you.get_effect_int( effect_bleed, part ) );
    }
    add( "bleed", bleeding > 20 ? 4 : bleeding > 10 ? 3 : bleeding > 5 ? 2 : bleeding > 0 ? 1 : 0 );
    add( "pain", level_from_color( display::pain_text_color( you ).second ) );
    const std::pair<std::string, nc_color> thirst = display::thirst_text_color( you );
    add( "thirst", thirst.first.empty() ? 0 : level_from_color( thirst.second ) );
    const std::pair<std::string, nc_color> hunger = display::hunger_text_color( you );
    add( "hunger", hunger.first.empty() ? 0 : level_from_color( hunger.second ) );
    const std::pair<std::string, nc_color> sleepiness = display::sleepiness_text_color( you );
    add( "tired", sleepiness.first.empty() ? 0 : level_from_color( sleepiness.second ) );
    const std::pair<std::string, nc_color> temperature = display::temp_text_color( you );
    add( is_cold_color( temperature.second ) ? "cold" : "hot",
         level_from_color( temperature.second ) );
    add( "enc", level_from_color( display::carry_weight_text_color( you ).second ) );
    const int stamina_max = you.get_stamina_max();
    const float stamina = stamina_max > 0 ? static_cast<float>( you.get_stamina() ) /
                          static_cast<float>( stamina_max ) : 1.0f;
    add( "winded", stamina < 0.1f ? 3 : stamina < 0.25f ? 2 : stamina < 0.5f ? 1 : 0 );
    std::stable_sort( found.begin(), found.end(), []( const condition & a, const condition & b ) {
        return a.level > b.level;
    } );
    return found;
}

// One square icon per condition, in its severity color, with a pip per level.
static void draw_status( const piece &cfg )
{
    static constexpr std::array<rgba, 4> severity = { {
            { 216, 187, 66, 255 }, { 230, 137, 45, 255 }, { 224, 73, 43, 255 }, { 255, 106, 85, 255 }
        }
    };
    std::vector<condition> conditions = active_conditions();
    if( conditions.size() > static_cast<size_t>( std::max( cfg.count, 1 ) ) ) {
        conditions.resize( static_cast<size_t>( std::max( cfg.count, 1 ) ) );
    }
    if( conditions.empty() ) {
        if( editing ) {
            // something to grab while nothing is wrong
            conditions = { { "hunger", 1 }, { "thirst", 2 }, { "pain", 3 } };
        } else {
            return;
        }
    }
    const float size = cfg.size * unit;
    const float gap = 4.0f * unit;
    const float count = static_cast<float>( conditions.size() );
    const float length = count * size + ( count - 1.0f ) * gap;
    const SDL_FRect all = place_piece( piece_id::status, cfg.column ? size : length,
                                       cfg.column ? length : size );
    float x = all.x;
    float y = all.y;
    for( const condition &cond : conditions ) {
        const rgba &color = severity[static_cast<size_t>( cond.level - 1 )];
        const SDL_FRect rect = { x, y, size, size };
        fill_round( rect, 9.0f * unit, cond.level == 4 ? rgba{ 90, 12, 10, 217 } :
                    rgba{ 11, 13, 10, 214 } );
        stroke_round( rect, 9.0f * unit, 2.0f * unit, cond.level == 4 ? rgba{ 176, 32, 31, 255 } :
                      color );
        const float icon_size = size * 0.58f;
        draw_icon( cond.icon, x + ( size - icon_size ) * 0.5f, y + ( size - icon_size ) * 0.5f - unit,
                   icon_size, color );
        // one pip per level along the bottom edge
        const float pip = 3.0f * unit;
        const float pips_w = static_cast<float>( cond.level ) * pip +
                             static_cast<float>( cond.level - 1 ) * 2.0f * unit;
        float pip_x = x + ( size - pips_w ) * 0.5f;
        for( int i = 0; i < cond.level; ++i ) {
            fill_round( { pip_x, y + size - 2.0f * unit - pip - 2.0f * unit, pip, pip }, pip * 0.5f, color );
            pip_x += pip + 2.0f * unit;
        }
        if( cfg.column ) {
            y += size + gap;
        } else {
            x += size + gap;
        }
    }
    // the icons explain themselves in the Info drawer
    add_button( all, hit_kind::action, ACTION_PL_INFO );
}

static const rgba &hp_color( const avatar &you, const char *part_id )
{
    const bodypart_id part( part_id );
    const int hp_max = you.get_part_hp_max( part );
    const int percent = hp_max > 0 ? you.get_part_hp_cur( part ) * 100 / hp_max : 0;
    return percent > 80 ? col_hp_good : percent > 50 ? col_hp_ok : percent > 25 ? col_hp_low :
           col_hp_bad;
}

// A small figure colored by the health of each limb.
static void draw_body( const piece &cfg )
{
    const avatar &you = get_avatar();
    const float size = cfg.size * unit;
    // the figure is drawn on the canvas's 40 by 62 grid
    const float k = size / 62.0f;
    const SDL_FRect rect = place_piece( piece_id::body, 40.0f * k + 14.0f * unit,
                                        size + 10.0f * unit );
    draw_panel( rect );
    const float ox = rect.x + 7.0f * unit;
    const float oy = rect.y + 5.0f * unit;
    const auto part = [&]( const float x, const float y, const float w, const float h,
    const float radius, const rgba & color ) {
        fill_round( { ox + x * k, oy + y * k, w * k, h * k }, radius * k, color );
    };
    part( 14.0f, 1.0f, 12.0f, 12.0f, 6.0f, hp_color( you, "head" ) );
    part( 12.5f, 14.5f, 15.0f, 21.0f, 3.0f, hp_color( you, "torso" ) );
    part( 4.5f, 15.0f, 6.0f, 20.0f, 3.0f, hp_color( you, "arm_r" ) );
    part( 29.5f, 15.0f, 6.0f, 20.0f, 3.0f, hp_color( you, "arm_l" ) );
    part( 12.5f, 37.0f, 6.5f, 24.0f, 3.0f, hp_color( you, "leg_r" ) );
    part( 21.0f, 37.0f, 6.5f, 24.0f, 3.0f, hp_color( you, "leg_l" ) );
    // tapping the figure opens the Info drawer
    add_button( rect, hit_kind::action, ACTION_PL_INFO );
}

// What is in your hands.
static void draw_wielded()
{
    const avatar &you = get_avatar();
    const item_location weapon = you.get_wielded_item();
    const std::string name = weapon ? remove_color_tags( weapon->tname() ) : _( "Bare hands" );
    std::string detail;
    if( weapon && weapon->is_gun() ) {
        detail = string_format( n_gettext( "%d round", "%d rounds", weapon->ammo_remaining() ),
                                weapon->ammo_remaining() );
    } else {
        detail = weapon ? _( "Wielded" ) : _( "Nothing wielded" );
    }
    const float max_text = 150.0f * unit;
    const std::string shown_name = fit_text( face::bold, 13.0f, name, max_text );
    const float text_w = std::max( text_width( face::bold, 13.0f, shown_name ),
                                   text_width( face::body, 12.0f, detail ) );
    const float w = 7.0f * unit + 22.0f * unit + 8.0f * unit + text_w + 10.0f * unit;
    const SDL_FRect rect = place_piece( piece_id::wield, w, 40.0f * unit );
    draw_panel( rect );
    draw_icon( "axe", rect.x + 7.0f * unit, rect.y + 9.0f * unit, 22.0f * unit, col_accent );
    const float x = rect.x + 37.0f * unit;
    draw_text( face::bold, 13.0f, shown_name, x, rect.y + 5.0f * unit, col_fg );
    draw_text( face::body, 12.0f, detail, x, rect.y + 20.0f * unit, col_muted );
    add_button( rect, hit_kind::action, ACTION_PL_INFO );
}

// The tabs at the screen edges that open the two drawers.
static void draw_drawer_handles()
{
    for( size_t i = 0; i < num_drawers; ++i ) {
        const drawer_id id = static_cast<drawer_id>( i );
        const drawer &cfg = ( *frame_layout )[id];
        handle_rects[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if( !cfg.visible ) {
            continue;
        }
        // handles sit on the very edge of the screen, as on the canvas
        insets along = frame_handle_safe;
        switch( cfg.edge ) {
            case screen_edge::left:
                along.left = 0.0f;
                break;
            case screen_edge::right:
                along.right = 0.0f;
                break;
            case screen_edge::top:
                along.top = 0.0f;
                break;
            case screen_edge::bottom:
                along.bottom = 0.0f;
                break;
        }
        const SDL_FRect rect = to_frect( handle_rect( cfg, frame_screen, along, unit ) );
        handle_rects[i] = rect;
        const bool horizontal = cfg.edge == screen_edge::top || cfg.edge == screen_edge::bottom;
        const char *const icon = id == drawer_id::info ? "person" : id == drawer_id::log ? "globe" :
                                 id == drawer_id::actions ? "bolt" : "pickup";
        fill_round( rect, 12.0f * unit, col_panel_solid );
        stroke_round( rect, 12.0f * unit, std::max( 1.0f, unit ), col_line );
        const float icon_size = 16.0f * unit;
        if( horizontal ) {
            const float y = rect.y + ( rect.h - icon_size ) * 0.5f;
            draw_icon( "gripH", rect.x + rect.w * 0.5f - icon_size - 2.0f * unit, y, icon_size, col_muted );
            draw_icon( icon, rect.x + rect.w * 0.5f + 2.0f * unit, y, icon_size, col_fg );
        } else {
            const float x = rect.x + ( rect.w - icon_size ) * 0.5f;
            draw_icon( "gripV", x, rect.y + rect.h * 0.5f - icon_size - 2.0f * unit, icon_size, col_muted );
            draw_icon( icon, x, rect.y + rect.h * 0.5f + 2.0f * unit, icon_size, col_fg );
        }
        // a generous touch target around the small tab
        SDL_FRect target = rect;
        const float grow = 10.0f * unit;
        target.x -= grow;
        target.y -= grow;
        target.w += 2.0f * grow;
        target.h += 2.0f * grow;
        // the Log drawer's handle opens straight onto its map
        add_button( target, id == drawer_id::info ? hit_kind::action : id == drawer_id::log ?
                    hit_kind::map_drawer : id == drawer_id::actions ? hit_kind::actions_drawer :
                    hit_kind::nearby_drawer,
                    id == drawer_id::info ? ACTION_PL_INFO : id == drawer_id::log ? ACTION_MESSAGES :
                    ACTION_ACTIONMENU );
    }
}

// The last messages, newest at the bottom, older ones dimmed.
static void draw_log( const piece &cfg )
{
    const size_t rows = static_cast<size_t>( std::clamp( cfg.rows, 1, 10 ) );
    std::vector<std::pair<std::string, std::string>> messages = Messages::recent_messages( rows );
    const float w = std::min( cfg.width * unit, frame_screen.w - 20.0f * unit );
    const float line = 17.0f * unit;
    const SDL_FRect rect = place_piece( piece_id::log, w,
                                        static_cast<float>( rows ) * line + 12.0f * unit );
    draw_panel( rect );
    const float pad_y = ( line - text_height( face::body, 13.0f ) ) * 0.5f;
    // fill from the bottom, so a short log leaves the top rows empty
    float y = rect.y + 6.0f * unit + static_cast<float>( rows - std::min( rows,
              messages.size() ) ) * line;
    for( size_t i = 0; i < messages.size(); ++i ) {
        const std::string text = fit_text( face::body, 13.0f, remove_color_tags( messages[i].second ),
                                           w - 20.0f * unit );
        const bool old = i + 2 < messages.size();
        draw_text( face::body, 13.0f, text, rect.x + 10.0f * unit, y + pad_y,
                   old ? rgba{ col_fg.r, col_fg.g, col_fg.b, 158 } : col_fg );
        y += line;
    }
    // the whole history is in the Log drawer
    add_button( rect, hit_kind::action, ACTION_MESSAGES );
}

// A panel of square buttons in a row or a column.
static void draw_button_strip( const piece_id id, const bool column,
                               const std::vector<strip_button> &shown, const float button )
{
    const float gap = 6.0f * unit;
    const float pad = 4.0f * unit;
    const float count = static_cast<float>( shown.size() );
    const float length = count * button + ( count - 1.0f ) * gap + 2.0f * pad;
    const float thickness = button + 2.0f * pad;
    const SDL_FRect panel = place_piece( id, column ? thickness : length,
                                         column ? length : thickness );
    draw_panel( panel );
    float x = panel.x + pad;
    float y = panel.y + pad;
    for( const strip_button &b : shown ) {
        const SDL_FRect rect = { x, y, button, button };
        fill_round( rect, 10.0f * unit, { 255, 255, 255, 13 } );
        stroke_round( rect, 10.0f * unit, std::max( 1.0f, unit ), col_line );
        if( b.label.empty() ) {
            draw_icon( b.icon, x + ( button - 20.0f * unit ) * 0.5f, y + ( button - 20.0f * unit ) * 0.5f,
                       20.0f * unit, col_fg );
        } else {
            draw_icon( b.icon, x + ( button - 20.0f * unit ) * 0.5f, y + 7.0f * unit, 20.0f * unit, col_fg );
            const std::string label = fit_text( face::body, 9.0f, b.label, button - 2.0f * unit );
            draw_text( face::body, 9.0f, label, x + ( button - text_width( face::body, 9.0f, label ) ) * 0.5f,
                       y + 30.0f * unit, col_muted );
        }
        add_button( rect, hit_kind::action, b.act );
        if( column ) {
            y += button + gap;
        } else {
            x += button + gap;
        }
    }
}

namespace
{
struct quick_entry {
    const char *id;
    action_id act;
    const char *icon;
    translation label;
};
} // namespace

// What the quick actions bar can hold, by the names of quick_item_ids().
static const std::vector<quick_entry> &quick_entries()
{
    static const std::vector<quick_entry> all = {
        { "examine", ACTION_EXAMINE, "examine", to_translation( "mobile HUD button", "Examine" ) },
        { "pickup", ACTION_PICKUP, "pickup", to_translation( "mobile HUD button", "Pick up" ) },
        { "wait", ACTION_WAIT, "wait", to_translation( "mobile HUD button", "Wait" ) },
        { "inventory", ACTION_INVENTORY, "inventory", to_translation( "mobile HUD button", "Inventory" ) },
        { "craft", ACTION_CRAFT, "craft", to_translation( "mobile HUD button", "Craft" ) },
        { "look", ACTION_LOOK, "look", to_translation( "mobile HUD button", "Look" ) },
        { "sleep", ACTION_SLEEP, "sleep", to_translation( "mobile HUD button", "Sleep" ) },
        { "fire", ACTION_FIRE, "fire", to_translation( "mobile HUD button", "Fire" ) },
        { "eat", ACTION_EAT, "hunger", to_translation( "mobile HUD button", "Eat" ) },
        { "read", ACTION_READ, "book", to_translation( "mobile HUD button", "Read" ) },
        { "map", ACTION_MAP, "globe", to_translation( "mobile HUD button", "Map" ) },
        { "throw", ACTION_THROW, "target", to_translation( "mobile HUD button", "Throw" ) },
        { "smash", ACTION_SMASH, "axe", to_translation( "mobile HUD button", "Smash" ) },
        { "reload", ACTION_RELOAD_WEAPON, "shuffle", to_translation( "mobile HUD button", "Reload" ) },
        { "use", ACTION_USE, "gear", to_translation( "mobile HUD button", "Use" ) },
        { "save", ACTION_QUICKSAVE, "save", to_translation( "mobile HUD button", "Save" ) },
    };
    return all;
}

static const quick_entry *quick_entry_of( const std::string &id )
{
    for( const quick_entry &entry : quick_entries() ) {
        if( id == entry.id ) {
            return &entry;
        }
    }
    return nullptr;
}

std::string quick_item_label( const std::string &id )
{
    const quick_entry *const entry = quick_entry_of( id );
    return entry ? entry->label.translated() : id;
}

std::string quick_item_icon( const std::string &id )
{
    const quick_entry *const entry = quick_entry_of( id );
    return entry ? entry->icon : "menu";
}

// The context actions, in the player's order.  The last one opens the pause menu.
static void draw_quick_actions( const piece &cfg )
{
    const size_t count = static_cast<size_t>( std::clamp( cfg.count, quick_count_min,
                         quick_count_max ) );
    std::vector<strip_button> shown;
    for( const std::string &id : quick_order( cfg ) ) {
        if( shown.size() + 1 >= count ) {
            break;
        }
        if( const quick_entry *const entry = quick_entry_of( id ) ) {
            shown.push_back( { entry->act, entry->icon, entry->label.translated() } );
        }
    }
    shown.push_back( { ACTION_ACTIONMENU, "menu", pgettext( "mobile HUD button", "Menu" ) } );
    draw_button_strip( piece_id::quick, cfg.column, shown, 46.0f * unit );
}

// A filled triangle; the points in window pixels.
static void fill_triangle( const SDL_FPoint &a, const SDL_FPoint &b, const SDL_FPoint &c,
                           const rgba &color )
{
    const SDL_FColor fc = to_fcolor( color );
    const std::array<SDL_Vertex, 3> vertices = { {
            { a, fc, { 0.0f, 0.0f } }, { b, fc, { 0.0f, 0.0f } }, { c, fc, { 0.0f, 0.0f } }
        }
    };
    SDL_SetRenderDrawBlendMode( sdl, SDL_BLENDMODE_BLEND );
    SDL_RenderGeometry( sdl, nullptr, vertices.data(), 3, nullptr, 0 );
}

// The round base of a dial and its knob, which follows the thumb.  Returns the
// knob's place, for the graphic on it.
static SDL_FRect draw_dial_base( const piece_id id, const SDL_FRect &area, const hit_kind kind )
{
    dial_state &dial = dial_of( id );
    const float size = area.w;
    dial.radius = size * 0.5f;
    fill_round( area, size * 0.5f, col_panel );
    stroke_round( area, size * 0.5f, std::max( 1.0f, 1.5f * unit ),
                  dial.held && !editing ? col_accent : rgba{ 233, 236, 223, 71 } );
    const float knob = size * 0.46f;
    const float travel = ( size - knob ) * 0.5f - 2.0f * unit;
    const SDL_FRect knob_rect = { area.x + ( size - knob ) * 0.5f + dial.knob_x * travel,
                                  area.y + ( size - knob ) * 0.5f + dial.knob_y * travel, knob, knob
                                };
    fill_round( knob_rect, knob * 0.5f, dial.held && !editing ? rgba{ 240, 145, 63, 64 } :
                rgba{ 233, 236, 223, 41 } );
    stroke_round( knob_rect, knob * 0.5f, std::max( 1.0f, 1.5f * unit ), { 233, 236, 223, 128 } );
    if( !editing ) {
        buttons.push_back( { to_rect( area ), { kind, ACTION_NULL } } );
    }
    return knob_rect;
}

// Zoom: a magnifier on the knob, plus above and minus below.  Drag up to zoom
// in and down to zoom out; see touch_input.cpp.
static void draw_zoom_slider( const piece &cfg );

static void draw_zoom( const piece &cfg )
{
    if( cfg.slider ) {
        draw_zoom_slider( cfg );
        return;
    }
    zoom_drag_now = { false, 48.0f * unit };
    const float size = std::clamp( cfg.size, dial_size_min, dial_size_max ) * unit;
    const SDL_FRect area = place_piece( piece_id::zoom, size, size );
    const SDL_FRect knob = draw_dial_base( piece_id::zoom, area, hit_kind::zoom_dial );
    const float mark = size * 0.2f;
    const float cx = area.x + ( size - mark ) * 0.5f;
    draw_icon( "plus", cx, area.y + size * 0.045f, mark, col_muted );
    draw_icon( "minus", cx, area.y + size - size * 0.045f - mark, mark, col_muted );
    const float glass = knob.w * 0.62f;
    draw_icon( "search", knob.x + ( knob.w - glass ) * 0.5f, knob.y + ( knob.h - glass ) * 0.5f, glass,
               col_fg );
}

// Zoom as a slider: a track with a mark for every zoom level, minus at one end
// and plus at the other, and a thumb where the zoom is now.  Drag along it;
// upright, up zooms in.
static void draw_zoom_slider( const piece &cfg )
{
    const float thick = 44.0f * unit;
    const float length = std::clamp( cfg.width, slider_length_min, slider_length_max ) * unit;
    const bool upright = cfg.column;
    const SDL_FRect area = place_piece( piece_id::zoom, upright ? thick : length,
                                        upright ? length : thick );
    dial_state &dial = dial_of( piece_id::zoom );
    fill_round( area, thick * 0.5f, col_panel );
    stroke_round( area, thick * 0.5f, std::max( 1.0f, 1.5f * unit ),
                  dial.held && !editing ? col_accent : rgba{ 233, 236, 223, 71 } );
    const float mark = 16.0f * unit;
    const float end = 26.0f * unit;
    // the thumb's middle runs from one end of the track to the other
    const float track = length - 2.0f * end - 30.0f * unit;
    const float lo = std::log2( 4.0f );
    const float hi = std::log2( static_cast<float>( touch_input::max_zoom() ) );
    const float levels = std::max( 1.0f, hi - lo );
    const float now = std::clamp( ( std::log2( static_cast<float>( std::max( g->get_zoom(),
                                    1 ) ) ) - lo ) / levels, 0.0f, 1.0f );
    zoom_drag_now = { !upright, track / levels };
    const float cross = ( upright ? area.x : area.y ) + thick * 0.5f;
    // where a fraction of the way in sits along the slider
    const auto along = [&]( const float t ) {
        return upright ? area.y + length - end - 15.0f * unit - t * track :
               area.x + end + 15.0f * unit + t * track;
    };
    for( int i = 0; i <= static_cast<int>( std::lround( levels ) ); ++i ) {
        const float at = along( static_cast<float>( i ) / levels );
        const float dot = 3.0f * unit;
        fill_round( { ( upright ? cross : at ) - dot * 0.5f, ( upright ? at : cross ) - dot * 0.5f, dot, dot },
                    dot * 0.5f, { 233, 236, 223, 110 } );
    }
    const float near_end = ( end - mark ) * 0.5f + 3.0f * unit;
    if( upright ) {
        draw_icon( "plus", cross - mark * 0.5f, area.y + near_end, mark, col_muted );
        draw_icon( "minus", cross - mark * 0.5f, area.y + length - near_end - mark, mark, col_muted );
    } else {
        draw_icon( "minus", area.x + near_end, cross - mark * 0.5f, mark, col_muted );
        draw_icon( "plus", area.x + length - near_end - mark, cross - mark * 0.5f, mark, col_muted );
    }
    const float knob = 30.0f * unit;
    const float at = along( now );
    const SDL_FRect knob_rect = { ( upright ? cross : at ) - knob * 0.5f,
                                  ( upright ? at : cross ) - knob * 0.5f, knob, knob
                                };
    fill_round( knob_rect, knob * 0.5f, dial.held && !editing ? rgba{ 240, 145, 63, 110 } :
                rgba{ 233, 236, 223, 71 } );
    stroke_round( knob_rect, knob * 0.5f, std::max( 1.0f, 1.5f * unit ), { 233, 236, 223, 160 } );
    const float glass = knob * 0.6f;
    draw_icon( "search", knob_rect.x + ( knob - glass ) * 0.5f, knob_rect.y + ( knob - glass ) * 0.5f,
               glass, col_fg );
    if( !editing ) {
        buttons.push_back( { to_rect( area ), { hit_kind::zoom_dial, ACTION_NULL } } );
    }
}

// Pan: an arrowhead each way and a target on the knob.  Drag to scroll the
// view that way, tap to recenter; see touch_input.cpp.
static void draw_pan( const piece &cfg )
{
    const float size = std::clamp( cfg.size, dial_size_min, dial_size_max ) * unit;
    const SDL_FRect area = place_piece( piece_id::pan, size, size );
    const SDL_FRect knob = draw_dial_base( piece_id::pan, area, hit_kind::pan_dial );
    const float cx = area.x + size * 0.5f;
    const float cy = area.y + size * 0.5f;
    // tip, and half the width of the base
    const float tip = size * 0.5f - size * 0.07f;
    const float base = tip - size * 0.11f;
    const float half = size * 0.09f;
    fill_triangle( { cx, cy - tip }, { cx + half, cy - base }, { cx - half, cy - base }, col_muted );
    fill_triangle( { cx, cy + tip }, { cx - half, cy + base }, { cx + half, cy + base }, col_muted );
    fill_triangle( { cx - tip, cy }, { cx - base, cy - half }, { cx - base, cy + half }, col_muted );
    fill_triangle( { cx + tip, cy }, { cx + base, cy + half }, { cx + base, cy - half }, col_muted );
    const float mark = knob.w * 0.62f;
    // lit while the view is away from the player: a tap brings it back
    draw_icon( "target", knob.x + ( knob.w - mark ) * 0.5f, knob.y + ( knob.h - mark ) * 0.5f, mark,
               touch_input::view_is_panned() && !editing ? col_accent : col_fg );
}

// Shown while the view is panned away from the player.
static void draw_recenter()
{
    const std::string label = _( "Recenter" );
    const float label_w = text_width( face::bold, 13.0f, label );
    const float w = 10.0f * unit + 18.0f * unit + 6.0f * unit + label_w + 13.0f * unit;
    const float h = 36.0f * unit;
    const SDL_FRect rect = place_piece( piece_id::recenter, w, h );
    fill_round( rect, h * 0.5f, col_panel_solid );
    stroke_round( rect, h * 0.5f, std::max( 1.0f, unit ), col_line );
    draw_icon( "target", rect.x + 10.0f * unit, rect.y + 9.0f * unit, 18.0f * unit, col_accent );
    draw_text( face::bold, 13.0f, label, rect.x + 34.0f * unit,
               rect.y + ( h - text_height( face::bold, 13.0f ) ) * 0.5f, col_fg );
    add_button( rect, hit_kind::recenter, ACTION_CENTER );
}

// How a pad at a fixed place shows: behind its button, or always.  Layouts made
// before the joystick setting may still say floating; that is the button now.
static pad_mode fixed_pad_mode( const piece &cfg )
{
    return cfg.mode == pad_mode::always ? pad_mode::always : pad_mode::toggle;
}

// The movement pad: a ring with a knob that follows the thumb, and in toggle
// mode a round button that shows and hides it.
static void draw_pad( const piece &cfg )
{
    pad_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    const pad_mode mode = fixed_pad_mode( cfg );
    if( touch_input::floating_joystick() ) {
        if( editing ) {
            const std::string note = _( "Joystick is set to Floating in Settings" );
            const float w = text_width( face::body, 11.0f, note ) + 16.0f * unit;
            const SDL_FRect rect = place_piece( piece_id::joy, w, 28.0f * unit );
            draw_panel( rect );
            draw_text( face::body, 11.0f, note, rect.x + 8.0f * unit, rect.y + 7.0f * unit, col_muted );
        }
        return;
    }
    const float size = cfg.size * unit;
    const SDL_FRect area = place_piece( piece_id::joy, size, size );
    const bool open = mode == pad_mode::always || pad_open || editing;
    const bool right = cfg.pin == anchor::top_right || cfg.pin == anchor::bottom_right;
    const bool bottom = cfg.pin == anchor::bottom_left || cfg.pin == anchor::bottom_center ||
                        cfg.pin == anchor::bottom_right;
    if( open ) {
        const rgba ring = { 233, 236, 223, 71 };
        fill_round( area, size * 0.5f, { 0, 0, 0, 77 } );
        stroke_round( area, size * 0.5f, 2.0f * unit, ring );
        const float cx = area.x + size * 0.5f;
        const float cy = area.y + size * 0.5f;
        // a tick for each of the eight directions
        for( int i = 0; i < 8; ++i ) {
            const float angle = static_cast<float>( i ) * static_cast<float>( M_PI ) * 0.25f;
            const float r = size * 0.5f - 12.0f * unit;
            const float dot = 3.0f * unit;
            fill_round( { cx + std::cos( angle ) * r - dot * 0.5f, cy + std::sin( angle ) * r - dot * 0.5f, dot, dot },
                        dot * 0.5f, { 233, 236, 223, 110 } );
        }
        const float knob = size * 0.38f;
        const SDL_FRect knob_rect = { cx - knob * 0.5f + pad_knob_x *size * 0.31f,
                                      cy - knob * 0.5f + pad_knob_y *size * 0.31f, knob, knob
                                    };
        fill_round( knob_rect, knob * 0.5f, { 233, 236, 223, 77 } );
        stroke_round( knob_rect, knob * 0.5f, 2.0f * unit, { 233, 236, 223, 128 } );
        pad_rect = area;
        if( !editing ) {
            buttons.push_back( { to_rect( area ), { hit_kind::pad, ACTION_NULL } } );
        }
    }
    if( mode == pad_mode::toggle ) {
        // in the pad's corner while it is hidden, just past it while it shows
        const float b = 44.0f * unit;
        SDL_FRect rect = { right ? area.x + size - b : area.x, bottom ? area.y + size - b : area.y, b, b };
        if( open ) {
            rect.y = bottom ? area.y - b - 8.0f * unit : area.y + size + 8.0f * unit;
        }
        const bool lit = pad_open && !editing;
        fill_round( rect, b * 0.5f, col_panel );
        stroke_round( rect, b * 0.5f, std::max( 1.0f, unit ), lit ? col_accent : col_line );
        draw_icon( "joy", rect.x + 11.0f * unit, rect.y + 11.0f * unit, 22.0f * unit,
                   lit ? col_accent : col_fg );
        add_button( rect, hit_kind::pad_toggle, ACTION_NULL );
    }
}

// In the editor every piece gets a dashed frame and a name tag; the selected
// one is solid.
static void draw_edit_frame( const SDL_FRect &rect, const std::string &name, const bool selected )
{
    if( rect.w <= 0.0f || rect.h <= 0.0f ) {
        return;
    }
    const float grow = 3.0f * unit;
    const SDL_FRect frame = { rect.x - grow, rect.y - grow, rect.w + 2.0f * grow, rect.h + 2.0f * grow };
    const rgba line = { col_accent.r, col_accent.g, col_accent.b, static_cast<Uint8>( selected ? 255 : 191 ) };
    if( selected ) {
        stroke_round( frame, 4.0f * unit, 2.0f * unit, line );
    } else {
        // dashes along the four sides
        const float dash = 6.0f * unit;
        const float thick = std::max( 1.0f, 1.5f * unit );
        for( float x = frame.x; x < frame.x + frame.w; x += 2.0f * dash ) {
            const float w = std::min( dash, frame.x + frame.w - x );
            fill_round( { x, frame.y, w, thick }, 0.0f, line );
            fill_round( { x, frame.y + frame.h - thick, w, thick }, 0.0f, line );
        }
        for( float y = frame.y; y < frame.y + frame.h; y += 2.0f * dash ) {
            const float h = std::min( dash, frame.y + frame.h - y );
            fill_round( { frame.x, y, thick, h }, 0.0f, line );
            fill_round( { frame.x + frame.w - thick, y, thick, h }, 0.0f, line );
        }
    }
    const float tag_w = text_width( face::mono, 10.0f, name ) + 10.0f * unit;
    const float tag_h = 16.0f * unit;
    // above the piece, or inside it at the top of the screen
    const float tag_y = frame.y - tag_h >= frame_screen.y ? frame.y - tag_h : frame.y;
    fill_round( { frame.x, tag_y, tag_w, tag_h }, 3.0f * unit,
                selected ? col_accent : rgba{ 217, 220, 207, 255 } );
    draw_text( face::mono, 10.0f, name, frame.x + 5.0f * unit,
               tag_y + ( tag_h - text_height( face::mono, 10.0f ) ) * 0.5f, { 17, 17, 17, 255 } );
}

void draw( const SDL_Renderer_Ptr &renderer, Font &, const SDL_Rect &view, const int window_w,
           const int window_h, const bool default_mode )
{
    buttons.clear();
    pad_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    piece_rects.fill( { 0.0f, 0.0f, 0.0f, 0.0f } );
    handle_rects.fill( { 0.0f, 0.0f, 0.0f, 0.0f } );
    dials[0].radius = dials[1].radius = 0.0f;
    if( !g || ( !default_mode && !editing ) || !renderer || window_w <= 0 || window_h <= 0 ) {
        return;
    }
    sdl = renderer.get();
    flush_caches_if_renderer_changed();
    const SDL_FRect area = hud_area( view, window_w, window_h );
    unit = std::min( area.w, area.h ) / 390.0f;
    if( unit <= 0.0f ) {
        return;
    }
    const SDL_FRect whole = view.w > 0 && view.h > 0 ?
                            SDL_FRect{ static_cast<float>( view.x ), static_cast<float>( view.y ), static_cast<float>( view.w ), static_cast<float>( view.h ) } :
                            SDL_FRect{ 0.0f, 0.0f, static_cast<float>( window_w ), static_cast<float>( window_h ) };
    frame_screen = { whole.x, whole.y, whole.w, whole.h };
    frame_landscape = area.w > area.h;
    const layout &current = editing ? ( frame_landscape ? edit_preset.landscape :
                                        edit_preset.portrait ) : active_layout( frame_landscape );
    frame_layout = &current;
    frame_handle_safe = { area.x - whole.x, area.y - whole.y,
                          whole.x + whole.w - area.x - area.w, whole.y + whole.h - area.y - area.h
                        };
    frame_safe = frame_handle_safe;
    if( frame_landscape ) {
        // The canvas's phone has a notch band at either side in landscape, and
        // side handles live in it.  Without a notch the pieces still keep clear.
        for( const drawer &d : current.drawers ) {
            if( d.visible && d.edge == screen_edge::left ) {
                frame_safe.left = std::max( frame_safe.left, 30.0f * unit );
            }
            if( d.visible && d.edge == screen_edge::right ) {
                frame_safe.right = std::max( frame_safe.right, 30.0f * unit );
            }
        }
    }

    if( touch_input::hud_enabled() ) {
        const auto shown = [&current]( const piece_id id ) {
            opacity = current[id].opacity;
            return current[id].visible;
        };
        if( shown( piece_id::location ) ) {
            draw_location( current[piece_id::location] );
        }
        if( shown( piece_id::status ) ) {
            draw_status( current[piece_id::status] );
        }
        if( shown( piece_id::body ) ) {
            draw_body( current[piece_id::body] );
        }
        if( shown( piece_id::wield ) ) {
            draw_wielded();
        }
        if( shown( piece_id::log ) ) {
            draw_log( current[piece_id::log] );
        }
        if( shown( piece_id::quick ) ) {
            draw_quick_actions( current[piece_id::quick] );
        }
        // the map only zooms when it is drawn with tiles
        if( shown( piece_id::zoom ) && ( use_tiles || editing ) ) {
            draw_zoom( current[piece_id::zoom] );
        }
        if( shown( piece_id::pan ) ) {
            draw_pan( current[piece_id::pan] );
        }
        if( shown( piece_id::joy ) ) {
            draw_pad( current[piece_id::joy] );
        }
        opacity = 1.0f;
        draw_drawer_handles();
    }
    opacity = 1.0f;
    if( ( touch_input::view_is_panned() || editing ) && current[piece_id::recenter].visible ) {
        draw_recenter();
    } else if( touch_input::view_is_panned() ) {
        // never leave a panned view without a way back
        piece fallback = fallback_preset().landscape[piece_id::recenter];
        const layout with_button = [&]() {
            layout l = current;
            l[piece_id::recenter] = fallback;
            return l;
        }
        ();
        frame_layout = &with_button;
        draw_recenter();
        frame_layout = &current;
    }
    if( editing ) {
        for( size_t i = 0; i < num_pieces; ++i ) {
            const piece_id id = static_cast<piece_id>( i );
            draw_edit_frame( piece_rects[i], display_name( id ),
                             edit_selection == static_cast<int>( i ) );
        }
        for( size_t i = 0; i < num_drawers; ++i ) {
            draw_edit_frame( handle_rects[i], display_name( static_cast<drawer_id>( i ) ),
                             edit_selection == static_cast<int>( num_pieces + i ) );
        }
    }
    SDL_SetRenderDrawBlendMode( sdl, SDL_BLENDMODE_NONE );
}

std::optional<hit> hit_at( const float x, const float y )
{
    // later pieces are drawn over earlier ones
    for( auto it = buttons.rbegin(); it != buttons.rend(); ++it ) {
        if( contains( it->where, x, y ) ) {
            return it->what;
        }
    }
    return std::nullopt;
}

// --- The movement pad ---

pad_mode movement_pad_mode()
{
    if( !touch_input::hud_enabled() || !g || touch_input::floating_joystick() ) {
        return pad_mode::floating;
    }
    int w = 0;
    int h = 0;
    if( SDL_Window *const win = get_sdl_window() ) {
        SDL_GetWindowSize( win, &w, &h );
    }
    const piece &joy = active_layout( w > h )[piece_id::joy];
    return joy.visible ? fixed_pad_mode( joy ) : pad_mode::floating;
}

void toggle_pad()
{
    pad_open = !pad_open;
    pad_knob_x = pad_knob_y = 0.0f;
}

std::optional<point> pad_direction( const float x, const float y )
{
    if( pad_rect.w <= 0.0f ) {
        return std::nullopt;
    }
    const float radius = pad_rect.w * 0.5f;
    const float dx = x - ( pad_rect.x + radius );
    const float dy = y - ( pad_rect.y + radius );
    const float distance = std::hypot( dx, dy );
    const float reach = std::min( 1.0f, distance / radius );
    pad_knob_x = distance > 0.0f ? dx / distance * reach : 0.0f;
    pad_knob_y = distance > 0.0f ? dy / distance * reach : 0.0f;
    // a dead zone in the middle
    if( distance < pad_rect.w * 0.12f ) {
        return std::nullopt;
    }
    const int octant = ( static_cast<int>( std::lround( std::atan2( dy, dx ) /
                                           ( static_cast<float>( M_PI ) * 0.25f ) ) ) % 8 + 8 ) % 8;
    static constexpr std::array<point, 8> directions = { {
            point( 1, 0 ), point( 1, 1 ), point( 0, 1 ), point( -1, 1 ),
            point( -1, 0 ), point( -1, -1 ), point( 0, -1 ), point( 1, -1 )
        }
    };
    return directions[static_cast<size_t>( octant )];
}

void release_pad()
{
    pad_knob_x = pad_knob_y = 0.0f;
}

// --- The zoom and pan dials ---

void set_dial( const piece_id which, const bool held, const float knob_x, const float knob_y )
{
    dial_state &dial = dial_of( which );
    dial.held = held;
    dial.knob_x = std::clamp( knob_x, -1.0f, 1.0f );
    dial.knob_y = std::clamp( knob_y, -1.0f, 1.0f );
}

float dial_radius( const piece_id which )
{
    return dial_of( which ).radius;
}

float unit_pixels()
{
    return unit;
}

zoom_drag zoom_drag_info()
{
    return zoom_drag_now;
}

// --- The editor's view of the HUD ---

void begin_edit( const preset &working )
{
    editing = true;
    edit_preset = working;
    edit_selection = -1;
}

void update_edit( const preset &working, const int selection )
{
    edit_preset = working;
    edit_selection = selection;
}

void end_edit()
{
    editing = false;
    edit_selection = -1;
}

bool is_editing()
{
    return editing;
}

edit_geometry edit_frame()
{
    edit_geometry geo;
    geo.screen = frame_screen;
    geo.safe = frame_safe;
    geo.handle_safe = frame_handle_safe;
    geo.unit = unit;
    geo.landscape = frame_landscape;
    for( size_t i = 0; i < num_pieces; ++i ) {
        geo.pieces[i] = { piece_rects[i].x, piece_rects[i].y, piece_rects[i].w, piece_rects[i].h };
    }
    for( size_t i = 0; i < num_drawers; ++i ) {
        geo.handles[i] = { handle_rects[i].x, handle_rects[i].y, handle_rects[i].w, handle_rects[i].h };
    }
    return geo;
}

} // namespace mobile_hud

#endif // TILES
