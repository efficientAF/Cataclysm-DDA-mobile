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

struct quick_button {
    SDL_Rect rect;
    action_id act;
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

static SDL_Rect recenter_rect = { 0, 0, 0, 0 };
static std::vector<quick_button> quick_buttons;

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
    static const text_entry none;
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
    SDL_SetTextureAlphaMod( entry.texture, color.a );
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
    SDL_SetTextureAlphaMod( texture, color.a );
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
             static_cast<float>( color.b ) / 255.0f, static_cast<float>( color.a ) / 255.0f
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

// --- Pieces, placed as in the canvas's Balanced preset ---

// Place on the first row; date and time on the second.  Returns its bottom edge.
static float draw_location( const SDL_FRect &area )
{
    const avatar &you = get_avatar();
    const float w = std::min( 236.0f * unit, area.w - 20.0f * unit );
    const float line = 17.0f * unit;
    const SDL_FRect rect = { area.x + 10.0f * unit, area.y + 12.0f * unit, w, 2.0f * line + 12.0f * unit };
    draw_panel( rect );
    const float room = w - 20.0f * unit;
    const float x = rect.x + 10.0f * unit;
    const float pad_y = ( line - text_height( face::body, 13.0f ) ) * 0.5f;
    const std::string place = remove_color_tags( display::current_position_text(
                                  you.pos_abs_omt() ) );
    draw_text( face::bold, 13.0f, fit_text( face::bold, 13.0f, place, room ), x,
               rect.y + 6.0f * unit + pad_y, col_fg );
    const std::string when = string_format( "%s · %s", display::date_string(),
                                            remove_color_tags( display::time_string( you ) ) );
    draw_text( face::body, 13.0f, fit_text( face::body, 13.0f, when, room ), x,
               rect.y + 6.0f * unit + line + pad_y, col_fg );
    return rect.y + rect.h;
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
static void draw_status( const SDL_FRect &area, const float top, const bool landscape )
{
    static constexpr std::array<rgba, 4> severity = { {
            { 216, 187, 66, 255 }, { 230, 137, 45, 255 }, { 224, 73, 43, 255 }, { 255, 106, 85, 255 }
        }
    };
    const float size = ( landscape ? 32.0f : 34.0f ) * unit;
    const float gap = 4.0f * unit;
    const size_t max_shown = landscape ? 8 : 6;
    float x = area.x + 10.0f * unit;
    const float y = top + 8.0f * unit;
    size_t shown = 0;
    for( const condition &cond : active_conditions() ) {
        if( shown++ >= max_shown ) {
            break;
        }
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
        x += size + gap;
    }
}

static const rgba &hp_color( const avatar &you, const char *part_id )
{
    const bodypart_id part( part_id );
    const int hp_max = you.get_part_hp_max( part );
    const int percent = hp_max > 0 ? you.get_part_hp_cur( part ) * 100 / hp_max : 0;
    return percent > 80 ? col_hp_good : percent > 50 ? col_hp_ok : percent > 25 ? col_hp_low :
           col_hp_bad;
}

// A small figure colored by the health of each limb.  Returns its bottom edge.
static float draw_body( const SDL_FRect &area, const bool landscape )
{
    const avatar &you = get_avatar();
    const float size = ( landscape ? 64.0f : 70.0f ) * unit;
    // the figure is drawn on the canvas's 40 by 62 grid
    const float k = size / 62.0f;
    const SDL_FRect rect = { area.x + area.w - 10.0f * unit - ( 40.0f * k + 14.0f * unit ),
                             area.y + 12.0f * unit, 40.0f * k + 14.0f * unit, size + 10.0f * unit
                           };
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
    return rect.y + rect.h;
}

// What is in your hands, under the body figure.
static void draw_wielded( const SDL_FRect &area, const float top )
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
    const SDL_FRect rect = { area.x + area.w - 10.0f * unit - w, top + 8.0f * unit, w, 40.0f * unit };
    draw_panel( rect );
    draw_icon( "axe", rect.x + 7.0f * unit, rect.y + 9.0f * unit, 22.0f * unit, col_accent );
    const float x = rect.x + 37.0f * unit;
    draw_text( face::bold, 13.0f, shown_name, x, rect.y + 5.0f * unit, col_fg );
    draw_text( face::body, 12.0f, detail, x, rect.y + 20.0f * unit, col_muted );
}

// The last three messages, newest at the bottom, older ones dimmed.
static void draw_log( const SDL_FRect &area, const bool landscape )
{
    static constexpr size_t rows = 3;
    std::vector<std::pair<std::string, std::string>> messages = Messages::recent_messages( rows );
    const float w = std::min( ( landscape ? 320.0f : 366.0f ) * unit, area.w - 20.0f * unit );
    const float line = 17.0f * unit;
    const SDL_FRect rect = { area.x + 10.0f * unit,
                             area.y + area.h - ( landscape ? 20.0f : 100.0f ) *unit -
                             ( static_cast<float>( rows ) * line + 12.0f * unit ),
                             w, static_cast<float>( rows ) *line + 12.0f * unit
                           };
    draw_panel( rect );
    const float pad_y = ( line - text_height( face::body, 13.0f ) ) * 0.5f;
    // fill from the bottom, so a short log leaves the top rows empty
    float y = rect.y + 6.0f * unit + static_cast<float>( rows - messages.size() ) * line;
    for( size_t i = 0; i < messages.size(); ++i ) {
        const std::string text = fit_text( face::body, 13.0f, remove_color_tags( messages[i].second ),
                                           w - 20.0f * unit );
        const bool old = i + 2 < messages.size();
        draw_text( face::body, 13.0f, text, rect.x + 10.0f * unit, y + pad_y,
                   old ? rgba{ col_fg.r, col_fg.g, col_fg.b, 158 } : col_fg );
        y += line;
    }
}

// The context actions: a column at the bottom right in landscape, a row along
// the bottom in portrait.  The last one opens the action menu.
static void draw_quick_actions( const SDL_FRect &area, const bool landscape )
{
    struct entry {
        action_id act;
        const char *icon;
        translation label;
    };
    static const std::vector<entry> all = {
        { ACTION_EXAMINE, "examine", to_translation( "mobile HUD button", "Examine" ) },
        { ACTION_PICKUP, "pickup", to_translation( "mobile HUD button", "Pick up" ) },
        { ACTION_WAIT, "wait", to_translation( "mobile HUD button", "Wait" ) },
        { ACTION_INVENTORY, "inventory", to_translation( "mobile HUD button", "Inventory" ) },
        { ACTION_CRAFT, "craft", to_translation( "mobile HUD button", "Craft" ) },
        { ACTION_ACTIONMENU, "menu", to_translation( "mobile HUD button", "Menu" ) },
    };
    // Balanced preset: four in landscape (the first three and Menu), six in portrait
    std::vector<entry> shown;
    if( landscape ) {
        shown = { all[0], all[1], all[2], all[5] };
    } else {
        shown = all;
    }
    const float button = 46.0f * unit;
    const float gap = 6.0f * unit;
    const float pad = 4.0f * unit;
    const float count = static_cast<float>( shown.size() );
    const float length = count * button + ( count - 1.0f ) * gap + 2.0f * pad;
    const float thickness = button + 2.0f * pad;
    const SDL_FRect panel = landscape ?
                            SDL_FRect{ area.x + area.w - 10.0f * unit - thickness, area.y + area.h - 20.0f * unit - length, thickness, length } :
                            SDL_FRect{ area.x + ( area.w - length ) * 0.5f, area.y + area.h - 34.0f * unit - thickness, length, thickness };
    draw_panel( panel );
    float x = panel.x + pad;
    float y = panel.y + pad;
    for( const entry &e : shown ) {
        const SDL_FRect rect = { x, y, button, button };
        fill_round( rect, 10.0f * unit, { 255, 255, 255, 13 } );
        stroke_round( rect, 10.0f * unit, std::max( 1.0f, unit ), col_line );
        draw_icon( e.icon, x + ( button - 20.0f * unit ) * 0.5f, y + 7.0f * unit, 20.0f * unit, col_fg );
        const std::string label = fit_text( face::body, 9.0f, e.label.translated(), button - 2.0f * unit );
        draw_text( face::body, 9.0f, label, x + ( button - text_width( face::body, 9.0f, label ) ) * 0.5f,
                   y + 30.0f * unit, col_muted );
        quick_buttons.push_back( { to_rect( rect ), e.act } );
        if( landscape ) {
            y += button + gap;
        } else {
            x += button + gap;
        }
    }
}

// Shown while the view is panned away from the player.
static void draw_recenter( const SDL_FRect &area, const bool landscape )
{
    const std::string label = _( "Recenter" );
    const float label_w = text_width( face::bold, 13.0f, label );
    const float w = 10.0f * unit + 18.0f * unit + 6.0f * unit + label_w + 13.0f * unit;
    const float h = 36.0f * unit;
    // Balanced preset: top center and 40 down in landscape, top right and 236 down in portrait
    const SDL_FRect rect = landscape ?
                           SDL_FRect{ area.x + ( area.w - w ) * 0.5f, area.y + 40.0f * unit, w, h } :
                           SDL_FRect{ area.x + area.w - 12.0f * unit - w, area.y + 236.0f * unit, w, h };
    fill_round( rect, h * 0.5f, col_panel_solid );
    stroke_round( rect, h * 0.5f, std::max( 1.0f, unit ), col_line );
    draw_icon( "target", rect.x + 10.0f * unit, rect.y + 9.0f * unit, 18.0f * unit, col_accent );
    draw_text( face::bold, 13.0f, label, rect.x + 34.0f * unit,
               rect.y + ( h - text_height( face::bold, 13.0f ) ) * 0.5f, col_fg );
    recenter_rect = to_rect( rect );
}

void draw( const SDL_Renderer_Ptr &renderer, Font &, const SDL_Rect &view, const int window_w,
           const int window_h, const bool default_mode )
{
    recenter_rect = { 0, 0, 0, 0 };
    quick_buttons.clear();
    if( !g || !default_mode || !renderer || window_w <= 0 || window_h <= 0 ) {
        return;
    }
    sdl = renderer.get();
    flush_caches_if_renderer_changed();
    const SDL_FRect area = hud_area( view, window_w, window_h );
    unit = std::min( area.w, area.h ) / 390.0f;
    if( unit <= 0.0f ) {
        return;
    }
    const bool landscape = area.w > area.h;

    if( touch_input::hud_enabled() ) {
        const float location_bottom = draw_location( area );
        draw_status( area, location_bottom, landscape );
        const float body_bottom = draw_body( area, landscape );
        draw_wielded( area, body_bottom );
        draw_log( area, landscape );
        draw_quick_actions( area, landscape );
    }
    if( touch_input::view_is_panned() ) {
        draw_recenter( area, landscape );
    }
    SDL_SetRenderDrawBlendMode( sdl, SDL_BLENDMODE_NONE );
}

std::optional<action_id> button_at( const float x, const float y )
{
    if( contains( recenter_rect, x, y ) ) {
        return ACTION_CENTER;
    }
    for( const quick_button &button : quick_buttons ) {
        if( contains( button.rect, x, y ) ) {
            return button.act;
        }
    }
    return std::nullopt;
}

} // namespace mobile_hud

#endif // TILES
