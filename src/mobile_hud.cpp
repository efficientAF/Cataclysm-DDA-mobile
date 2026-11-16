#if defined(TILES)

#include "mobile_hud.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "catacharset.h"
#include "color.h"
#include "cursesport.h"
#include "display.h"
#include "game.h"
#include "messages.h"
#include "output.h"
#include "point.h"
#include "sdl_font.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "touch_input.h"
#include "translations.h"

namespace mobile_hud
{

// Curses color indices into the font palette.
static constexpr unsigned char color_light_gray = 7;
static constexpr unsigned char color_white = 15;

// Sizes in canvas units.
static constexpr float text_units = 13.0f;
static constexpr float line_units = 17.0f;
static constexpr float pad_units = 10.0f;
static constexpr float margin_units = 12.0f;

// Panel background, as in the canvas: rgba(13, 16, 12, 0.8)
static constexpr Uint8 panel_r = 13;
static constexpr Uint8 panel_g = 16;
static constexpr Uint8 panel_b = 12;
static constexpr Uint8 panel_a = 204;

// Rectangle of the Recenter button in window pixels; empty while it is hidden.
static SDL_Rect recenter_rect = { 0, 0, 0, 0 };

namespace
{
struct quick_button {
    SDL_Rect rect;
    action_id act;
};
} // namespace

// Quick action buttons drawn in the last frame.
static std::vector<quick_button> quick_buttons;

static bool contains( const SDL_Rect &rect, const float x, const float y )
{
    return rect.w > 0 && rect.h > 0 &&
           x >= static_cast<float>( rect.x ) && x < static_cast<float>( rect.x + rect.w ) &&
           y >= static_cast<float>( rect.y ) && y < static_cast<float>( rect.y + rect.h );
}

// Where the HUD may draw: the game view, minus notches and system bars.
static SDL_Rect hud_area( const SDL_Rect &view, const int window_w, const int window_h )
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
    return area;
}

static void fill_rect( const SDL_Renderer_Ptr &renderer, const SDL_Rect &rect, const Uint8 r,
                       const Uint8 g, const Uint8 b, const Uint8 a )
{
    SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
    SetRenderDrawColor( renderer, r, g, b, a );
    RenderFillRect( renderer, &rect );
    SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
}

static float text_width( const Font &font, const std::string &text, const float text_height )
{
    return static_cast<float>( font.width * utf8_width( text ) ) * text_height /
           static_cast<float>( font.height );
}

// Shortens text to fit into max_width pixels.
static std::string fit_text( const Font &font, const std::string &text, const float text_height,
                             const float max_width )
{
    const float cell = static_cast<float>( font.width ) * text_height / static_cast<float>
                       ( font.height );
    const int columns = cell > 0.0f ? static_cast<int>( max_width / cell ) : 0;
    if( utf8_width( text ) <= columns ) {
        return text;
    }
    return utf8_truncate( text, static_cast<size_t>( std::max( columns, 0 ) ) );
}

// Draws one line of text with its top left corner at x, y, text_height pixels tall.
static void draw_text( const SDL_Renderer_Ptr &renderer, Font &font, const std::string &text,
                       const float x, const float y, const float text_height, const unsigned char color )
{
    if( text.empty() ) {
        return;
    }
    const float scale = text_height / static_cast<float>( font.height );
    RenderSetScale( renderer, scale, scale );
    font.OutputChar( renderer, text, point( static_cast<int>( x / scale ),
                                            static_cast<int>( y / scale ) ), color );
    RenderSetScale( renderer, 1.0f, 1.0f );
}

static void draw_recenter_button( const SDL_Renderer_Ptr &renderer, Font &font,
                                  const SDL_Rect &safe, const float unit, const bool landscape )
{
    const std::string label = _( "Recenter" );
    const float text_height = 16.0f * unit;
    const int w = static_cast<int>( text_width( font, label, text_height ) + 28.0f * unit );
    const int h = static_cast<int>( 40.0f * unit );
    SDL_Rect rect = { 0, 0, w, h };
    if( landscape ) {
        // canvas: anchor top center, 40 units down
        rect.x = safe.x + ( safe.w - w ) / 2;
        rect.y = safe.y + static_cast<int>( 40.0f * unit );
    } else {
        // canvas: anchor top right, 12 units in and 236 down
        rect.x = safe.x + safe.w - w - static_cast<int>( 12.0f * unit );
        rect.y = safe.y + static_cast<int>( 236.0f * unit );
    }

    const int border = std::max( 1, static_cast<int>( 1.5f * unit ) );
    // accent border, then the dark panel inside it
    fill_rect( renderer, rect, 240, 145, 63, 255 );
    const SDL_Rect inner = { rect.x + border, rect.y + border, rect.w - 2 * border, rect.h - 2 * border };
    fill_rect( renderer, inner, 20, 24, 18, 255 );
    draw_text( renderer, font, label,
               static_cast<float>( rect.x ) + ( static_cast<float>( rect.w ) -
                       text_width( font, label, text_height ) ) * 0.5f,
               static_cast<float>( rect.y ) + ( static_cast<float>( rect.h ) - text_height ) * 0.5f,
               text_height, color_white );
    recenter_rect = rect;
}

// Location name on the first row; date, time, weather and temperature on the second.
// Returns the bottom edge of the panel.
static int draw_location( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &safe,
                          const float unit )
{
    const avatar &you = get_avatar();
    const std::string place = remove_color_tags( display::current_position_text(
                                  you.pos_abs_omt() ) );
    const std::string when = string_format( "%s  %s  %s  %s", display::date_string(),
                                            remove_color_tags( display::time_string( you ) ),
                                            remove_color_tags( display::weather_text_color( you ).first ),
                                            remove_color_tags( display::get_temp( you ) ) );
    const float text_height = text_units * unit;
    const float line = line_units * unit;
    const float pad = pad_units * unit;
    const int max_w = safe.w - static_cast<int>( 2.0f * margin_units * unit );
    const float wanted = std::max( text_width( font, place, text_height ),
                                   text_width( font, when, text_height ) ) + 2.0f * pad;
    const SDL_Rect rect = { safe.x + static_cast<int>( margin_units * unit ),
                            safe.y + static_cast<int>( margin_units * unit ),
                            std::min( static_cast<int>( wanted ), max_w ),
                            static_cast<int>( 2.0f * line + 2.0f * pad * 0.6f )
                          };
    fill_rect( renderer, rect, panel_r, panel_g, panel_b, panel_a );
    const float text_x = static_cast<float>( rect.x ) + pad;
    const float text_y = static_cast<float>( rect.y ) + pad * 0.6f + ( line - text_height ) * 0.5f;
    const float room = static_cast<float>( rect.w ) - 2.0f * pad;
    draw_text( renderer, font, fit_text( font, place, text_height, room ), text_x, text_y,
               text_height, color_white );
    draw_text( renderer, font, fit_text( font, when, text_height, room ), text_x, text_y + line,
               text_height, color_light_gray );
    return rect.y + rect.h;
}

// Index into the font palette for a curses color.
static unsigned char palette_index( const nc_color &color )
{
    int fg = static_cast<int>( cata_cursesport::colorpairs[color.to_color_pair_index()].FG );
    if( color.is_bold() ) {
        fg += 8;
    }
    return static_cast<unsigned char>( std::clamp( fg, 0, 15 ) );
}

// Colors the game uses for "nothing wrong": no chip is shown for them.
static bool is_calm_color( const unsigned char index )
{
    // black, green, light gray, dark gray, light green, white
    return index == 0 || index == 2 || index == 7 || index == 8 || index == 10 || index == 15;
}

// One chip per condition that needs attention, hidden while things are normal.
// The texts and colors are the ones the sidebar widgets show.
static void draw_status( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &safe,
                         const float unit, const int top )
{
    const avatar &you = get_avatar();
    const std::vector<std::pair<std::string, nc_color>> conditions = {
        display::pain_text_color( you ),
        display::hunger_text_color( you ),
        display::thirst_text_color( you ),
        display::sleepiness_text_color( you ),
        display::temp_text_color( you ),
        display::weariness_text_color( you ),
        display::carry_weight_text_color( you ),
    };
    const float text_height = text_units * unit;
    const float pad = pad_units * unit;
    const int border = std::max( 1, static_cast<int>( 2.0f * unit ) );
    const int h = static_cast<int>( 30.0f * unit );
    const int gap = static_cast<int>( 4.0f * unit );
    const int left = safe.x + static_cast<int>( margin_units * unit );
    const int right = safe.x + safe.w - static_cast<int>( margin_units * unit );
    int x = left;
    int y = top + static_cast<int>( 8.0f * unit );
    for( const std::pair<std::string, nc_color> &condition : conditions ) {
        const std::string label = remove_color_tags( condition.first );
        const unsigned char color = palette_index( condition.second );
        if( label.empty() || is_calm_color( color ) ) {
            continue;
        }
        const int w = static_cast<int>( text_width( font, label, text_height ) + 2.0f * pad );
        if( x + w > right && x > left ) {
            // next row
            x = left;
            y += h + gap;
        }
        const SDL_Rect rect = { x, y, w, h };
        const SDL_Color &rgb = windowsPalette[color];
        fill_rect( renderer, rect, rgb.r, rgb.g, rgb.b, 255 );
        const SDL_Rect inner = { rect.x + border, rect.y + border, rect.w - 2 * border, rect.h - 2 * border };
        fill_rect( renderer, inner, 11, 13, 10, 255 );
        draw_text( renderer, font, label, static_cast<float>( x ) + pad,
                   static_cast<float>( y ) + ( static_cast<float>( h ) - text_height ) * 0.5f,
                   text_height, color );
        x += w + gap;
    }
}

// The last few messages, newest at the bottom.
static void draw_log( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &safe,
                      const float unit, const bool landscape )
{
    static constexpr int rows = 3;
    const std::vector<std::pair<std::string, std::string>> messages = Messages::recent_messages(
                rows );
    if( messages.empty() ) {
        return;
    }
    const float text_height = text_units * unit;
    const float line = line_units * unit;
    const float pad = pad_units * unit;
    // canvas: anchor bottom left; 320 wide and 20 up in landscape, 366 wide and 100 up in portrait
    const int w = std::min( static_cast<int>( ( landscape ? 320.0f : 366.0f ) * unit ),
                            safe.w - static_cast<int>( 2.0f * margin_units * unit ) );
    const int h = static_cast<int>( static_cast<float>( messages.size() ) * line + 2.0f * pad * 0.6f );
    const SDL_Rect rect = { safe.x + static_cast<int>( margin_units * unit ),
                            safe.y + safe.h - h - static_cast<int>( ( landscape ? 20.0f : 100.0f ) * unit ),
                            w, h
                          };
    fill_rect( renderer, rect, panel_r, panel_g, panel_b, panel_a );
    const float room = static_cast<float>( rect.w ) - 2.0f * pad;
    float y = static_cast<float>( rect.y ) + pad * 0.6f + ( line - text_height ) * 0.5f;
    for( size_t i = 0; i < messages.size(); ++i ) {
        const std::string text = fit_text( font, remove_color_tags( messages[i].second ), text_height,
                                           room );
        // older lines are dimmed
        draw_text( renderer, font, text, static_cast<float>( rect.x ) + pad, y, text_height,
                   i + 1 == messages.size() ? color_white : color_light_gray );
        y += line;
    }
}

// Buttons for the actions you need most, as in the canvas's Quick actions piece:
// a column at the bottom right in landscape, a row along the bottom in portrait.
static void draw_quick_buttons( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &safe,
                                const float unit, const bool landscape )
{
    static const std::vector<std::pair<action_id, translation>> actions = {
        { ACTION_PICKUP, to_translation( "mobile HUD button", "Get" ) },
        { ACTION_INVENTORY, to_translation( "mobile HUD button", "Items" ) },
        { ACTION_CRAFT, to_translation( "mobile HUD button", "Craft" ) },
        { ACTION_MAP, to_translation( "mobile HUD button", "Map" ) },
        { ACTION_WAIT, to_translation( "mobile HUD button", "Wait" ) },
        { ACTION_ACTIONMENU, to_translation( "mobile HUD button", "Menu" ) },
    };
    const int size = static_cast<int>( 48.0f * unit );
    const int gap = static_cast<int>( 6.0f * unit );
    const int count = static_cast<int>( actions.size() );
    const int length = count * size + ( count - 1 ) * gap;
    const float text_height = 12.0f * unit;
    const int border = std::max( 1, static_cast<int>( unit ) );
    // canvas: bottom right, 20 up, in landscape; bottom center, 34 up, in portrait
    int x = landscape ? safe.x + safe.w - size - static_cast<int>( margin_units * unit ) :
            safe.x + ( safe.w - length ) / 2;
    int y = landscape ? safe.y + safe.h - length - static_cast<int>( 20.0f * unit ) :
            safe.y + safe.h - size - static_cast<int>( 34.0f * unit );
    for( const std::pair<action_id, translation> &action : actions ) {
        const SDL_Rect rect = { x, y, size, size };
        fill_rect( renderer, rect, 233, 236, 223, 60 );
        const SDL_Rect inner = { rect.x + border, rect.y + border, rect.w - 2 * border, rect.h - 2 * border };
        fill_rect( renderer, inner, panel_r, panel_g, panel_b, 230 );
        const std::string label = fit_text( font, action.second.translated(), text_height,
                                            static_cast<float>( size ) );
        draw_text( renderer, font, label,
                   static_cast<float>( x ) + ( static_cast<float>( size ) - text_width( font, label,
                                               text_height ) ) * 0.5f,
                   static_cast<float>( y ) + ( static_cast<float>( size ) - text_height ) * 0.5f,
                   text_height, color_white );
        quick_buttons.push_back( { rect, action.first } );
        if( landscape ) {
            y += size + gap;
        } else {
            x += size + gap;
        }
    }
}

void draw( const SDL_Renderer_Ptr &renderer, Font &font, const SDL_Rect &view, const int window_w,
           const int window_h, const bool default_mode )
{
    recenter_rect = { 0, 0, 0, 0 };
    quick_buttons.clear();
    if( !g || !default_mode || window_w <= 0 || window_h <= 0 || font.width <= 0 ||
        font.height <= 0 ) {
        return;
    }
    const SDL_Rect safe = hud_area( view, window_w, window_h );
    const float unit = static_cast<float>( std::min( safe.w, safe.h ) ) / 390.0f;
    const bool landscape = safe.w > safe.h;
    if( unit <= 0.0f ) {
        return;
    }

    if( touch_input::hud_enabled() ) {
        const int location_bottom = draw_location( renderer, font, safe, unit );
        draw_status( renderer, font, safe, unit, location_bottom );
        draw_log( renderer, font, safe, unit, landscape );
        draw_quick_buttons( renderer, font, safe, unit, landscape );
    }
    if( touch_input::view_is_panned() ) {
        draw_recenter_button( renderer, font, safe, unit, landscape );
    }
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
