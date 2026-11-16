#include "mobile_overmap.h"

#include "coordinates.h"
#include "mobile_menu.h"
#include "overmap_ui.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "cata_imgui.h"
#include "cata_tiles.h"
#include "color.h"
#include "cursesport.h"
#include "game.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input.h"
#include "input_context.h"
#include "line.h"
#include "mobile_ui.h"
#include "omdata.h"
#include "output.h"
#include "overmapbuffer.h"
#include "point.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "translations.h"
#endif

namespace mobile_overmap
{

bool wanted( const overmap_ui::overmap_draw_data_t &data )
{
#if defined(TILES)
    return mobile_menu::enabled() && !data.debug_editor && !data.debug_scent &&
           data.message.empty() && data.distance == INT_MAX && data.display_path.empty() &&
           data.iZoneIndex == -1;
#else
    static_cast<void>( data );
    return false;
#endif
}

#if defined(TILES)

using mobile_ui::face;

namespace
{
// What the game knows about one overmap tile, as the tile sheet shows it.
struct place {
    bool seen = false;
    std::string sym;
    ImU32 color = 0;
    std::string name;
};

// Where the overmap is on screen, in ImGui coordinates.  The same numbers
// input_context::get_coordinates uses for a mouse click on the overmap.
struct map_geometry {
    point win_pos;
    point win_size;
    point tile;
    // tiles from the window's corner to the tile at the cursor
    point center;
    bool valid = false;
};

struct legend_entry {
    std::string sym;
    ImU32 color = 0;
    std::string name;
};

enum class sheet_event : int {
    none,
    save,
    travel,
    close,
};

using clock = std::chrono::steady_clock;
} // namespace

static bool travel_confirmed_flag = false;
// the legend stays as the player left it
static bool legend_shown = true;

static constexpr ImU32 col_glyph_box = IM_COL32( 5, 6, 5, 255 );
static constexpr ImU32 col_glyph_line = IM_COL32( 255, 255, 255, 15 );
static constexpr ImU32 col_pill = IM_COL32( 10, 12, 9, 214 );
static constexpr ImU32 col_field = IM_COL32( 9, 11, 8, 255 );
static constexpr ImU32 col_button_soft = IM_COL32( 255, 255, 255, 15 );
static constexpr ImU32 col_unexplored = IM_COL32( 95, 102, 90, 255 );
static constexpr ImU32 col_note = IM_COL32( 255, 216, 74, 255 );

static ImU32 with_alpha( const ImU32 color, const float alpha )
{
    const ImU32 a = static_cast<ImU32>( static_cast<float>( ( color >> IM_COL32_A_SHIFT ) & 0xFF ) *
                                        alpha );
    return ( color & ~IM_COL32_A_MASK ) | ( a << IM_COL32_A_SHIFT );
}

static ImU32 to_u32( const nc_color &color )
{
    return ImGui::ColorConvertFloat4ToU32( cataimgui::imvec4_from_color( color ) );
}

static place place_at( const tripoint_abs_omt &pos )
{
    place ret;
    const om_vision_level vision = overmap_buffer.seen( pos );
    if( vision == om_vision_level::unseen ) {
        ret.sym = "?";
        ret.color = col_unexplored;
        ret.name = _( "Unexplored" );
        return ret;
    }
    ret.seen = true;
    const oter_t &ter = overmap_buffer.ter( pos ).obj();
    if( ter.blends_adjacent( vision ) ) {
        const oter_vision::blended_omt info = oter_vision::get_blended_omt_info( pos, vision );
        ret.sym = info.sym;
        ret.color = to_u32( info.color );
        ret.name = info.name;
    } else {
        ret.sym = ter.get_symbol( vision );
        ret.color = to_u32( ter.get_color( vision ) );
        ret.name = ter.get_name( vision );
    }
    ret.name = uppercase_first_letter( ret.name );
    return ret;
}

static map_geometry overmap_geometry()
{
    map_geometry geo;
    if( !g || !g->w_overmap ) {
        return geo;
    }
    const window_dimensions dim = get_window_dimensions( g->w_overmap );
    const int scale = std::max( get_scaling_factor(), 1 );
    geo.win_pos = dim.window_pos_pixel;
    geo.win_size = dim.window_size_pixel;
    geo.tile = dim.scaled_font_size;
    if( scale > 1 ) {
        geo.tile = point( geo.tile.x / scale, geo.tile.y / scale );
        if( !( use_tiles && use_tiles_overmap ) ) {
            geo.win_size = point( geo.win_size.x / scale, geo.win_size.y / scale );
        }
    }
    if( geo.tile.x <= 0 || geo.tile.y <= 0 ) {
        return geo;
    }
    geo.center = point( geo.win_size.x / geo.tile.x / 2, geo.win_size.y / geo.tile.y / 2 );
    geo.valid = true;
    return geo;
}

// The tile under a point of the screen, relative to the cursor.
static std::optional<point> tile_at( const map_geometry &geo, const ImVec2 &pos )
{
    if( !geo.valid ) {
        return std::nullopt;
    }
    const point screen_pos( static_cast<int>( pos.x ) - geo.win_pos.x,
                            static_cast<int>( pos.y ) - geo.win_pos.y );
    if( screen_pos.x < 0 || screen_pos.y < 0 || screen_pos.x >= geo.win_size.x ||
        screen_pos.y >= geo.win_size.y ) {
        return std::nullopt;
    }
    return cata_tiles::screen_to_player( screen_pos, geo.tile, geo.win_size, point_bub_ms::zero,
                                         false ).raw();
}

// Top left corner on screen of the tile at the given offset from the cursor.
static ImVec2 tile_corner( const map_geometry &geo, const point &rel )
{
    return ImVec2( static_cast<float>( geo.win_pos.x + ( rel.x + geo.center.x ) * geo.tile.x ),
                   static_cast<float>( geo.win_pos.y + ( rel.y + geo.center.y ) * geo.tile.y ) );
}

static std::string level_text( const int z )
{
    if( z == 0 ) {
        return _( "ground level" );
    }
    //~ %d: z-level of the overmap being shown, for example -1 for the first basement level
    return string_format( _( "level %d" ), z );
}

// "96 m NE" or "1.2 km NE", measured the way the canvas does: 24 m per tile.
static std::string distance_text( const tripoint_abs_omt &from, const tripoint_abs_omt &to )
{
    const point delta = ( to.xy() - from.xy() ).raw();
    if( delta == point::zero ) {
        return to.z() == from.z() ? _( "You are here" ) : level_text( to.z() );
    }
    const float meters = std::hypot( static_cast<float>( delta.x ),
                                     static_cast<float>( delta.y ) ) * 24.0f;
    const std::string dir = trim( direction_name_short( direction_from( delta ) ) );
    std::string ret;
    if( meters < 1000.0f ) {
        //~ %1$d: distance in meters, %2$s: compass direction, for example NE
        ret = string_format( _( "%1$d m %2$s" ), static_cast<int>( std::lround( meters ) ), dir );
    } else {
        //~ %1$.1f: distance in kilometers, %2$s: compass direction, for example NE
        ret = string_format( _( "%1$.1f km %2$s" ), meters / 1000.0f, dir );
    }
    if( to.z() != from.z() ) {
        ret += " · " + level_text( to.z() );
    }
    return ret;
}

// The text of a note without its "glyph:color;" prefix.
static std::string note_text_at( const tripoint_abs_omt &pos )
{
    if( !overmap_buffer.has_note( pos ) ) {
        return std::string();
    }
    const std::string &note = overmap_buffer.note( pos );
    const size_t offset = std::get<2>( overmap_ui::get_note_display_info( note ) );
    return trim( offset < note.size() ? note.substr( offset ) : note );
}

// The canvas's body font, for the text field.  mobile_ui keeps its fonts to
// itself, so it is looked up by file name here.
static ImFont *body_font()
{
    for( ImFont *const font : ImGui::GetIO().Fonts->Fonts ) {
        if( std::string_view( font->GetDebugName() ).substr( 0, 13 ) == "Barlow-Medium" ) {
            return font;
        }
    }
    return nullptr;
}

class view::impl : public cataimgui::window
{
    public:
        impl() : cataimgui::window( "MOBILE_OVERMAP",
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoBackground ) {}

        // --- touched since the last handle() ---
        // an upstream action asked for by a button
        std::string pending_action;
        sheet_event pending_sheet = sheet_event::none;
        // vertical drag over the map, in pixels, not yet turned into tiles
        float pan_pixels = 0.0f;
        clock::time_point last_touch;

        // --- tile sheet ---
        bool sheet_open = false;
        tripoint_abs_omt selected;
        std::string note_edit;
        bool note_field_active = false;

        // CHOOSE_DESTINATION actions still to send for "Travel here"
        int travel_steps = 0;

        std::string toast;
        clock::time_point toast_until;

        void open_sheet( const tripoint_abs_omt &pos );
        void close_sheet();
        void show_toast( const std::string &text );
        // How far a sideways swipe pans, in tiles.
        int swipe_step() const;

    protected:
        cataimgui::bounds get_bounds() override {
            // One pixel short of the screen.  A window that covers the overmap
            // completely would stop ui_manager from ever redrawing the map
            // (see ui_adaptor::invalidate_ui).
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            return { 0.0f, 0.0f, size.x, std::max( size.y - 1.0f, 2.0f ) };
        }
        void draw_controls() override;

    private:
        void draw_chrome( const ImVec2 &area_min, const ImVec2 &area_max, bool interactive );
        void draw_legend( ImDrawList *draw, const ImVec2 &area_min, const ImVec2 &area_max );
        void draw_sheet( const ImVec2 &origin, const ImVec2 &size );
        void refresh_cache( const tripoint_abs_omt &cursor, const map_geometry &geo );

        // what the pill and the legend say, worked out when the view moves
        tripoint_abs_omt cached_cursor = tripoint_abs_omt::invalid;
        point cached_tile;
        bool cache_stale = true;
        std::string pill;
        std::vector<legend_entry> legend;
};

void view::impl::open_sheet( const tripoint_abs_omt &pos )
{
    selected = pos;
    sheet_open = true;
    note_edit = overmap_buffer.has_note( pos ) ? overmap_buffer.note( pos ) : std::string();
    note_field_active = false;
}

void view::impl::close_sheet()
{
    sheet_open = false;
    note_field_active = false;
    cache_stale = true;
}

void view::impl::show_toast( const std::string &text )
{
    toast = text;
    toast_until = clock::now() + std::chrono::milliseconds( 2200 );
}

int view::impl::swipe_step() const
{
    const map_geometry geo = overmap_geometry();
    return geo.valid ? std::max( 3, geo.win_size.x / geo.tile.x / 4 ) : 3;
}

void view::impl::refresh_cache( const tripoint_abs_omt &cursor, const map_geometry &geo )
{
    if( !cache_stale && cursor == cached_cursor && geo.tile == cached_tile ) {
        return;
    }
    cache_stale = false;
    cached_cursor = cursor;
    cached_tile = geo.tile;

    const size_t notes = overmap_buffer.get_all_notes( cursor.z() ).size();
    pill = place_at( cursor ).name + " · " + level_text( cursor.z() ) + " · " +
           //~ %d: number of notes the player has put on this level of the map
           string_format( n_gettext( "%d note", "%d notes", notes ), static_cast<int>( notes ) );

    // The legend names the kinds of place that are on screen most.
    legend.clear();
    if( !geo.valid ) {
        return;
    }
    struct tally {
        int count = 0;
        legend_entry entry;
    };
    std::map<std::string, tally> seen;
    // every other tile is plenty to find what is common, and keeps this cheap
    const point reach( std::min( geo.center.x, 60 ), std::min( geo.center.y, 40 ) );
    for( int dy = -reach.y; dy <= reach.y; dy += 2 ) {
        for( int dx = -reach.x; dx <= reach.x; dx += 2 ) {
            const tripoint_abs_omt pos = cursor + point( dx, dy );
            const om_vision_level vision = overmap_buffer.seen( pos );
            if( vision == om_vision_level::unseen ) {
                continue;
            }
            const oter_t &ter = overmap_buffer.ter( pos ).obj();
            tally &entry = seen[ter.get_name( vision )];
            if( entry.count++ == 0 ) {
                entry.entry = { ter.get_symbol( vision ), to_u32( ter.get_color( vision ) ),
                                uppercase_first_letter( ter.get_name( vision ) )
                              };
            }
        }
    }
    std::vector<tally> sorted;
    sorted.reserve( seen.size() );
    for( const std::pair<const std::string, tally> &elem : seen ) {
        sorted.push_back( elem.second );
    }
    std::stable_sort( sorted.begin(), sorted.end(), []( const tally & a, const tally & b ) {
        return a.count > b.count;
    } );
    for( size_t i = 0; i < sorted.size() && i < 6; ++i ) {
        legend.push_back( sorted[i].entry );
    }
}

void view::impl::draw_legend( ImDrawList *const draw, const ImVec2 &area_min,
                              const ImVec2 &area_max )
{
    const float u = mobile_ui::unit();
    std::vector<legend_entry> chips = legend;
    chips.push_back( { "@", IM_COL32_WHITE, _( "You" ) } );
    chips.push_back( { "N", col_note, _( "Note" ) } );

    // chips wrap like text and the block sits on the bottom edge
    const float left = area_min.x + 10.0f * u;
    const float right = area_max.x - 70.0f * u;
    const float chip_h = 22.0f * u;
    const float gap = 4.0f * u;
    std::vector<ImVec2> spots;
    std::vector<float> widths;
    float x = left;
    float row = 0.0f;
    for( const legend_entry &chip : chips ) {
        const float w = 8.0f * u + mobile_ui::text_size( face::mono, 12.0f, chip.sym ).x + 5.0f * u +
                        mobile_ui::text_size( face::bold, 11.0f, chip.name ).x + 8.0f * u;
        if( x > left && x + w > right ) {
            x = left;
            row += 1.0f;
        }
        spots.emplace_back( x, row );
        widths.push_back( w );
        x += w + gap;
    }
    const float top = area_max.y - 10.0f * u - ( row + 1.0f ) * chip_h - row * gap;
    for( size_t i = 0; i < chips.size(); ++i ) {
        const ImVec2 pos( spots[i].x, top + spots[i].y * ( chip_h + gap ) );
        mobile_ui::box( draw, pos, ImVec2( pos.x + widths[i], pos.y + chip_h ), col_pill,
                        mobile_ui::col_line, 11.0f );
        const ImVec2 sym_size = mobile_ui::text_size( face::mono, 12.0f, chips[i].sym );
        mobile_ui::text( draw, face::mono, 12.0f, ImVec2( pos.x + 8.0f * u,
                         pos.y + ( chip_h - sym_size.y ) * 0.5f ), chips[i].color, chips[i].sym );
        const ImVec2 name_size = mobile_ui::text_size( face::bold, 11.0f, chips[i].name );
        mobile_ui::text( draw, face::bold, 11.0f, ImVec2( pos.x + 13.0f * u + sym_size.x,
                         pos.y + ( chip_h - name_size.y ) * 0.5f ), mobile_ui::col_fg, chips[i].name );
    }
}

void view::impl::draw_chrome( const ImVec2 &area_min, const ImVec2 &area_max,
                              const bool interactive )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();

    // the place under the center of the map, or the tile the sheet is about
    const ImVec2 pill_size = mobile_ui::text_size( face::bold, 12.0f, pill );
    const ImVec2 pill_pos( area_min.x + safe.x + 10.0f * u, area_min.y + 10.0f * u );
    const ImVec2 pill_max( pill_pos.x + pill_size.x + 20.0f * u, pill_pos.y + pill_size.y + 10.0f * u );
    mobile_ui::box( draw, pill_pos, pill_max, col_pill, mobile_ui::col_line,
                    ( pill_max.y - pill_pos.y ) * 0.5f / u );
    mobile_ui::text( draw, face::bold, 12.0f, ImVec2( pill_pos.x + 10.0f * u, pill_pos.y + 5.0f * u ),
                     mobile_ui::col_fg, pill );

    // zoom in, zoom out, center on you
    static const std::array<std::pair<const char *, const char *>, 3> buttons = { {
            { "plus", "zoom_in" }, { "minus", "zoom_out" }, { "target", "CENTER" }
        }
    };
    const float fab = 48.0f * u;
    const float fab_gap = 8.0f * u;
    const float fab_x = area_max.x - safe.z - 10.0f * u - fab;
    float fab_y = area_max.y - safe.w - 10.0f * u - 3.0f * fab - 2.0f * fab_gap;
    for( const std::pair<const char *, const char *> &button : buttons ) {
        const ImVec2 pos( fab_x, fab_y );
        bool held = false;
        if( interactive &&
            mobile_ui::pressable( std::string( "##om_fab_" ) + button.first, pos, ImVec2( fab, fab ),
                                  &held ) ) {
            pending_action = button.second;
        }
        mobile_ui::box( draw, pos, ImVec2( pos.x + fab, pos.y + fab ),
                        held ? mobile_ui::col_button_down : mobile_ui::col_panel, mobile_ui::col_line, 14.0f );
        mobile_ui::icon( draw, button.first, ImVec2( pos.x + 13.0f * u, pos.y + 13.0f * u ), 22.0f,
                         mobile_ui::col_fg );
        fab_y += fab + fab_gap;
    }

    if( legend_shown ) {
        draw_legend( draw, ImVec2( area_min.x + safe.x, area_min.y ),
                     ImVec2( area_max.x - safe.z, area_max.y - safe.w ) );
    }
}

// One of the two wide buttons at the bottom of the tile sheet.
static bool sheet_button( ImDrawList *const draw, const std::string &id, const ImVec2 &pos,
                          const ImVec2 &size, const std::string &label, const float label_size,
                          const bool primary, const bool enabled )
{
    bool held = false;
    const bool tapped = enabled && mobile_ui::pressable( id, pos, size, &held );
    const float alpha = enabled ? 1.0f : 0.4f;
    ImU32 fill = primary ? mobile_ui::col_accent : col_button_soft;
    if( held ) {
        fill = primary ? mobile_ui::col_accent_down : mobile_ui::col_button_down;
    }
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), with_alpha( fill, alpha ),
                    with_alpha( primary ? mobile_ui::col_accent : mobile_ui::col_line, alpha ), 12.0f );
    const ImVec2 text_size = mobile_ui::text_size( face::bold, label_size, label );
    mobile_ui::text( draw, face::bold, label_size, ImVec2( pos.x + ( size.x - text_size.x ) * 0.5f,
                     pos.y + ( size.y - text_size.y ) * 0.5f ),
                     with_alpha( primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, alpha ), label );
    return tapped;
}

void view::impl::draw_sheet( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );

    const float sheet_w = landscape ? std::min( 520.0f * u, size.x * 0.76f ) : size.x;
    const float sheet_h = ( 22.0f + 60.0f + 54.0f + 66.0f + 6.0f ) * u + safe.w;
    const ImVec2 pos( origin.x + ( size.x - sheet_w ) * 0.5f, screen_max.y - sheet_h );

    // dim the screen behind; a tap there closes the sheet
    draw->AddRectFilled( origin, screen_max, mobile_ui::col_dim );
    ImGui::SetNextItemAllowOverlap();
    if( mobile_ui::pressable( "##om_sheet_outside", origin, size ) ) {
        const ImVec2 at = ImGui::GetIO().MouseClickedPos[0];
        if( at.y < pos.y || at.x < pos.x || at.x > pos.x + sheet_w ) {
            pending_sheet = sheet_event::close;
        }
    }

    draw->AddRectFilled( pos, ImVec2( pos.x + sheet_w, screen_max.y ), mobile_ui::col_sheet,
                         22.0f * u, ImDrawFlags_RoundCornersTop );
    draw->AddRect( pos, ImVec2( pos.x + sheet_w, screen_max.y + 30.0f * u ), mobile_ui::col_line,
                   22.0f * u, ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    // grab handle
    draw->AddRectFilled( ImVec2( pos.x + sheet_w * 0.5f - 21.0f * u, pos.y + 9.0f * u ),
                         ImVec2( pos.x + sheet_w * 0.5f + 21.0f * u, pos.y + 14.0f * u ),
                         IM_COL32( 255, 255, 255, 71 ), 3.0f * u );

    // glyph, name, and where it is
    const place here = place_at( selected );
    float y = pos.y + 26.0f * u;
    const ImVec2 glyph_pos( pos.x + 16.0f * u, y );
    mobile_ui::box( draw, glyph_pos, ImVec2( glyph_pos.x + 46.0f * u, glyph_pos.y + 46.0f * u ),
                    col_glyph_box, col_glyph_line, 12.0f );
    const ImVec2 sym_size = mobile_ui::text_size( face::mono, 24.0f, here.sym );
    mobile_ui::text( draw, face::mono, 24.0f, ImVec2( glyph_pos.x + ( 46.0f * u - sym_size.x ) * 0.5f,
                     glyph_pos.y + ( 46.0f * u - sym_size.y ) * 0.5f ), here.color, here.sym );
    const float text_x = glyph_pos.x + 58.0f * u;
    const float text_w = sheet_w - 90.0f * u;
    std::string where = distance_text( get_avatar().pos_abs_omt(), selected );
    const std::string note = note_text_at( selected );
    if( !note.empty() ) {
        where += " · " + note;
    }
    draw->PushClipRect( ImVec2( text_x, y ), ImVec2( text_x + text_w, y + 46.0f * u ), true );
    mobile_ui::text( draw, face::display, 20.0f, ImVec2( text_x, y + 3.0f * u ), mobile_ui::col_fg,
                     here.name );
    mobile_ui::text( draw, face::body, 12.5f, ImVec2( text_x, y + 27.0f * u ), mobile_ui::col_muted,
                     where );
    draw->PopClipRect();
    y += 56.0f * u;

    // the note field and Save
    const std::string save_label = _( "Save" );
    const float save_w = mobile_ui::text_size( face::bold, 13.5f, save_label ).x + 28.0f * u;
    const float field_w = sheet_w - 32.0f * u - 8.0f * u - save_w;
    const float field_h = 44.0f * u;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float font_scale = std::max( style.FontScaleMain * style.FontScaleDpi, 0.01f );
    ImGui::PushFont( body_font(), 15.0f * u / font_scale );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 11.0f * u );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, u ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding,
                         ImVec2( 12.0f * u, std::max( 0.0f, ( field_h - ImGui::GetFontSize() ) * 0.5f ) ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, col_field );
    ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, col_field );
    ImGui::PushStyleColor( ImGuiCol_FrameBgActive, col_field );
    ImGui::PushStyleColor( ImGuiCol_Border,
                           note_field_active ? mobile_ui::col_accent : mobile_ui::col_line );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
    ImGui::SetCursorScreenPos( ImVec2( pos.x + 16.0f * u, y ) );
    ImGui::SetNextItemWidth( field_w );
    const bool had_note = overmap_buffer.has_note( selected );
    const bool entered = ImGui::InputTextWithHint( "##om_note",
                         had_note ? _( "Edit note" ) : _( "Add a note" ), &note_edit,
                         ImGuiInputTextFlags_EnterReturnsTrue );
    note_field_active = ImGui::IsItemActive();
    ImGui::PopStyleColor( 6 );
    ImGui::PopStyleVar( 3 );
    ImGui::PopFont();
    if( sheet_button( draw, "##om_note_save", ImVec2( pos.x + sheet_w - 16.0f * u - save_w,
                      y + 2.0f * u ), ImVec2( save_w, 40.0f * u ), save_label, 13.5f, false, true ) ||
        entered ) {
        pending_sheet = sheet_event::save;
    }
    y += 58.0f * u;

    // Travel here and Close
    const float cell_w = ( sheet_w - 32.0f * u - 8.0f * u ) * 0.5f;
    const bool can_travel = here.seen && selected.xy() != get_avatar().pos_abs_omt().xy();
    if( sheet_button( draw, "##om_travel", ImVec2( pos.x + 16.0f * u, y ), ImVec2( cell_w, 48.0f * u ),
                      _( "Travel here" ), 14.0f, true, can_travel ) ) {
        pending_sheet = sheet_event::travel;
    }
    if( sheet_button( draw, "##om_close", ImVec2( pos.x + 24.0f * u + cell_w, y ),
                      ImVec2( cell_w, 48.0f * u ), _( "Close" ), 14.0f, false, true ) ) {
        pending_sheet = sheet_event::close;
    }
}

void view::impl::draw_controls()
{
    if( !g ) {
        return;
    }
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    // the window is a pixel short of the screen, the drawing is not
    draw->PushClipRectFullScreen();
    const bool interactive = !sheet_open;
    const bool touching = io.MouseSource == ImGuiMouseSource_TouchScreen;
    // The touch layer turns a vertical drag into wheel notches of five lines of
    // text each (see touch_input::finger_motion); this is that drag in pixels.
    const float wheel_pixels = io.MouseWheel * 5.0f * std::max( ImGui::GetFontSize(), 1.0f );
    if( touching && ImGui::IsMousePosValid() ) {
        last_touch = clock::now();
    }

    const map_geometry geo = overmap_geometry();
    const tripoint_abs_omt cursor = g->overmap_data.cursor_pos;
    refresh_cache( sheet_open ? selected : cursor, geo );

    // app bar, opaque over the top edge of the map
    const float bar_bottom = origin.y + safe.y + 54.0f * u;
    draw->AddRectFilled( origin, ImVec2( screen_max.x, bar_bottom ), mobile_ui::col_screen );
    ImGui::BeginDisabled( !interactive );
    if( mobile_ui::app_bar( _( "Map" ), ImVec2( origin.x + safe.x, origin.y + safe.y ),
                            size.x - safe.x - safe.z, nullptr ) ) {
        pending_action = "QUIT";
    }
    ImGui::EndDisabled();
    const ImVec2 legend_pos( screen_max.x - safe.z - 52.0f * u, origin.y + safe.y + 5.0f * u );
    bool legend_held = false;
    if( interactive &&
        mobile_ui::pressable( "##om_legend", legend_pos, ImVec2( 44.0f * u, 44.0f * u ),
                              &legend_held ) ) {
        legend_shown = !legend_shown;
    }
    if( legend_held ) {
        mobile_ui::box( draw, legend_pos, ImVec2( legend_pos.x + 44.0f * u, legend_pos.y + 44.0f * u ),
                        mobile_ui::col_fill_soft, 0, 12.0f );
    }
    mobile_ui::icon( draw, "book", ImVec2( legend_pos.x + 11.0f * u, legend_pos.y + 11.0f * u ), 22.0f,
                     legend_shown ? mobile_ui::col_accent : mobile_ui::col_fg );

    const ImVec2 area_min( origin.x, bar_bottom );
    const ImVec2 area_max = screen_max;
    const ImVec2 area_size( area_max.x - area_min.x, area_max.y - area_min.y );

    // the map itself: a tap picks a tile, a drag pans
    if( interactive ) {
        ImGui::SetNextItemAllowOverlap();
        if( mobile_ui::pressable( "##om_map", area_min, area_size ) ) {
            if( const std::optional<point> rel = tile_at( geo, io.MouseClickedPos[0] ) ) {
                open_sheet( cursor + *rel );
                refresh_cache( selected, geo );
            }
        }
        if( wheel_pixels != 0.0f && touching && io.MousePos.y >= area_min.y ) {
            pan_pixels += wheel_pixels;
        }
    }

    draw_chrome( area_min, area_max, interactive );

    if( sheet_open ) {
        // outline the tile the sheet is about, under the sheet
        if( geo.valid && selected.z() == cursor.z() ) {
            const ImVec2 corner = tile_corner( geo, ( selected.xy() - cursor.xy() ).raw() );
            draw->PushClipRect( area_min, area_max, true );
            draw->AddRect( ImVec2( corner.x - 1.0f, corner.y - 1.0f ),
                           ImVec2( corner.x + static_cast<float>( geo.tile.x ) + 1.0f,
                                   corner.y + static_cast<float>( geo.tile.y ) + 1.0f ),
                           mobile_ui::col_accent, 0.0f, 0, std::max( 2.0f, u ) );
            draw->PopClipRect();
        }
        draw_sheet( origin, size );
    }

    if( !toast.empty() && clock::now() < toast_until ) {
        const ImVec2 text_size = mobile_ui::text_size( face::bold, 13.0f, toast );
        const ImVec2 pos( origin.x + ( size.x - text_size.x ) * 0.5f - 14.0f * u,
                          area_min.y + 44.0f * u );
        mobile_ui::box( draw, pos, ImVec2( pos.x + text_size.x + 28.0f * u,
                                           pos.y + text_size.y + 16.0f * u ), mobile_ui::col_sheet, mobile_ui::col_line, 8.0f );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( pos.x + 14.0f * u, pos.y + 8.0f * u ),
                         mobile_ui::col_fg, toast );
    }
    draw->PopClipRect();
}

view::view() : pimpl( std::make_unique<impl>() ) {}

view::~view()
{
    travel_confirmed_flag = false;
}

// Nothing for upstream's loop to do; unlike a made-up action it does not ask
// for the map to be drawn again.
static const std::string idle_action = "TIMEOUT";

std::string view::handle( const std::string &action, input_context &ctxt,
                          tripoint_abs_omt &cursor )
{
    impl &v = *pimpl;
    travel_confirmed_flag = false;

    if( v.sheet_open ) {
        sheet_event event = v.pending_sheet;
        v.pending_sheet = sheet_event::none;
        // The back button closes the sheet.  While the note is being typed
        // every key arrives here as some action of the map, "q" for QUIT among
        // them, so only the real Escape or back key counts then.
        if( action == "QUIT" && ( !v.note_field_active ||
                                  ctxt.get_raw_input().get_first_input() == KEY_ESCAPE ) ) {
            event = sheet_event::close;
        }
        switch( event ) {
            case sheet_event::save: {
                const std::string text = trim( v.note_edit );
                if( text.empty() ) {
                    if( overmap_buffer.has_note( v.selected ) ) {
                        overmap_buffer.delete_note( v.selected );
                        v.show_toast( _( "Note removed" ) );
                    }
                } else {
                    overmap_buffer.add_note( v.selected, text );
                    v.show_toast( _( "Note saved" ) );
                }
                v.close_sheet();
                // the note is drawn by the map
                return "ANY_INPUT";
            }
            case sheet_event::travel:
                cursor = v.selected;
                v.close_sheet();
                v.travel_steps = 2;
                break;
            case sheet_event::close:
                v.close_sheet();
                return idle_action;
            case sheet_event::none:
                return idle_action;
        }
    }

    // "Travel here" is upstream's CHOOSE_DESTINATION on the tile, twice: the
    // first plans the route, the second sets off along it.
    if( v.travel_steps > 0 ) {
        if( v.travel_steps == 1 && get_avatar().omt_path.empty() ) {
            v.travel_steps = 0;
            v.show_toast( _( "There is no way to get there from here." ) );
            return idle_action;
        }
        --v.travel_steps;
        travel_confirmed_flag = true;
        return "CHOOSE_DESTINATION";
    }

    if( !v.pending_action.empty() ) {
        std::string ret;
        ret.swap( v.pending_action );
        return ret;
    }

    // a vertical drag: the map follows the finger
    const map_geometry geo = overmap_geometry();
    if( v.pan_pixels != 0.0f && geo.valid ) {
        const int tiles = static_cast<int>( v.pan_pixels / static_cast<float>( geo.tile.y ) );
        if( tiles != 0 ) {
            v.pan_pixels -= static_cast<float>( tiles * geo.tile.y );
            cursor.y() -= tiles;
            return idle_action;
        }
    }

    // A sideways swipe arrives as an arrow key once the finger is up.  Pan as
    // if the finger had dragged the map along; keys of a real keyboard keep
    // moving the cursor the way upstream does.
    if( ( action == "LEFT" || action == "RIGHT" ) &&
        clock::now() - v.last_touch < std::chrono::milliseconds( 600 ) ) {
        cursor.x() += action == "LEFT" ? v.swipe_step() : -v.swipe_step();
        v.pan_pixels = 0.0f;
        return idle_action;
    }

    // taps are read from ImGui, not from the mouse actions of the map
    if( action == "SELECT" || action == "MOUSE_MOVE" ) {
        return idle_action;
    }
    return action;
}

bool travel_confirmed()
{
    return travel_confirmed_flag;
}

#else // !TILES

class view::impl
{
};

view::view() = default;

view::~view() = default;

std::string view::handle( const std::string &action, input_context &, tripoint_abs_omt & )
{
    return action;
}

bool travel_confirmed()
{
    return false;
}

#endif // TILES

} // namespace mobile_overmap
