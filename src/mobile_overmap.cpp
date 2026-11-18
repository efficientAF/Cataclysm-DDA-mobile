#include "mobile_overmap.h"

#include "coordinates.h"
#include "mobile_list.h"
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
#include "clzones.h"
#include "color.h"
#include "cursesport.h"
#include "game.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input.h"
#include "input_context.h"
#include "line.h"
#include "map.h"
#include "mobile_entry.h"
#include "mobile_ui.h"
#include "omdata.h"
#include "output.h"
#include "overmapbuffer.h"
#include "point.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "touch_input.h"
#include "translations.h"
#include "ui_manager.h"
#include "uistate.h"
#endif

namespace mobile_overmap
{

bool wanted( const overmap_ui::overmap_draw_data_t &data )
{
#if defined(TILES)
    // "Pick a spot" prompts, a zone and an NPC's path get the same chrome; see
    // pick_mode() below for what changes.
    return mobile_menu::enabled() && !data.debug_editor && !data.debug_scent;
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
    choose,
    close,
};

using clock = std::chrono::steady_clock;
} // namespace

static bool travel_confirmed_flag = false;
// the legend stays as the player left it
static bool legend_shown = true;
// The place a search result or a note of the list took the map to; it gets a
// ring until the player touches the map again.
static tripoint_abs_omt marked_place = tripoint_abs_omt::invalid;
// it was set since the map last had a look
static bool mark_fresh = false;

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

// True while the map was opened to have a place chosen on it
// (ui::omap::choose_point): the tile sheet then offers the choice, not travel.
static bool pick_mode()
{
    return g && ( !g->overmap_data.message.empty() || g->overmap_data.distance != INT_MAX );
}

// Upstream's rule for a place that may be chosen, see the end of overmap_ui::display.
static bool in_pick_range( const tripoint_abs_omt &pos )
{
    return overmap_buffer.distance_limit( g->overmap_data.distance, g->overmap_data.origin_pos, pos );
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
        // drag over the map, in pixels, not yet turned into tiles
        ImVec2 pan_pixels = ImVec2( 0.0f, 0.0f );
        // how much of the drag in progress pan_pixels has had, and whether it
        // went far enough to not be a tap
        ImVec2 drag_seen = ImVec2( 0.0f, 0.0f );
        bool map_dragged = false;
        // Frames drawn since the overmap's loop last ran.  More than one means
        // that a sheet, a card or a question is waiting on top of the map.
        int frames_since_handle = 0;
        // the action handed to the overmap's loop may open one
        bool expect_nested = false;
        std::string handle( const std::string &action, input_context &ctxt,
                            tripoint_abs_omt &cursor );
        // height of the app bar over the map, in pixels
        float bar_pixels = 0.0f;
        // the player has been put in the middle of the visible map once
        bool centered = false;
        int last_zoom = 0;
        // Rows from the cursor, which upstream keeps at the center of the
        // screen, down to the middle of the map below the app bar.
        int focus_rows( const map_geometry &geo ) const;

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
        // the pill and the legend are worked out again
        void forget_cache() {
            cache_stale = true;
        }
        // The "More" sheet: what the map shows, and missions.  Returns the
        // action upstream's loop should handle, or an empty string.
        std::string more_sheet( const tripoint_abs_omt &focus );

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
        void draw_marks( ImDrawList *draw, const map_geometry &geo, const ImVec2 &area_min,
                         const ImVec2 &area_max );
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

std::string view::impl::more_sheet( const tripoint_abs_omt &focus )
{
    //~ %1$s: something the map can show, %2$s: "On" or "Off"
    const auto with_state = []( const std::string & name, const bool on ) {
        return mobile_ui::sheet_row{ std::string(), string_format( _( "%1$s: %2$s" ), name, on ? _( "On" ) : _( "Off" ) ), std::string() };
    };
    expect_nested = true;
    // the sheet comes back after a switch, so that several can be set
    while( true ) {
        // the map shows what was switched, under the sheet
        if( g->overmap_data.ui ) {
            g->overmap_data.ui->invalidate_ui();
        }
        mobile_ui::sheet_spec spec;
        spec.title = _( "Map" );
        spec.grid = true;
        // "Mark explored" is about the place in the middle of the map, which
        // the pill names
        spec.rows = {
            { std::string(), _( "Missions" ), std::string() },
            with_state( _( "Legend" ), legend_shown ),
            with_state( _( "Notes" ), uistate.overmap_show_map_notes ),
            with_state( _( "City names" ), uistate.overmap_show_city_labels ),
            with_state( _( "Hordes" ), uistate.overmap_show_hordes ),
            with_state( _( "Revealed places" ), uistate.overmap_show_revealed_omts ),
            with_state( _( "Forest trails" ), uistate.overmap_show_forest_trails ),
            with_state( _( "Weather" ), uistate.overmap_visible_weather ),
            {
                std::string(), overmap_buffer.is_explored( focus ) ? _( "Mark not explored" ) :
                _( "Mark explored" ), std::string()
            },
        };
        const int choice = mobile_ui::show_sheet( spec );
        if( choice < 0 || choice > 8 ) {
            return std::string();
        }
        if( choice == 0 ) {
            return "MISSIONS";
        }
        if( choice == 1 ) {
            legend_shown = !legend_shown;
        } else if( choice == 2 ) {
            uistate.overmap_show_map_notes = !uistate.overmap_show_map_notes;
        } else if( choice == 3 ) {
            uistate.overmap_show_city_labels = !uistate.overmap_show_city_labels;
        } else if( choice == 4 ) {
            uistate.overmap_show_hordes = !uistate.overmap_show_hordes;
        } else if( choice == 5 ) {
            uistate.overmap_show_revealed_omts = !uistate.overmap_show_revealed_omts;
        } else if( choice == 6 ) {
            uistate.overmap_show_forest_trails = !uistate.overmap_show_forest_trails;
        } else if( choice == 7 ) {
            // upstream's own rule and action, see overmap_ui::display
            if( !uistate.overmap_visible_weather && !get_map().is_outside( get_avatar().pos_bub() ) ) {
                show_toast( _( "The weather can only be seen from outside." ) );
                return std::string();
            }
            return "TOGGLE_OVERMAP_WEATHER";
        } else {
            overmap_buffer.toggle_explored( focus );
        }
    }
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

int view::impl::focus_rows( const map_geometry &geo ) const
{
    return geo.valid ? static_cast<int>( std::lround( bar_pixels * 0.5f /
                                         static_cast<float>( geo.tile.y ) ) ) : 0;
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
    // up to the two columns of buttons
    const float right = area_max.x - 126.0f * u;
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

// What upstream's letter map marks and the tile map does not: the edge of the
// range a place may be chosen in, and the zone the map was opened to show.
void view::impl::draw_marks( ImDrawList *const draw, const map_geometry &geo,
                             const ImVec2 &area_min, const ImVec2 &area_max )
{
    if( !geo.valid ) {
        return;
    }
    const overmap_ui::overmap_draw_data_t &data = g->overmap_data;
    const tripoint_abs_omt cursor = data.cursor_pos;
    const ImVec2 tile( static_cast<float>( geo.tile.x ), static_cast<float>( geo.tile.y ) );
    draw->PushClipRect( area_min, area_max, true );
    if( data.distance != INT_MAX && data.distance >= 0 && cursor.z() == data.origin_pos.z() ) {
        const point from( std::max( -geo.center.x - 1,
                                    data.origin_pos.x() - data.distance - cursor.x() ),
                          std::max( -geo.center.y - 1, data.origin_pos.y() - data.distance - cursor.y() ) );
        const point to( std::min( geo.center.x + 1, data.origin_pos.x() + data.distance - cursor.x() ),
                        std::min( geo.center.y + 1, data.origin_pos.y() + data.distance - cursor.y() ) );
        for( int dy = from.y; dy <= to.y; ++dy ) {
            for( int dx = from.x; dx <= to.x; ++dx ) {
                if( !overmap_buffer.distance_limit_line( data.distance, data.origin_pos,
                        cursor + point( dx, dy ) ) ) {
                    continue;
                }
                const ImVec2 corner = tile_corner( geo, point( dx, dy ) );
                draw->AddRectFilled( corner, ImVec2( corner.x + tile.x, corner.y + tile.y ),
                                     IM_COL32( 255, 96, 80, 96 ) );
            }
        }
    }
    const std::vector<zone_manager::ref_zone_data> zones = zone_manager::get_manager().get_zones();
    if( data.iZoneIndex >= 0 && static_cast<size_t>( data.iZoneIndex ) < zones.size() ) {
        const zone_data &zone = zones[data.iZoneIndex].get();
        const tripoint_abs_omt a = project_to<coords::omt>( zone.get_start_point() );
        const tripoint_abs_omt b = project_to<coords::omt>( zone.get_end_point() );
        if( a.z() <= cursor.z() && b.z() >= cursor.z() ) {
            const ImVec2 min = tile_corner( geo, point( std::min( a.x(), b.x() ) - cursor.x(),
                                            std::min( a.y(), b.y() ) - cursor.y() ) );
            const ImVec2 max = tile_corner( geo, point( std::max( a.x(), b.x() ) - cursor.x() + 1,
                                            std::max( a.y(), b.y() ) - cursor.y() + 1 ) );
            draw->AddRect( ImVec2( min.x - 2.0f, min.y - 2.0f ), ImVec2( max.x + 2.0f, max.y + 2.0f ),
                           mobile_ui::col_accent, 0.0f, 0, std::max( 2.0f, mobile_ui::unit() * 1.5f ) );
        }
    }
    draw->PopClipRect();
}

void view::impl::draw_chrome( const ImVec2 &area_min, const ImVec2 &area_max,
                              const bool interactive )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();

    // what the map was opened to ask, as a banner
    float banner_bottom = area_min.y;
    std::string banner = g->overmap_data.message;
    const std::vector<zone_manager::ref_zone_data> zones = zone_manager::get_manager().get_zones();
    if( banner.empty() && g->overmap_data.iZoneIndex >= 0 &&
        static_cast<size_t>( g->overmap_data.iZoneIndex ) < zones.size() ) {
        //~ %s: the name of a zone that is outlined on the map
        banner = string_format( _( "Zone: %s" ), zones[g->overmap_data.iZoneIndex].get().get_name() );
    }
    if( !banner.empty() ) {
        if( pick_mode() ) {
            banner += "\n";
            banner += _( "Tap a place, then \"Choose this place\"." );
        }
        const float wrap = std::min( area_max.x - area_min.x - safe.x - safe.z - 44.0f * u, 560.0f * u );
        const ImVec2 text_size = mobile_ui::text_size( face::bold, 13.5f, banner, wrap );
        const ImVec2 pos( area_min.x + safe.x + 10.0f * u, area_min.y + 10.0f * u );
        const ImVec2 max( pos.x + text_size.x + 24.0f * u, pos.y + text_size.y + 16.0f * u );
        mobile_ui::box( draw, pos, max, col_pill, mobile_ui::col_accent, 12.0f );
        mobile_ui::text( draw, face::bold, 13.5f, ImVec2( pos.x + 12.0f * u, pos.y + 8.0f * u ),
                         mobile_ui::col_fg, banner, wrap );
        banner_bottom = max.y - 4.0f * u;
    }

    // the place under the center of the map, or the tile the sheet is about
    const ImVec2 pill_size = mobile_ui::text_size( face::bold, 12.0f, pill );
    const ImVec2 pill_pos( area_min.x + safe.x + 10.0f * u, banner_bottom + 10.0f * u );
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
    const float fab_top = area_max.y - safe.w - 10.0f * u - 3.0f * fab - 2.0f * fab_gap;
    float fab_y = fab_top;
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

    // a second column beside it: one level up, the level shown, one level down
    const int z = g->overmap_data.cursor_pos.z();
    const float level_x = fab_x - fab_gap - fab;
    for( int i = 0; i < 2; ++i ) {
        const bool up = i == 0;
        const bool possible = up ? z < OVERMAP_HEIGHT : z > -OVERMAP_DEPTH;
        const ImVec2 pos( level_x, fab_top + ( up ? 0.0f : 2.0f * ( fab + fab_gap ) ) );
        bool held = false;
        if( interactive && possible &&
            mobile_ui::pressable( up ? "##om_fab_up" : "##om_fab_down", pos, ImVec2( fab, fab ), &held ) ) {
            pending_action = up ? "LEVEL_UP" : "LEVEL_DOWN";
        }
        mobile_ui::box( draw, pos, ImVec2( pos.x + fab, pos.y + fab ),
                        held ? mobile_ui::col_button_down : mobile_ui::col_panel, mobile_ui::col_line, 14.0f );
        // a chevron; the icon set has none that points up or down
        const ImVec2 mid( pos.x + fab * 0.5f, pos.y + fab * 0.5f );
        const float dy = ( up ? -1.0f : 1.0f ) * 4.0f * u;
        const std::array<ImVec2, 3> chevron = { {
                ImVec2( mid.x - 8.0f * u, mid.y - dy ), ImVec2( mid.x, mid.y + dy ),
                ImVec2( mid.x + 8.0f * u, mid.y - dy )
            }
        };
        draw->AddPolyline( chevron.data(), 3, with_alpha( mobile_ui::col_fg, possible ? 1.0f : 0.3f ),
                           ImDrawFlags_None, 2.2f * u );
    }
    {
        const ImVec2 pos( level_x, fab_top + fab + fab_gap );
        mobile_ui::box( draw, pos, ImVec2( pos.x + fab, pos.y + fab ), col_pill, mobile_ui::col_line,
                        14.0f );
        //~ Label over the number of the level the map shows; keep it very short
        const std::string caption = _( "LEVEL" );
        const ImVec2 caption_size = mobile_ui::text_size( face::mono, 8.5f, caption );
        mobile_ui::text( draw, face::mono, 8.5f, ImVec2( pos.x + ( fab - caption_size.x ) * 0.5f,
                         pos.y + 7.0f * u ), mobile_ui::col_muted, caption );
        const std::string number = z > 0 ? string_format( "+%d", z ) : std::to_string( z );
        const ImVec2 number_size = mobile_ui::text_size( face::bold, 17.0f, number );
        mobile_ui::text( draw, face::bold, 17.0f, ImVec2( pos.x + ( fab - number_size.x ) * 0.5f,
                         pos.y + 19.0f * u ), z == 0 ? mobile_ui::col_fg : mobile_ui::col_accent, number );
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
    const bool picking = pick_mode();
    const bool in_range = !picking || in_pick_range( selected );
    // picking a place: no note field, and a line saying why not if it is too far
    const float middle_h = picking ? ( in_range ? 0.0f : 30.0f ) : 54.0f;
    const float sheet_h = ( 22.0f + 60.0f + middle_h + 66.0f + 6.0f ) * u + safe.w;
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

    if( picking ) {
        if( !in_range ) {
            mobile_ui::text( draw, face::bold, 13.0f, ImVec2( pos.x + 16.0f * u, y ),
                             IM_COL32( 255, 120, 100, 255 ),
                             //~ %d: the largest distance allowed, in overmap tiles
                             string_format( n_gettext( "Too far away: it must be within %d map tile of the start.",
                                            "Too far away: it must be within %d map tiles of the start.",
                                            g->overmap_data.distance ), g->overmap_data.distance ) );
            y += 30.0f * u;
        }
        const float cell_w = ( sheet_w - 32.0f * u - 8.0f * u ) * 0.5f;
        if( sheet_button( draw, "##om_choose", ImVec2( pos.x + 16.0f * u, y ),
                          ImVec2( cell_w, 48.0f * u ), _( "Choose this place" ), 14.0f, true, in_range ) ) {
            pending_sheet = sheet_event::choose;
        }
        if( sheet_button( draw, "##om_close", ImVec2( pos.x + 24.0f * u + cell_w, y ),
                          ImVec2( cell_w, 48.0f * u ), _( "Close" ), 14.0f, false, true ) ) {
            pending_sheet = sheet_event::close;
        }
        return;
    }

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

    const map_geometry geo = overmap_geometry();
    const tripoint_abs_omt cursor = g->overmap_data.cursor_pos;

    // Something of its own is over the map (the search card, the "More" sheet,
    // a question).  Nothing else has the overmap drawn again then: the game's
    // own map, which those ask for, is hidden by the overmap.
    ++frames_since_handle;
    if( ( expect_nested || frames_since_handle > 1 ) && g->overmap_data.ui ) {
        g->overmap_data.ui->invalidate_ui();
    }

    // app bar, opaque over the top edge of the map
    const float bar_bottom = origin.y + safe.y + 54.0f * u;
    draw->AddRectFilled( origin, ImVec2( screen_max.x, bar_bottom ), mobile_ui::col_screen );
    ImGui::BeginDisabled( !interactive );
    if( mobile_ui::app_bar( pick_mode() ? _( "Choose a place" ) : _( "Map" ), ImVec2( origin.x + safe.x,
                            origin.y + safe.y ),
                            size.x - safe.x - safe.z, nullptr ) ) {
        pending_action = "QUIT";
    }
    ImGui::EndDisabled();
    // search, the list of notes, and the rest behind "More"
    static const std::array<std::pair<const char *, const char *>, 3> bar_buttons = { {
            { "search", "SEARCH" }, { "note", "LIST_NOTES" }, { "menu", "MORE" }
        }
    };
    float bar_x = screen_max.x - safe.z - 8.0f * u - 3.0f * 44.0f * u;
    for( const std::pair<const char *, const char *> &button : bar_buttons ) {
        const ImVec2 pos( bar_x, origin.y + safe.y + 5.0f * u );
        bool held = false;
        if( interactive &&
            mobile_ui::pressable( std::string( "##om_bar_" ) + button.first, pos,
                                  ImVec2( 44.0f * u, 44.0f * u ), &held ) ) {
            pending_action = button.second;
        }
        if( held ) {
            mobile_ui::box( draw, pos, ImVec2( pos.x + 44.0f * u, pos.y + 44.0f * u ),
                            mobile_ui::col_fill_soft, 0, 12.0f );
        }
        mobile_ui::icon( draw, button.first, ImVec2( pos.x + 11.0f * u, pos.y + 11.0f * u ), 22.0f,
                         mobile_ui::col_fg );
        bar_x += 44.0f * u;
    }

    const ImVec2 area_min( origin.x, bar_bottom );
    const ImVec2 area_max = screen_max;
    const ImVec2 area_size( area_max.x - area_min.x, area_max.y - area_min.y );
    bar_pixels = bar_bottom - origin.y;
    const tripoint_abs_omt focus = cursor + point( 0, focus_rows( geo ) );
    refresh_cache( sheet_open ? selected : focus, geo );

    // the map itself: a tap picks a tile, a drag pans; the touch layer hands
    // over drags as they are and takes pinches for the zoom
    if( interactive ) {
        touch_input::set_raw_drag_rect( area_min.x, area_min.y, area_size.x, area_size.y,
                                        touch_input::view_mode::overmap );
        ImGui::SetNextItemAllowOverlap();
        const bool released = mobile_ui::pressable( "##om_map", area_min, area_size );
        // the last move of a drag can arrive in the frame of its release
        const bool let_go = ImGui::IsItemDeactivated() && map_dragged && ImGui::IsMousePosValid();
        if( ImGui::IsItemActive() || let_go ) {
            const ImVec2 drag( io.MousePos.x - io.MouseClickedPos[0].x,
                               io.MousePos.y - io.MouseClickedPos[0].y );
            if( std::hypot( drag.x, drag.y ) > 6.0f * u ) {
                map_dragged = true;
            }
            if( map_dragged ) {
                pan_pixels.x += drag.x - drag_seen.x;
                pan_pixels.y += drag.y - drag_seen.y;
                drag_seen = drag;
            }
        }
        if( !ImGui::IsItemActive() ) {
            if( released && !map_dragged ) {
                if( const std::optional<point> rel = tile_at( geo, io.MouseClickedPos[0] ) ) {
                    marked_place = tripoint_abs_omt::invalid;
                    open_sheet( cursor + *rel );
                    refresh_cache( selected, geo );
                }
            }
            map_dragged = false;
            drag_seen = ImVec2( 0.0f, 0.0f );
        }
    } else {
        touch_input::clear_raw_drag_rect();
    }

    draw_marks( draw, geo, area_min, area_max );
    // where a search result or a note of the list is
    if( geo.valid && !marked_place.is_invalid() && marked_place.z() == cursor.z() ) {
        const ImVec2 corner = tile_corner( geo, ( marked_place.xy() - cursor.xy() ).raw() );
        const ImVec2 mid( corner.x + static_cast<float>( geo.tile.x ) * 0.5f,
                          corner.y + static_cast<float>( geo.tile.y ) * 0.5f );
        draw->PushClipRect( area_min, area_max, true );
        draw->AddCircle( mid, static_cast<float>( std::max( geo.tile.x, geo.tile.y ) ) * 0.5f + 6.0f * u,
                         mobile_ui::col_accent, 0, std::max( 2.0f, 2.0f * u ) );
        draw->PopClipRect();
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
    marked_place = tripoint_abs_omt::invalid;
    touch_input::clear_raw_drag_rect();
    travel_confirmed_flag = false;
}

// Nothing for upstream's loop to do; unlike a made-up action it does not ask
// for the map to be drawn again.
static const std::string idle_action = "TIMEOUT";

bool view::covers_map() const
{
    return pimpl->sheet_open || ( !pimpl->toast.empty() && clock::now() < pimpl->toast_until );
}

std::string view::handle( const std::string &action, input_context &ctxt,
                          tripoint_abs_omt &cursor )
{
    impl &v = *pimpl;
    v.frames_since_handle = 0;
    v.expect_nested = false;
    std::string ret = v.handle( action, ctxt, cursor );
    // Whatever upstream's loop does with it may put a card, a list or a
    // question over the map.  The map is drawn under it from its first frame
    // on; see draw_controls for the frames after that.
    if( ret != idle_action && ret != "ANY_INPUT" && ret != "QUIT" ) {
        v.expect_nested = true;
        if( g->overmap_data.ui ) {
            g->overmap_data.ui->invalidate_ui();
        }
    }
    return ret;
}

std::string view::impl::handle( const std::string &action, input_context &ctxt,
                                tripoint_abs_omt &cursor )
{
    impl &v = *this;
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
            case sheet_event::choose:
                // upstream returns the tile under the cursor, after its own range check
                cursor = v.selected;
                v.close_sheet();
                return "CONFIRM";
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

    const map_geometry geo = overmap_geometry();
    // Upstream keeps the cursor in the middle of the screen; the middle of the
    // map that shows below the app bar is a few rows further down.
    const tripoint_abs_omt centered_cursor = g->overmap_data.origin_pos +
            point( 0, -v.focus_rows( geo ) );
    if( !v.centered && geo.valid && v.bar_pixels > 0.0f ) {
        v.centered = true;
        if( cursor == g->overmap_data.origin_pos ) {
            cursor = centered_cursor;
        } else if( g->overmap_data.display_path.empty() ) {
            // opened on some other place, e.g. a zone
            cursor += point( 0, -v.focus_rows( geo ) );
        }
    }

    // a search result or a note was jumped to: upstream put it under the
    // cursor, it belongs in the middle of the map that shows
    if( mark_fresh ) {
        mark_fresh = false;
        if( !marked_place.is_invalid() ) {
            cursor = marked_place + point( 0, -v.focus_rows( geo ) );
            return "ANY_INPUT";
        }
    }

    if( !v.pending_action.empty() ) {
        std::string ret;
        ret.swap( v.pending_action );
        if( ret == "CENTER" ) {
            marked_place = tripoint_abs_omt::invalid;
            cursor = centered_cursor;
            return idle_action;
        }
        if( ret == "MORE" ) {
            ret = v.more_sheet( cursor + point( 0, v.focus_rows( geo ) ) );
            v.forget_cache();
            // the map is drawn again either way: what it shows may have changed
            return ret.empty() ? "ANY_INPUT" : ret;
        }
        if( ret == "SEARCH" || ret == "LIST_NOTES" ) {
            // upstream's code runs and hands the list to pick_search_result or
            // show_notes below; the note count of the pill may change
            v.forget_cache();
        }
        return ret;
    }

    // a pinch changed the zoom (touch_input::two_finger_flush): lay out again
    if( v.last_zoom != uistate.overmap_tileset_zoom ) {
        const bool first = v.last_zoom == 0;
        v.last_zoom = uistate.overmap_tileset_zoom;
        if( !first && g->overmap_data.ui ) {
            g->overmap_data.ui->mark_resize();
            return "ANY_INPUT";
        }
    }

    // a drag: the map follows the finger
    if( geo.valid ) {
        // to the nearest tile, so that the map is never most of a tile behind the finger
        const point tiles( static_cast<int>( std::lround( v.pan_pixels.x / static_cast<float>
                                             ( geo.tile.x ) ) ),
                           static_cast<int>( std::lround( v.pan_pixels.y / static_cast<float>
                                             ( geo.tile.y ) ) ) );
        if( tiles != point::zero ) {
            v.pan_pixels.x -= static_cast<float>( tiles.x * geo.tile.x );
            v.pan_pixels.y -= static_cast<float>( tiles.y * geo.tile.y );
            cursor += -tiles;
            return idle_action;
        }
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

int pick_search_result( const std::string &term, const std::vector<point_abs_omt> &locations,
                        const int z, const tripoint_abs_omt &from )
{
    // Nearest to the player first, as the distances in the list are from
    // there; upstream's order is from the middle of the map.
    std::vector<size_t> order( locations.size() );
    for( size_t i = 0; i < order.size(); ++i ) {
        order[i] = i;
    }
    std::stable_sort( order.begin(), order.end(), [&]( const size_t a, const size_t b ) {
        const point da = ( locations[a] - from.xy() ).raw();
        const point db = ( locations[b] - from.xy() ).raw();
        return da.x * da.x + da.y * da.y < db.x * db.x + db.y * db.y;
    } );
    // a common word finds thousands of tiles
    const size_t shown = std::min<size_t>( locations.size(), 200 );
    mobile_list::spec what;
    what.title = _( "Search results" );
    //~ %1$d: number of places found, %2$s: what was searched for
    what.header = string_format( n_gettext( "%1$d place matches \"%2$s\"",
                                            "%1$d places match \"%2$s\"", locations.size() ),
                                 static_cast<int>( locations.size() ), term );
    if( shown < locations.size() ) {
        //~ %d: number of search results in the list
        what.header += string_format( _( " · the nearest %d are listed" ), static_cast<int>( shown ) );
    }
    for( size_t i = 0; i < shown; ++i ) {
        const tripoint_abs_omt pos( locations[order[i]], z );
        const place here = place_at( pos );
        const std::string note = note_text_at( pos );
        mobile_list::row r;
        r.name = here.name;
        r.detail = distance_text( from, pos );
        if( !note.empty() ) {
            //~ %s: text of a note on the map
            r.detail += " · " + string_format( _( "Note: %s" ), note );
        }
        r.description = overmap_buffer.get_description_at( project_to<coords::sm>( pos ), false );
        r.action = _( "Go there" );
        what.rows.push_back( r );
    }
    int tab = 0;
    int selected = 0;
    const int chosen = mobile_list::show( what, tab, selected );
    if( chosen < 0 || static_cast<size_t>( chosen ) >= shown ) {
        return -1;
    }
    marked_place = tripoint_abs_omt( locations[order[chosen]], z );
    mark_fresh = true;
    return static_cast<int>( order[chosen] );
}

// The note's text as the player may edit it, in place of upstream's
// create_note, whose field sits inside a letter-grid preview.
static bool edit_note( const tripoint_abs_omt &pos )
{
    const std::string old_note = overmap_buffer.note( pos );
    mobile_entry::spec what;
    what.title = old_note.empty() ? _( "Add a note to the map." ) : _( "Edit an existing note." );
    what.description = string_format( "%s\n%s",
                                      _( "A letter and a colon first sets the note's symbol, a color letter and a semicolon its color." ),
                                      // NOLINTNEXTLINE(cata-text-style): literal exclamation mark
                                      _( "Examples: B:Base | g;Loot | !:R;Minefield" ) );
    what.text = old_note;
    const std::optional<std::string> entered = mobile_entry::query( what );
    if( !entered ) {
        return false;
    }
    if( entered->empty() ) {
        if( !old_note.empty() && query_yn( _( "Really delete note?" ) ) ) {
            overmap_buffer.delete_note( pos );
            return true;
        }
        return false;
    }
    if( *entered != old_note ) {
        overmap_buffer.add_note( pos, *entered );
        return true;
    }
    return false;
}

// Upstream's MARK_DANGER, see map_notes_callback::key in overmap_ui.cpp.
static void mark_danger( const tripoint_abs_omt &pos )
{
    const int danger_radius = overmap_buffer.note_danger_radius( pos );
    if( danger_radius >= 0 ) {
        if( query_yn( _( "Remove dangerous mark?" ) ) ) {
            overmap_buffer.mark_note_dangerous( pos, 0, false );
        }
        return;
    }
    const int max_amount = 20;
    int amount = 0;
    // NOLINTNEXTLINE(cata-text-style): No need for two whitespaces
    if( query_int( amount, true, _( "Danger radius in overmap squares? (0-%d)" ), max_amount ) &&
        amount >= 0 && amount <= max_amount ) {
        overmap_buffer.mark_note_dangerous( pos, amount, true );
    }
}

point_abs_omt show_notes( const tripoint_abs_omt &origin )
{
    const tripoint_abs_omt you = get_avatar().pos_abs_omt();
    int tab = 0;
    int selected = -1;
    bool first = true;
    while( true ) {
        const overmapbuffer::t_notes_vector notes = overmap_buffer.get_all_notes( origin.z() );
        mobile_list::spec what;
        //~ %d: number of notes
        what.title = string_format( _( "Map notes (%d)" ), static_cast<int>( notes.size() ) );
        what.header = uppercase_first_letter( level_text( origin.z() ) );
        what.empty_text =
            _( "No notes on this level of the map.  Tap a place on the map to write one." );
        for( size_t i = 0; i < notes.size(); ++i ) {
            const tripoint_abs_omt pos( notes[i].first, origin.z() );
            if( first && pos.xy() == origin.xy() ) {
                selected = static_cast<int>( i );
            }
            const std::string &note = notes[i].second;
            const std::tuple<char, nc_color, size_t> info = overmap_ui::get_note_display_info( note );
            const size_t offset = std::get<2>( info );
            const std::string note_text = trim( offset < note.size() ? note.substr( offset ) : note );
            const int danger_radius = overmap_buffer.note_danger_radius( pos );
            const std::string where = overmap_buffer.get_description_at( project_to<coords::sm>( pos ), false );
            mobile_list::row r;
            r.name = string_format( "[%c] %s", std::get<0>( info ), note_text );
            r.detail = distance_text( you, pos ) + " · " + where;
            std::string danger;
            if( danger_radius >= 0 ) {
                //~ %d: radius of the area around a note that is marked dangerous, in map tiles
                r.right = string_format( _( "Danger %d" ), danger_radius );
                danger = string_format( _( "DANGEROUS AREA!  (R=%d)" ), danger_radius );
            } else if( overmap_buffer.is_marked_dangerous( pos ) ) {
                danger = _( "IN DANGEROUS AREA!" );
            }
            r.description = note_text;
            if( !danger.empty() ) {
                r.description += "\n" + colorize( danger, c_red );
            }
            r.action = _( "Go there" );
            r.more_actions = {
                std::string( _( "Edit" ) ),
                std::string( danger_radius >= 0 ? _( "No danger" ) : _( "Danger" ) ),
                std::string( _( "Delete" ) )
            };
            what.rows.push_back( r );
        }
        first = false;
        const mobile_list::outcome done = mobile_list::show_all( what, tab, selected );
        if( done.row < 0 || static_cast<size_t>( done.row ) >= notes.size() ) {
            return point_abs_omt::invalid;
        }
        const tripoint_abs_omt pos( notes[done.row].first, origin.z() );
        switch( done.action ) {
            case 0:
                marked_place = pos;
                mark_fresh = true;
                return pos.xy();
            case 1:
                edit_note( pos );
                break;
            case 2:
                mark_danger( pos );
                break;
            default:
                if( query_yn( _( "Really delete note?" ) ) ) {
                    overmap_buffer.delete_note( pos );
                }
                break;
        }
    }
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

bool view::covers_map() const
{
    return false;
}

bool travel_confirmed()
{
    return false;
}

int pick_search_result( const std::string &, const std::vector<point_abs_omt> &, int,
                        const tripoint_abs_omt & )
{
    return -1;
}

point_abs_omt show_notes( const tripoint_abs_omt & )
{
    return point_abs_omt::invalid;
}

#endif // TILES

} // namespace mobile_overmap
