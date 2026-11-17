#include "mobile_pick.h"

#include "coordinates.h"
#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "avatar.h"
#include "cata_imgui.h"
#include "character.h"
#include "creature.h"
#include "creature_tracker.h"
#include "game.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "item.h"
#include "map.h"
#include "map_selector.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "vpart_position.h"
#include "vehicle.h"
#endif

namespace mobile_pick
{

bool wanted()
{
#if defined(TILES)
    return mobile_menu::enabled() && g != nullptr;
#else
    return false;
#endif
}

#if defined(TILES)

using mobile_ui::face;

// what the question allows, if the game said
static bool have_candidates = false;
static tripoint_bub_ms candidate_center;
static std::vector<tripoint_bub_ms> candidate_tiles;

candidates::candidates( const tripoint_bub_ms &center, const std::vector<tripoint_bub_ms> &valid )
{
    have_candidates = !valid.empty();
    candidate_center = center;
    candidate_tiles = valid;
}

candidates::~candidates()
{
    have_candidates = false;
    candidate_tiles.clear();
}

namespace
{
struct cell {
    // step from the player
    point step;
    bool allowed = true;
    // what is on that tile, for tiles that qualify
    std::string label;
};

enum class answer : int {
    none,
    cancel,
    step,
    up,
    down,
};

class ring_window : public cataimgui::window
{
    public:
        ring_window( const std::string &message, const bool allow_vertical ) :
            cataimgui::window( "MOBILE_PICK_DIRECTION",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            message( message ), allow_vertical( allow_vertical ) {
            build_cells();
        }
        answer tapped = answer::none;
        point tapped_step;
    protected:
        cataimgui::bounds get_bounds() override {
            // One pixel short of the screen, so that the map underneath keeps
            // being redrawn (see ui_adaptor::invalidate_ui).
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            return { 0.0f, 0.0f, size.x, std::max( size.y - 1.0f, 2.0f ) };
        }
        void draw_controls() override;
    private:
        void build_cells();
        std::string message;
        bool allow_vertical;
        std::array<cell, 9> cells;
};

// What a tile holds, in a word or two: who stands there, else the furniture,
// else the ground.
std::string tile_label( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    if( const Creature *const critter = get_creature_tracker().creature_at( pos ) ) {
        if( critter != &you && you.sees( here, *critter ) ) {
            return remove_color_tags( critter->get_name() );
        }
    }
    if( here.has_furn( pos ) ) {
        return here.furnname( pos );
    }
    return here.tername( pos );
}

void ring_window::build_cells()
{
    for( int i = 0; i < 9; ++i ) {
        cell &c = cells[static_cast<size_t>( i )];
        c.step = point( i % 3 - 1, i / 3 - 1 );
        if( have_candidates ) {
            const tripoint_bub_ms pos = candidate_center + tripoint_rel_ms( c.step.x, c.step.y, 0 );
            c.allowed = std::find( candidate_tiles.begin(), candidate_tiles.end(),
                                   pos ) != candidate_tiles.end();
            if( c.allowed ) {
                c.label = tile_label( pos );
            }
        }
    }
}

// An arrowhead pointing along step, or a dot for the player's own tile.
void draw_arrow( ImDrawList *const draw, const ImVec2 &center, const point &step, const float size,
                 const ImU32 color )
{
    if( step == point::zero ) {
        draw->AddCircleFilled( center, size * 0.28f, color, 20 );
        return;
    }
    const float length = std::hypot( static_cast<float>( step.x ), static_cast<float>( step.y ) );
    const ImVec2 dir( static_cast<float>( step.x ) / length, static_cast<float>( step.y ) / length );
    const ImVec2 side( -dir.y, dir.x );
    const ImVec2 tip( center.x + dir.x * size * 0.5f, center.y + dir.y * size * 0.5f );
    const ImVec2 back( center.x - dir.x * size * 0.35f, center.y - dir.y * size * 0.35f );
    draw->AddTriangleFilled( tip, ImVec2( back.x + side.x * size * 0.45f,
                                          back.y + side.y * size * 0.45f ),
                             ImVec2( back.x - side.x * size * 0.45f, back.y - side.y * size * 0.45f ), color );
}

void ring_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();

    // the question and Cancel, in a banner at the top
    const float left = origin.x + safe.x + 12.0f * u;
    const float right = origin.x + size.x - safe.z - 12.0f * u;
    const float banner_w = std::min( right - left, 420.0f * u );
    const std::string cancel = _( "Cancel" );
    const float cancel_w = mobile_ui::text_size( face::bold, 14.0f, cancel ).x + 28.0f * u;
    const float text_w = banner_w - cancel_w - 36.0f * u;
    const ImVec2 message_px = mobile_ui::text_size( face::bold, 15.0f, message, text_w );
    const float banner_h = std::max( 56.0f * u, message_px.y + 24.0f * u );
    const ImVec2 banner_min( left + ( right - left - banner_w ) * 0.5f, origin.y + safe.y + 12.0f * u );
    const ImVec2 banner_max( banner_min.x + banner_w, banner_min.y + banner_h );
    mobile_ui::box( draw, banner_min, banner_max, mobile_ui::col_sheet, mobile_ui::col_line, 14.0f );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( banner_min.x + 14.0f * u,
                     banner_min.y + ( banner_h - message_px.y ) * 0.5f ), mobile_ui::col_fg, message, text_w );
    const ImVec2 cancel_pos( banner_max.x - cancel_w - 8.0f * u,
                             banner_min.y + ( banner_h - 40.0f * u ) *
                             0.5f );
    bool held = false;
    if( mobile_ui::pressable( "##pick_cancel", cancel_pos, ImVec2( cancel_w, 40.0f * u ), &held ) ) {
        tapped = answer::cancel;
    }
    mobile_ui::box( draw, cancel_pos, ImVec2( cancel_pos.x + cancel_w, cancel_pos.y + 40.0f * u ),
                    held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 11.0f );
    mobile_ui::text( draw, face::bold, 14.0f, ImVec2( cancel_pos.x + 14.0f * u,
                     cancel_pos.y + 11.0f * u ), mobile_ui::col_accent, cancel );

    // the ring: three by three, in the middle of what is left of the screen
    const float gap = 6.0f * u;
    const float top = banner_max.y + 12.0f * u;
    const float bottom = origin.y + size.y - safe.w - 12.0f * u;
    const float cell_size = std::min( { 84.0f * u, ( bottom - top - 2.0f * gap ) / 3.0f,
                                        ( right - left - 2.0f * gap ) / ( allow_vertical ? 4.2f : 3.0f )
                                      } );
    const float ring = 3.0f * cell_size + 2.0f * gap;
    const ImVec2 ring_min( origin.x + ( size.x - ring ) * 0.5f - ( allow_vertical ?
                           ( cell_size + gap ) * 0.5f : 0.0f ), top + ( bottom - top - ring ) * 0.5f );
    const auto button = [&]( const std::string & id, const ImVec2 & pos, const bool allowed,
    const std::string & label, const point & step, const bool arrow ) {
        const ImVec2 max( pos.x + cell_size, pos.y + cell_size );
        bool down = false;
        const bool hit = allowed &&
                         mobile_ui::pressable( id, pos, ImVec2( cell_size, cell_size ), &down );
        const bool lit = allowed && have_candidates;
        mobile_ui::box( draw, pos, max, down ? mobile_ui::col_button_down :
                        allowed ? IM_COL32( 22, 27, 20, 150 ) : IM_COL32( 22, 27, 20, 60 ),
                        lit ? mobile_ui::col_accent : mobile_ui::col_line, 14.0f );
        const ImU32 ink = !allowed ? IM_COL32( 154, 163, 146, 90 ) : lit ? mobile_ui::col_accent :
                          mobile_ui::col_fg;
        const bool has_label = !label.empty();
        if( arrow ) {
            draw_arrow( draw, ImVec2( pos.x + cell_size * 0.5f, pos.y + cell_size * ( has_label ? 0.34f :
                                      0.5f ) ), step, cell_size * 0.3f, ink );
        }
        if( has_label ) {
            const float wrap = cell_size - 8.0f * u;
            const ImVec2 label_px = mobile_ui::text_size( face::bold, 11.5f, label, wrap );
            draw->PushClipRect( pos, max, true );
            mobile_ui::text( draw, face::bold, 11.5f, ImVec2( pos.x + ( cell_size - std::min( label_px.x,
                             wrap ) ) * 0.5f, arrow ? pos.y + cell_size * 0.56f : pos.y + ( cell_size - label_px.y ) * 0.5f ),
                             allowed ? mobile_ui::col_fg : ink, label, wrap );
            draw->PopClipRect();
        }
        return hit;
    };
    for( size_t i = 0; i < cells.size(); ++i ) {
        const cell &c = cells[i];
        const ImVec2 pos( ring_min.x + static_cast<float>( i % 3 ) * ( cell_size + gap ),
                          ring_min.y + static_cast<float>( i / 3 ) * ( cell_size + gap ) );
        const std::string label = c.step == point::zero && c.label.empty() ? _( "Here" ) : c.label;
        if( button( "##pick_cell" + std::to_string( i ), pos, c.allowed, label, c.step, true ) ) {
            tapped = answer::step;
            tapped_step = c.step;
        }
    }
    if( allow_vertical ) {
        const float x = ring_min.x + ring + gap;
        if( button( "##pick_up", ImVec2( x, ring_min.y ), true, _( "Up" ), point::zero, false ) ) {
            tapped = answer::up;
        }
        if( button( "##pick_down", ImVec2( x, ring_min.y + 2.0f * ( cell_size + gap ) ), true,
                    _( "Down" ), point::zero, false ) ) {
            tapped = answer::down;
        }
    }
    draw->PopClipRect();
}
} // namespace

// --- Look-around ---

static float look_banner_bottom = 0.0f;

float banner_bottom()
{
    return look_banner_bottom;
}

class look_banner::impl : public cataimgui::window
{
    public:
        impl() : cataimgui::window( "MOBILE_LOOK_BANNER",
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoFocusOnAppearing ) {}
        std::string title;
        std::vector<std::string> lines;
    protected:
        cataimgui::bounds get_bounds() override {
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            const float u = mobile_ui::unit();
            const ImVec4 safe = mobile_ui::safe_insets();
            const float w = std::min( size.x - safe.x - safe.z - 24.0f * u, 420.0f * u );
            return { safe.x + ( size.x - safe.x - safe.z - w ) * 0.5f, safe.y + 10.0f * u, w,
                     ( 30.0f + 18.0f * static_cast<float>( std::max<size_t>( lines.size(), 1 ) ) ) *u + 14.0f * u };
        }
        void draw_controls() override {
            const float u = mobile_ui::unit();
            ImDrawList *const draw = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            const ImVec2 max( min.x + size.x, min.y + size.y );
            look_banner_bottom = max.y;
            draw->PushClipRectFullScreen();
            mobile_ui::box( draw, min, max, IM_COL32( 22, 27, 20, 235 ), mobile_ui::col_line, 14.0f );
            const float wrap = size.x - 28.0f * u;
            draw->PushClipRect( min, max, true );
            mobile_ui::text( draw, face::bold, 15.0f, ImVec2( min.x + 14.0f * u, min.y + 9.0f * u ),
                             mobile_ui::col_fg, title );
            float y = min.y + 32.0f * u;
            for( const std::string &line : lines ) {
                mobile_ui::text( draw, face::body, 12.5f, ImVec2( min.x + 14.0f * u, y ),
                                 mobile_ui::col_muted, line );
                y += 18.0f * u;
            }
            static_cast<void>( wrap );
            draw->PopClipRect();
            draw->PopClipRect();
        }
};

look_banner::look_banner() : window( std::make_unique<impl>() ) {}
look_banner::~look_banner()
{
    look_banner_bottom = 0.0f;
}

void look_banner::show( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const avatar &you = get_avatar();
    std::string title;
    std::vector<std::string> lines;
    if( !here.inbounds( pos ) || !you.sees( here, pos ) ) {
        title = _( "Out of sight" );
        lines.emplace_back( _( "Tap a tile to look at it.  Tap it again, or OK, to choose it." ) );
    } else {
        const std::string ground = here.has_furn( pos ) ?
                                   string_format( _( "%1$s on %2$s" ), here.furnname( pos ), here.tername( pos ) ) :
                                   here.tername( pos );
        const Creature *const critter = get_creature_tracker().creature_at( pos );
        if( pos == you.pos_bub() ) {
            title = _( "You" );
            lines.push_back( ground );
        } else if( critter != nullptr && you.sees( here, *critter ) ) {
            const Creature::Attitude attitude = critter->attitude_to( you );
            title = string_format( "%s · %s", remove_color_tags( critter->get_name() ),
                                   attitude == Creature::Attitude::HOSTILE ? _( "hostile" ) :
                                   attitude == Creature::Attitude::FRIENDLY ? _( "friendly" ) : _( "neutral" ) );
            lines.push_back( ground );
        } else {
            title = ground;
        }
        if( const optional_vpart_position vp = here.veh_at( pos ) ) {
            const std::optional<vpart_reference> top = vp.part_displayed();
            lines.push_back( top ? string_format( "%1$s · %2$s", vp->vehicle().name,
                                                  top->part().name( false ) ) : vp->vehicle().name );
        }
        if( here.sees_some_items( pos, you ) ) {
            const map_stack items = here.i_at( pos );
            const size_t count = items.size();
            if( count > 0 ) {
                const std::string first = remove_color_tags( items.begin()->tname() );
                lines.push_back( count == 1 ? string_format( _( "Item: %s" ), first ) :
                                 string_format( _( "Items: %1$s and %2$d more" ), first,
                                                static_cast<int>( count ) - 1 ) );
            }
        }
        if( lines.empty() ) {
            lines.emplace_back( _( "Tap a tile to look at it.  Tap it again, or OK, to choose it." ) );
        }
    }
    set( title, lines );
}

void look_banner::set( const std::string &title, const std::vector<std::string> &lines )
{
    if( title != window->title || lines != window->lines ) {
        window->title = title;
        window->lines = lines;
        window->mark_resized();
    }
}

std::optional<tripoint_rel_ms> direction( const std::string &message, const bool allow_vertical,
        const int timeout )
{
    ring_window window( message, allow_vertical );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.set_iso( true );
    ctxt.register_directions();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    if( timeout >= 0 ) {
        // keeps the map's animations going, as upstream's prompt does
        ctxt.set_timeout( timeout );
    }
    while( true ) {
        // the map again every time, or what was drawn before shows through
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        std::optional<tripoint_rel_ms> step;
        if( action == "QUIT" || window.tapped == answer::cancel ) {
            return std::nullopt;
        }
        if( const std::optional<tripoint_rel_ms> key = ctxt.get_direction_rel_ms( action ) ) {
            step = key;
        } else if( window.tapped == answer::step ) {
            step = tripoint_rel_ms( window.tapped_step.x, window.tapped_step.y, 0 );
        } else if( window.tapped == answer::up ) {
            return tripoint_rel_ms::above;
        } else if( window.tapped == answer::down ) {
            return tripoint_rel_ms::below;
        }
        window.tapped = answer::none;
        if( step ) {
            // face the way of the tile, as upstream's prompt does
            FacingDirection &facing = get_player_character().facing;
            if( step->x() > 0 ) {
                facing = FacingDirection::RIGHT;
            } else if( step->x() < 0 ) {
                facing = FacingDirection::LEFT;
            }
            return step;
        }
    }
}

#else // !TILES

candidates::candidates( const tripoint_bub_ms &, const std::vector<tripoint_bub_ms> & ) {}
candidates::~candidates() = default;

float banner_bottom()
{
    return 0.0f;
}

class look_banner::impl
{
};
look_banner::look_banner() = default;
look_banner::~look_banner() = default;
void look_banner::show( const tripoint_bub_ms & ) {}
void look_banner::set( const std::string &, const std::vector<std::string> & ) {}

std::optional<tripoint_rel_ms> direction( const std::string &, bool, int )
{
    return std::nullopt;
}

#endif // TILES

} // namespace mobile_pick
