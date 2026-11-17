#include "mobile_drawer.h"

#include "action.h"

#if defined(TILES)
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "cata_imgui.h"
#include "character.h"
#include "character_attire.h"
#include "color.h"
#include "coordinates.h"
#include "creature.h"
#include "display.h"
#include "effect.h"
#include "map_scale_constants.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "item.h"
#include "item_location.h"
#include "line.h"
#include "messages.h"
#include "mobile_hud_layout.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "weather.h"

static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_infected( "infected" );
#endif

namespace mobile_drawer
{

#if defined(TILES)

using mobile_ui::face;

namespace
{

// the canvas's health and severity colors
constexpr ImU32 col_hp_good = IM_COL32( 108, 191, 85, 255 );
constexpr ImU32 col_hp_ok = IM_COL32( 216, 187, 66, 255 );
constexpr ImU32 col_hp_low = IM_COL32( 230, 137, 45, 255 );
constexpr ImU32 col_hp_bad = IM_COL32( 224, 73, 43, 255 );
constexpr ImU32 col_water = IM_COL32( 90, 169, 216, 255 );
constexpr ImU32 col_rest = IM_COL32( 154, 134, 214, 255 );
constexpr ImU32 col_panel = IM_COL32( 15, 18, 14, 242 );
constexpr ImU32 col_behind = IM_COL32( 0, 0, 0, 64 );

// the canvas's text line: 13 units at a line height of 1.45
constexpr float text_px = 13.0f;
constexpr float line_units = 19.0f;

// overmap tiles shown each way from the player on the Map tab
constexpr int minimap_radius = 17;
// how long a drawer takes to slide in
constexpr float slide_seconds = 0.2f;

enum class block_kind : int {
    // a section title; starts a new section
    title,
    // label, bar, value
    bar,
    // up to four small tiles of label over value
    tiles,
    // outlined pills that wrap
    pills,
    // a list row: text on the left, muted text on the right
    row,
    // a message: time, then text
    message,
    // a line of muted text
    note,
    // the "Open the full map" button
    map_button,
    // a square picture of the overmap around the player
    minimap,
    // a grid of buttons, one per action
    actions,
};

struct pill {
    std::string label;
    ImU32 color;
};

struct block {
    block_kind kind = block_kind::note;
    std::string left;
    std::string right;
    // how full a bar is, 0 to 1
    float fill = 0.0f;
    ImU32 color = mobile_ui::col_fg;
    std::vector<pill> pills;
    std::vector<std::pair<std::string, std::string>> tiles;
    // minimap: cells_side * cells_side colors, row by row from the north-west;
    // a zero color is a tile not yet seen
    int cells_side = 0;
    std::vector<ImU32> cells;
    // minimap: indices of the cells that carry a note
    std::vector<int> notes;
    // actions: what each button does, and its name
    std::vector<std::pair<action_id, std::string>> actions;
};

struct tab {
    std::string label;
    std::vector<block> blocks;
};

block make_title( const std::string &text )
{
    block b;
    b.kind = block_kind::title;
    b.left = text;
    return b;
}

block make_bar( const std::string &label, const float fill, const ImU32 color,
                const std::string &value )
{
    block b;
    b.kind = block_kind::bar;
    b.left = label;
    b.right = value;
    b.fill = std::clamp( fill, 0.0f, 1.0f );
    b.color = color;
    return b;
}

block make_row( const std::string &left, const std::string &right,
                const ImU32 color = mobile_ui::col_fg )
{
    block b;
    b.kind = block_kind::row;
    b.left = left;
    b.right = right;
    b.color = color;
    return b;
}

block make_note( const std::string &text )
{
    block b;
    b.kind = block_kind::note;
    b.left = text;
    return b;
}

ImU32 hp_color( const float fraction )
{
    return fraction > 0.8f ? col_hp_good : fraction > 0.5f ? col_hp_ok :
           fraction > 0.25f ? col_hp_low : col_hp_bad;
}

std::string percent( const float fraction )
{
    return string_format( "%d%%", static_cast<int>( std::clamp( fraction, 0.0f,
                          1.0f ) * 100.0f + 0.5f ) );
}

std::string part_name( const bodypart_id &part )
{
    return uppercase_first_letter( body_part_name( part ) );
}

// The item's name without the durability and fit marks in front of it.
std::string item_name( const item &it )
{
    return uppercase_first_letter( remove_color_tags( it.tname( 1, false ) ) );
}

tab body_tab( const avatar &you )
{
    tab t;
    t.label = _( "Body" );
    t.blocks.push_back( make_title( _( "Hit points" ) ) );
    for( const bodypart_id &part : you.get_all_body_parts( get_body_part_flags::only_main |
            get_body_part_flags::sorted ) ) {
        const int cur = you.get_part_hp_cur( part );
        const int max = you.get_part_hp_max( part );
        const float fraction = max > 0 ? static_cast<float>( cur ) / static_cast<float>( max ) : 0.0f;
        t.blocks.push_back( make_bar( part_name( part ), fraction, hp_color( fraction ),
                                      string_format( "%d", cur ) ) );
    }

    t.blocks.push_back( make_title( _( "Wounds" ) ) );
    block wounds;
    wounds.kind = block_kind::pills;
    const auto add_wound = [&wounds]( const bodypart_id & part, const std::string & what,
    const ImU32 color ) {
        //~ a wound on a body part: "Left arm: bleeding"
        wounds.pills.push_back( { string_format( _( "%1$s: %2$s" ), part_name( part ), what ), color } );
    };
    for( const bodypart_id &part : you.get_all_body_parts( get_body_part_flags::sorted ) ) {
        // the same thresholds as the sidebar's limb colors
        const int bleeding = you.get_effect_int( effect_bleed, part );
        if( bleeding > 0 ) {
            add_wound( part, to_lower_case( you.get_effect( effect_bleed, part ).disp_name() ),
                       bleeding < 11 ? col_hp_ok : bleeding < 21 ? col_hp_low : col_hp_bad );
        }
        if( you.has_effect( effect_bite, part ) ) {
            add_wound( part, _( "bitten" ), col_hp_low );
        }
        if( you.has_effect( effect_infected, part ) ) {
            add_wound( part, _( "infected" ), col_hp_bad );
        }
        if( you.is_limb_broken( part ) ) {
            add_wound( part, _( "broken" ), col_hp_bad );
        }
    }
    if( wounds.pills.empty() ) {
        t.blocks.push_back( make_note( _( "None" ) ) );
    } else {
        t.blocks.push_back( wounds );
    }

    t.blocks.push_back( make_title( _( "Encumbrance" ) ) );
    for( const bodypart_id &part : you.get_all_body_parts( get_body_part_flags::sorted ) ) {
        std::string detail = string_format( "%d", you.encumb( part ) );
        const std::pair<std::string, nc_color> warmth = display::temp_text_color( you, part.id() );
        if( warmth.second != c_green && !warmth.first.empty() ) {
            detail += " · " + to_lower_case( warmth.first );
        }
        if( you.get_part_wetness_percentage( part ) >= BODYWET_PERCENT_WET ) {
            detail += std::string( " · " ) + _( "wet" );
        }
        t.blocks.push_back( make_row( part_name( part ), detail ) );
    }
    return t;
}

tab status_tab( const avatar &you )
{
    tab t;
    t.label = _( "Status" );
    t.blocks.push_back( make_title( _( "Stats" ) ) );
    block stats;
    stats.kind = block_kind::tiles;
    stats.tiles = {
        { _( "STR" ), string_format( "%d", you.get_str() ) },
        { _( "DEX" ), string_format( "%d", you.get_dex() ) },
        { _( "INT" ), string_format( "%d", you.get_int() ) },
        { _( "PER" ), string_format( "%d", you.get_per() ) },
    };
    t.blocks.push_back( stats );

    t.blocks.push_back( make_title( _( "Needs" ) ) );
    // Food: stored calories against the healthy amount for this body.
    const float food = you.needs_food() ? you.get_kcal_percent() : 1.0f;
    // Water: full at no thirst, empty at "Parched" (520).
    const float water = 1.0f - static_cast<float>( you.get_instant_thirst() ) / 520.0f;
    // Rest: full at no sleepiness, empty at the level where the body gives out.
    const float rest = 1.0f - static_cast<float>( you.get_sleepiness() ) /
                       static_cast<float>( sleepiness_levels::MASSIVE_SLEEPINESS );
    const int stamina_max = you.get_stamina_max();
    const float stamina = stamina_max > 0 ? static_cast<float>( you.get_stamina() ) /
                          static_cast<float>( stamina_max ) : 1.0f;
    t.blocks.push_back( make_bar( _( "Food" ), food, col_hp_ok, percent( food ) ) );
    t.blocks.push_back( make_bar( _( "Water" ), water, col_water, percent( water ) ) );
    t.blocks.push_back( make_bar( _( "Rest" ), rest, col_rest, percent( rest ) ) );
    t.blocks.push_back( make_bar( _( "Stamina" ), stamina, col_hp_good, percent( stamina ) ) );

    t.blocks.push_back( make_title( _( "Condition" ) ) );
    t.blocks.push_back( make_row( _( "Speed" ), string_format( "%d", you.get_speed() ) ) );
    t.blocks.push_back( make_row( _( "Morale" ), string_format( "%+d", you.get_morale_level() ) ) );
    t.blocks.push_back( make_row( _( "Focus" ), string_format( "%d", you.get_focus() ) ) );
    const std::string pain = remove_color_tags( display::pain_text_color( you ).first );
    t.blocks.push_back( make_row( _( "Pain" ), pain.empty() ? _( "None" ) : pain ) );
    return t;
}

tab gear_tab( const avatar &you )
{
    tab t;
    t.label = _( "Gear" );
    t.blocks.push_back( make_title( _( "Wielded" ) ) );
    const item_location weapon = you.get_wielded_item();
    if( weapon ) {
        t.blocks.push_back( make_row( item_name( *weapon ),
                                      weapon->is_two_handed( you ) ? _( "2-handed" ) : "" ) );
    } else {
        t.blocks.push_back( make_row( _( "Nothing" ), "" ) );
    }

    t.blocks.push_back( make_title( _( "Worn" ) ) );
    std::vector<const item *> worn;
    you.worn.inv_dump( worn );
    for( const item *const it : worn ) {
        std::string detail;
        if( it->is_container() && it->get_volume_capacity() > 0_ml ) {
            //~ how full a worn container is: "18.4 / 30 L"
            detail = string_format( _( "%1$s / %2$s %3$s" ),
                                    format_volume( it->get_contents_volume() ),
                                    format_volume( it->get_volume_capacity() ), volume_units_abbr() );
        }
        t.blocks.push_back( make_row( item_name( *it ), detail ) );
    }
    if( worn.empty() ) {
        t.blocks.push_back( make_row( _( "Nothing" ), "" ) );
    }

    t.blocks.push_back( make_title( _( "Carrying" ) ) );
    const units::mass carried = you.weight_carried();
    const units::mass capacity = you.weight_capacity();
    const float load = capacity > 0_gram ? static_cast<float>( units::to_gram( carried ) ) /
                       static_cast<float>( units::to_gram( capacity ) ) : 1.0f;
    t.blocks.push_back( make_bar( _( "Weight" ), load, load > 1.0f ? col_hp_low : col_hp_ok,
                                  string_format( "%.0f %s", convert_weight( carried ), weight_units() ) ) );
    return t;
}

tab area_tab( const avatar &you )
{
    tab t;
    t.label = _( "Area" );
    t.blocks.push_back( make_title( _( "Here" ) ) );
    t.blocks.push_back( make_row( remove_color_tags( display::current_position_text(
                                      you.pos_abs_omt() ) ), "" ) );
    t.blocks.push_back( make_row( _( "Time" ), string_format( "%s · %s", display::date_string(),
                                  remove_color_tags( display::time_string( you ) ) ) ) );
    std::string weather = remove_color_tags( display::weather_text_color( you ).first );
    const std::string temperature = remove_color_tags( display::get_temp( you ) );
    if( !temperature.empty() && temperature != "-" ) {
        weather += " · " + temperature;
    }
    t.blocks.push_back( make_row( _( "Weather" ), weather ) );
    t.blocks.push_back( make_row( _( "Light" ),
                                  uppercase_first_letter( get_light_level(
                                          you.fine_detail_vision_mod() ).first ) ) );

    t.blocks.push_back( make_title( _( "Threats" ) ) );
    std::vector<std::pair<int, const Creature *>> seen;
    for( const Creature *const critter : you.get_visible_creatures( MAX_VIEW_DISTANCE ) ) {
        if( critter != nullptr ) {
            seen.emplace_back( rl_dist( you.pos_bub(), critter->pos_bub() ), critter );
        }
    }
    std::stable_sort( seen.begin(), seen.end(), []( const std::pair<int, const Creature *> &a,
    const std::pair<int, const Creature *> &b ) {
        return a.first < b.first;
    } );
    for( const std::pair<int, const Creature *> &entry : seen ) {
        const std::string where = string_format(
                                      //~ distance and compass direction to a creature: "4 tiles NE"
                                      n_gettext( "%1$d tile %2$s", "%1$d tiles %2$s", entry.first ), entry.first,
                                      direction_name_short( direction_from( you.pos_bub(), entry.second->pos_bub() ) ) );
        t.blocks.push_back( make_row( uppercase_first_letter( remove_color_tags(
                                          entry.second->get_name() ) ), where,
                                      cataimgui::ImU32_from_color( entry.second->basic_symbol_color() ) ) );
    }
    if( seen.empty() ) {
        t.blocks.push_back( make_row( _( "None in sight" ), "" ) );
    }
    return t;
}

tab log_tab()
{
    tab t;
    t.label = _( "Log" );
    const size_t count = std::min<size_t>( Messages::size(), 300 );
    const std::vector<std::pair<std::string, std::string>> messages = Messages::recent_messages(
                count );
    for( auto it = messages.rbegin(); it != messages.rend(); ++it ) {
        block b;
        b.kind = block_kind::message;
        b.left = it->first;
        b.right = remove_color_tags( it->second );
        t.blocks.push_back( b );
    }
    if( messages.empty() ) {
        t.blocks.push_back( make_note( _( "Nothing has happened yet." ) ) );
    }
    return t;
}

tab map_tab()
{
    tab t;
    t.label = _( "Map" );
    t.blocks.push_back( make_title( _( "Nearby" ) ) );
    // The overmap around the player, as the full map colors it.
    const tripoint_abs_omt center = get_avatar().pos_abs_omt();
    block map;
    map.kind = block_kind::minimap;
    map.cells_side = 2 * minimap_radius + 1;
    map.cells.reserve( static_cast<size_t>( map.cells_side * map.cells_side ) );
    for( int dy = -minimap_radius; dy <= minimap_radius; ++dy ) {
        for( int dx = -minimap_radius; dx <= minimap_radius; ++dx ) {
            const tripoint_abs_omt at = center + point( dx, dy );
            const om_vision_level vision = overmap_buffer.seen( at );
            if( vision == om_vision_level::unseen ) {
                map.cells.push_back( 0 );
                continue;
            }
            if( overmap_buffer.has_note( at ) ) {
                map.notes.push_back( static_cast<int>( map.cells.size() ) );
            }
            map.cells.push_back( cataimgui::ImU32_from_color( overmap_buffer.ter( at )->get_color(
                                     vision ) ) | IM_COL32_A_MASK );
        }
    }
    t.blocks.push_back( map );
    t.blocks.push_back( make_note( remove_color_tags( display::current_position_text( center ) ) ) );
    block button;
    button.kind = block_kind::map_button;
    button.left = _( "Open the full map" );
    t.blocks.push_back( button );
    return t;
}

// Text in the canvas's text line, centered in its line box.
void line_text( ImDrawList *const draw, const face f, const float size, const ImVec2 &pos,
                const float line_h, const ImU32 color, const std::string &str )
{
    const ImVec2 measured = mobile_ui::text_size( f, size, str );
    mobile_ui::text( draw, f, size, ImVec2( pos.x, pos.y + ( line_h - measured.y ) * 0.5f ), color,
                     str );
}

// Text cut short with an ellipsis so that it fits a width.
std::string fit( const face f, const float size, const std::string &str, const float width )
{
    if( mobile_ui::text_size( f, size, str ).x <= width ) {
        return str;
    }
    std::string cut = str;
    while( !cut.empty() ) {
        // drop one UTF-8 character
        while( cut.size() > 1 && ( static_cast<unsigned char>( cut.back() ) & 0xC0 ) == 0x80 ) {
            cut.pop_back();
        }
        cut.pop_back();
        if( mobile_ui::text_size( f, size, cut + "…" ).x <= width ) {
            break;
        }
    }
    return cut + "…";
}

// One tab of the Actions drawer: the actions that have a key in this game, named as
// the keybindings screen names them.
tab actions_tab( const std::string &label, const std::vector<action_id> &actions )
{
    static const input_context names( "DEFAULTMODE" );
    tab t;
    t.label = label;
    block b;
    b.kind = block_kind::actions;
    for( const action_id act : actions ) {
        std::string name = names.get_action_name( action_ident( act ) );
        if( !name.empty() ) {
            b.actions.emplace_back( act, name );
        }
    }
    t.blocks.push_back( b );
    return t;
}

std::vector<tab> actions_tabs()
{
    return {
        actions_tab( _( "Around" ), {
            ACTION_EXAMINE, ACTION_PICKUP, ACTION_LOOK, ACTION_LIST_ITEMS, ACTION_OPEN, ACTION_CLOSE,
            ACTION_SMASH, ACTION_GRAB, ACTION_HAUL, ACTION_BUTCHER, ACTION_CHAT, ACTION_PEEK,
            ACTION_CONTROL_VEHICLE, ACTION_MOVE_UP, ACTION_MOVE_DOWN, ACTION_ZONES
        } ),
        actions_tab( _( "Items" ), {
            ACTION_INVENTORY, ACTION_EAT, ACTION_USE, ACTION_WEAR, ACTION_TAKE_OFF, ACTION_WIELD,
            ACTION_READ, ACTION_DROP, ACTION_DIR_DROP, ACTION_UNLOAD, ACTION_UNLOAD_CONTAINER,
            ACTION_INSERT_ITEM, ACTION_RELOAD_ITEM, ACTION_MEND, ACTION_COMPARE, ACTION_ADVANCEDINV,
            ACTION_SORT_ARMOR, ACTION_ORGANIZE
        } ),
        actions_tab( _( "Fight" ), {
            ACTION_FIRE, ACTION_RELOAD_WEAPON, ACTION_SELECT_FIRE_MODE, ACTION_THROW, ACTION_CAST_SPELL,
            ACTION_PICK_STYLE, ACTION_AUTOATTACK, ACTION_TOGGLE_SAFEMODE, ACTION_IGNORE_ENEMY,
            ACTION_WHITELIST_ENEMY, ACTION_CYCLE_MOVE, ACTION_TOGGLE_RUN, ACTION_TOGGLE_CROUCH,
            ACTION_TOGGLE_PRONE
        } ),
        actions_tab( _( "Make" ), {
            ACTION_CRAFT, ACTION_RECRAFT, ACTION_LONGCRAFT, ACTION_CONSTRUCT, ACTION_DISASSEMBLE,
            ACTION_WAIT, ACTION_SLEEP, ACTION_WORKOUT
        } ),
        actions_tab( _( "You" ), {
            ACTION_PL_INFO, ACTION_BODYSTATUS, ACTION_MEDICAL, ACTION_MORALE, ACTION_BIONICS,
            ACTION_MUTATIONS, ACTION_MISSIONS, ACTION_FACTIONS, ACTION_MAP, ACTION_SKY, ACTION_MESSAGES,
            ACTION_DIARY, ACTION_HELP
        } ),
        actions_tab( _( "Game" ), {
            ACTION_QUICKSAVE, ACTION_SAVE, ACTION_OPTIONS, ACTION_AUTOPICKUP, ACTION_SAFEMODE,
            ACTION_TOGGLE_AUTO_TRAVEL_MODE, ACTION_TOGGLE_AUTO_FEATURES, ACTION_TOGGLE_AUTO_PICKUP,
            ACTION_TOGGLE_THIEF_MODE, ACTION_MAIN_MENU
        } ),
    };
}

// set by open_on_map()
static bool start_on_map = false;
static bool actions_requested = false;
// the tab the Actions drawer was left on
static int actions_page = 0;

class drawer_window : public cataimgui::window
{
    public:
        explicit drawer_window( const drawer which ) : cataimgui::window( "MOBILE_DRAWER",
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ), which( which ) {
            // The game waits while the drawer is open, so everything is read once.
            const avatar &you = get_avatar();
            if( which == drawer::info ) {
                tabs = { body_tab( you ), status_tab( you ), gear_tab( you ), area_tab( you ) };
            } else if( which == drawer::actions ) {
                tabs = actions_tabs();
                current = std::clamp( actions_page, 0, static_cast<int>( tabs.size() ) - 1 );
            } else {
                tabs = { log_tab(), map_tab() };
                if( start_on_map ) {
                    current = 1;
                }
            }
            start_on_map = false;
        }

        bool closed = false;
        bool open_map = false;
        // the button tapped in the Actions drawer
        action_id chosen = ACTION_NULL;
        int page() const {
            return current;
        }

        // Whether the drawer is still sliding in, so that it wants redrawing.
        bool sliding() const {
            return slide_progress() < 1.0f;
        }

        void turn_page( const int by ) {
            current = std::clamp( current + by, 0, static_cast<int>( tabs.size() ) - 1 );
        }

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        // Draws the blocks of a tab from pos down; returns the height they took.
        float draw_blocks( const tab &t, const ImVec2 &pos, float width );

        // 0 when the drawer has just opened, 1 once it is fully in
        float slide_progress() const {
            const std::chrono::duration<float> passed = std::chrono::steady_clock::now() - opened;
            return std::clamp( passed.count() / slide_seconds, 0.0f, 1.0f );
        }

        drawer which;
        std::vector<tab> tabs;
        int current = 0;
        std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
};

float drawer_window::draw_blocks( const tab &t, const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    const float line_h = line_units * u;
    const float hairline = std::max( 1.0f, u );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float clip_top = draw->GetClipRectMin().y;
    const float clip_bottom = draw->GetClipRectMax().y;
    float y = pos.y;
    const block *previous = nullptr;
    for( size_t i = 0; i < t.blocks.size(); ++i ) {
        const block &b = t.blocks[i];
        const block *const next = i + 1 < t.blocks.size() ? &t.blocks[i + 1] : nullptr;
        // sections are 12 apart, their parts 6; list rows and messages touch
        if( previous != nullptr ) {
            if( b.kind == block_kind::title ) {
                y += 12.0f * u;
            } else if( !( b.kind == previous->kind && ( b.kind == block_kind::row ||
                          b.kind == block_kind::message ) ) ) {
                y += 6.0f * u;
            }
        }
        previous = &b;
        // measured first, so that what is scrolled out of view is not drawn
        float height = line_h;
        float message_x = 0.0f;
        switch( b.kind ) {
            case block_kind::title:
                height = 10.5f * u;
                break;
            case block_kind::tiles:
                height = 42.0f * u;
                break;
            case block_kind::pills: {
                float x = 0.0f;
                height = 23.0f * u;
                for( const pill &p : b.pills ) {
                    const float w = mobile_ui::text_size( face::body, 11.5f, p.label ).x + 18.0f * u;
                    if( x > 0.0f && x + w > width ) {
                        x = 0.0f;
                        height += 28.0f * u;
                    }
                    x += w + 5.0f * u;
                }
                break;
            }
            case block_kind::row:
                height = line_h + 12.0f * u + hairline;
                break;
            case block_kind::message:
                message_x = mobile_ui::text_size( face::mono, 10.5f, b.left ).x + 6.0f * u;
                height = std::max( line_h, mobile_ui::text_size( face::body, text_px, b.right,
                                   width - message_x ).y + 3.0f * u ) + 6.0f * u + hairline;
                break;
            case block_kind::note:
                height = std::max( line_h, mobile_ui::text_size( face::body, text_px, b.left, width ).y );
                break;
            case block_kind::map_button:
                height = 44.0f * u;
                break;
            case block_kind::actions: {
                const int columns = width > 420.0f * u ? 3 : 2;
                const int rows = ( static_cast<int>( b.actions.size() ) + columns - 1 ) / columns;
                height = static_cast<float>( rows ) * 54.0f * u;
                break;
            }
            case block_kind::minimap:
                // square, as large as fits with the place name and the button under it
                height = std::max( 96.0f * u, std::min( width, ImGui::GetWindowSize().y -
                                                        ( 12.0f + 10.5f + 6.0f + 6.0f + line_units + 6.0f + 44.0f + 8.0f ) * u ) );
                break;
            case block_kind::bar:
                break;
        }
        const bool visible = y + height >= clip_top && y <= clip_bottom;
        if( !visible && b.kind != block_kind::map_button && b.kind != block_kind::minimap ) {
            y += height;
            continue;
        }
        switch( b.kind ) {
            case block_kind::title:
                mobile_ui::text( draw, face::mono, 10.5f, ImVec2( pos.x, y ), mobile_ui::col_muted,
                                 to_upper_case( b.left ) );
                break;
            case block_kind::bar: {
                const float label_w = 74.0f * u;
                const float value_w = 38.0f * u;
                line_text( draw, face::body, text_px, ImVec2( pos.x, y ), line_h, mobile_ui::col_fg,
                           fit( face::body, text_px, b.left, label_w ) );
                const ImVec2 bar_min( pos.x + label_w + 8.0f * u, y + ( line_h - 7.0f * u ) * 0.5f );
                const ImVec2 bar_max( pos.x + width - value_w - 8.0f * u, bar_min.y + 7.0f * u );
                draw->AddRectFilled( bar_min, bar_max, IM_COL32( 255, 255, 255, 20 ), 4.0f * u );
                if( b.fill > 0.0f ) {
                    const float filled = std::max( ( bar_max.x - bar_min.x ) * b.fill, 7.0f * u );
                    draw->AddRectFilled( bar_min, ImVec2( bar_min.x + filled, bar_max.y ), b.color, 4.0f * u );
                }
                const float value = mobile_ui::text_size( face::mono, 11.5f, b.right ).x;
                line_text( draw, face::mono, 11.5f, ImVec2( pos.x + width - value, y ), line_h,
                           mobile_ui::col_muted, b.right );
                break;
            }
            case block_kind::tiles: {
                const float gap = 6.0f * u;
                const float tile_w = ( width - 3.0f * gap ) / 4.0f;
                for( size_t n = 0; n < b.tiles.size() && n < 4; ++n ) {
                    const ImVec2 tile( pos.x + static_cast<float>( n ) * ( tile_w + gap ), y );
                    draw->AddRectFilled( tile, ImVec2( tile.x + tile_w, tile.y + height ),
                                         mobile_ui::col_fill_soft, 8.0f * u );
                    mobile_ui::text( draw, face::mono, 10.0f, ImVec2( tile.x + 8.0f * u, tile.y + 6.0f * u ),
                                     mobile_ui::col_muted, b.tiles[n].first );
                    mobile_ui::text( draw, face::mono, 17.0f, ImVec2( tile.x + 8.0f * u, tile.y + 17.0f * u ),
                                     mobile_ui::col_fg, b.tiles[n].second );
                }
                break;
            }
            case block_kind::pills: {
                const float pill_h = 23.0f * u;
                float x = 0.0f;
                float row_y = y;
                for( const pill &p : b.pills ) {
                    const float w = mobile_ui::text_size( face::body, 11.5f, p.label ).x + 18.0f * u;
                    if( x > 0.0f && x + w > width ) {
                        x = 0.0f;
                        row_y += 28.0f * u;
                    }
                    const ImVec2 min( pos.x + x, row_y );
                    draw->AddRect( min, ImVec2( min.x + w, min.y + pill_h ), p.color, pill_h * 0.5f, 0,
                                   hairline );
                    line_text( draw, face::body, 11.5f, ImVec2( min.x + 9.0f * u, min.y ), pill_h, p.color,
                               p.label );
                    x += w + 5.0f * u;
                }
                break;
            }
            case block_kind::row: {
                const float right_w = b.right.empty() ? 0.0f :
                                      std::min( mobile_ui::text_size( face::body, text_px, b.right ).x,
                                                width * 0.62f );
                const std::string right = fit( face::body, text_px, b.right, right_w + 0.5f );
                const float left_w = width - right_w - ( b.right.empty() ? 0.0f : 10.0f * u );
                line_text( draw, face::body, text_px, ImVec2( pos.x, y + 6.0f * u ), line_h, b.color,
                           fit( face::body, text_px, b.left, left_w ) );
                if( !right.empty() ) {
                    const float w = mobile_ui::text_size( face::body, text_px, right ).x;
                    line_text( draw, face::body, text_px, ImVec2( pos.x + width - w, y + 6.0f * u ), line_h,
                               mobile_ui::col_muted, right );
                }
                if( next != nullptr && next->kind == block_kind::row ) {
                    const float line_y = y + height - hairline * 0.5f;
                    draw->AddLine( ImVec2( pos.x, line_y ), ImVec2( pos.x + width, line_y ),
                                   mobile_ui::col_line, hairline );
                }
                break;
            }
            case block_kind::message: {
                line_text( draw, face::mono, 10.5f, ImVec2( pos.x, y + 3.0f * u ), line_h,
                           mobile_ui::col_muted, b.left );
                mobile_ui::text( draw, face::body, text_px, ImVec2( pos.x + message_x, y + 4.5f * u ),
                                 mobile_ui::col_fg, b.right, width - message_x );
                const float line_y = y + height - hairline * 0.5f;
                draw->AddLine( ImVec2( pos.x, line_y ), ImVec2( pos.x + width, line_y ),
                               IM_COL32( 255, 255, 255, 10 ), hairline );
                break;
            }
            case block_kind::note:
                mobile_ui::text( draw, face::body, text_px, ImVec2( pos.x, y + 1.5f * u ),
                                 mobile_ui::col_muted, b.left, width );
                break;
            case block_kind::minimap: {
                const ImVec2 map_max( pos.x + height, y + height );
                // a tap on the picture opens the full map too
                if( mobile_ui::pressable( "##minimap", ImVec2( pos.x, y ), ImVec2( height, height ) ) ) {
                    open_map = true;
                }
                draw->AddRectFilled( ImVec2( pos.x, y ), map_max, IM_COL32( 0, 0, 0, 255 ), 8.0f * u );
                const float cell = height / static_cast<float>( b.cells_side );
                const auto cell_min = [&]( const int index ) {
                    return ImVec2( pos.x + static_cast<float>( index % b.cells_side ) * cell,
                                   y + static_cast<float>( index / b.cells_side ) * cell );
                };
                for( int n = 0; n < static_cast<int>( b.cells.size() ); ++n ) {
                    if( b.cells[n] == 0 ) {
                        continue;
                    }
                    const ImVec2 at = cell_min( n );
                    // a hair of the black shows between the tiles
                    draw->AddRectFilled( ImVec2( at.x + 0.5f, at.y + 0.5f ),
                                         ImVec2( at.x + cell - 0.5f, at.y + cell - 0.5f ), b.cells[n] );
                }
                for( const int n : b.notes ) {
                    const ImVec2 at = cell_min( n );
                    const ImVec2 middle( at.x + cell * 0.5f, at.y + cell * 0.5f );
                    draw->AddCircleFilled( middle, cell * 0.5f, IM_COL32( 0, 0, 0, 255 ) );
                    draw->AddCircleFilled( middle, cell * 0.34f, mobile_ui::col_title );
                }
                // the player, in the middle
                const ImVec2 at = cell_min( static_cast<int>( b.cells.size() ) / 2 );
                draw->AddRectFilled( ImVec2( at.x - cell * 0.35f, at.y - cell * 0.35f ),
                                     ImVec2( at.x + cell * 1.35f, at.y + cell * 1.35f ),
                                     IM_COL32( 0, 0, 0, 255 ), 2.0f * u );
                draw->AddRectFilled( ImVec2( at.x - cell * 0.1f, at.y - cell * 0.1f ),
                                     ImVec2( at.x + cell * 1.1f, at.y + cell * 1.1f ),
                                     mobile_ui::col_accent, 1.5f * u );
                draw->AddRect( ImVec2( pos.x, y ), map_max, mobile_ui::col_line, 8.0f * u, 0, hairline );
                break;
            }
            case block_kind::actions: {
                const int columns = width > 420.0f * u ? 3 : 2;
                const float gap = 6.0f * u;
                const float cell_w = ( width - static_cast<float>( columns - 1 ) * gap ) /
                                     static_cast<float>( columns );
                for( size_t n = 0; n < b.actions.size(); ++n ) {
                    const ImVec2 cell( pos.x + static_cast<float>( n % static_cast<size_t>( columns ) ) *
                                       ( cell_w + gap ),
                                       y + static_cast<float>( n / static_cast<size_t>( columns ) ) * 54.0f * u );
                    bool held = false;
                    if( mobile_ui::pressable( "##drawer_action" + std::to_string( n ), cell,
                                              ImVec2( cell_w, 48.0f * u ), &held ) ) {
                        chosen = b.actions[n].first;
                    }
                    mobile_ui::box( draw, cell, ImVec2( cell.x + cell_w, cell.y + 48.0f * u ),
                                    held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 ),
                                    mobile_ui::col_line, 11.0f );
                    const float wrap = cell_w - 16.0f * u;
                    const std::string &name = b.actions[n].second;
                    const ImVec2 name_px = mobile_ui::text_size( face::bold, 13.0f, name, wrap );
                    draw->PushClipRect( cell, ImVec2( cell.x + cell_w, cell.y + 48.0f * u ), true );
                    mobile_ui::text( draw, face::bold, 13.0f, ImVec2( cell.x + 8.0f * u,
                                     cell.y + std::max( 2.0f * u, ( 48.0f * u - name_px.y ) * 0.5f ) ),
                                     mobile_ui::col_fg, name, wrap );
                    draw->PopClipRect();
                }
                break;
            }
            case block_kind::map_button: {
                // drawn 36 high as on the canvas, inside a 44 high touch area
                const float button_w = mobile_ui::text_size( face::bold, text_px, b.left ).x + 24.0f * u;
                bool held = false;
                if( mobile_ui::pressable( "##open_map", ImVec2( pos.x, y ), ImVec2( button_w, height ),
                                          &held ) ) {
                    open_map = true;
                }
                const ImVec2 min( pos.x, y + 4.0f * u );
                const ImVec2 max( pos.x + button_w, y + 40.0f * u );
                mobile_ui::box( draw, min, max, held ? mobile_ui::col_button_down :
                                IM_COL32( 255, 255, 255, 15 ), mobile_ui::col_line, 10.0f );
                line_text( draw, face::bold, text_px, ImVec2( min.x + 12.0f * u, min.y ), max.y - min.y,
                           mobile_ui::col_fg, b.left );
                break;
            }
        }
        y += height;
    }
    return y - pos.y;
}

void drawer_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const float hairline = std::max( 1.0f, u );
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();

    // Where the drawer sits: the edge and depth of the HUD layout in use.
    const mobile_hud::drawer &cfg = mobile_hud::active_layout( landscape )[which == drawer::log ?
                                          mobile_hud::drawer_id::log : which == drawer::actions ?
                                          mobile_hud::drawer_id::actions : mobile_hud::drawer_id::info];
    const mobile_hud::screen_edge from = cfg.edge;
    using edge = mobile_hud::screen_edge;
    ImVec2 min;
    ImVec2 max;
    ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
    if( from == edge::top || from == edge::bottom ) {
        const float depth = ( size.y - safe.y - safe.w ) * cfg.size;
        min = ImVec2( origin.x + safe.x + 8.0f * u, origin.y + safe.y );
        max = ImVec2( origin.x + size.x - safe.z - 8.0f * u, min.y + depth );
        corners = ImDrawFlags_RoundCornersBottom;
        if( from == edge::bottom ) {
            max.y = origin.y + size.y - safe.w;
            min.y = max.y - depth;
            corners = ImDrawFlags_RoundCornersTop;
        }
    } else {
        const float depth = ( size.x - safe.x - safe.z ) * cfg.size;
        min = ImVec2( origin.x + safe.x, origin.y + safe.y + 8.0f * u );
        max = ImVec2( min.x + depth, origin.y + size.y - safe.w - 8.0f * u );
        corners = ImDrawFlags_RoundCornersRight;
        if( from == edge::right ) {
            max.x = origin.x + size.x - safe.z;
            min.x = max.x - depth;
            corners = ImDrawFlags_RoundCornersLeft;
        }
    }

    // sliding in from its edge, easing out
    const float remaining = 1.0f - slide_progress();
    const float hidden = remaining * remaining * remaining;
    if( hidden > 0.0f ) {
        const ImVec2 shift = from == edge::top ? ImVec2( 0.0f, -( max.y - origin.y ) * hidden ) :
                             from == edge::bottom ? ImVec2( 0.0f, ( origin.y + size.y - min.y ) * hidden ) :
                             from == edge::left ? ImVec2( -( max.x - origin.x ) * hidden, 0.0f ) :
                             ImVec2( ( origin.x + size.x - min.x ) * hidden, 0.0f );
        min = ImVec2( min.x + shift.x, min.y + shift.y );
        max = ImVec2( max.x + shift.x, max.y + shift.y );
    }

    // the game stays visible behind, a little darker
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ), col_behind );
    draw->PopClipRect();
    draw->AddRectFilled( min, max, col_panel, 16.0f * u, corners );
    draw->AddRect( min, max, mobile_ui::col_line, 16.0f * u, corners, hairline );
    // no border along the screen edge the panel is attached to
    if( from == edge::top || from == edge::bottom ) {
        const float y = from == edge::top ? min.y : max.y;
        draw->AddLine( ImVec2( min.x, y ), ImVec2( max.x, y ), col_panel, hairline * 2.0f );
    } else {
        const float x = from == edge::left ? min.x : max.x;
        draw->AddLine( ImVec2( x, min.y ), ImVec2( x, max.y ), col_panel, hairline * 2.0f );
    }

    // --- header: tabs and the close button ---
    const float head_h = 42.0f * u;
    const float head_bottom = min.y + head_h;
    float x = min.x + 10.0f * u;
    for( int i = 0; i < static_cast<int>( tabs.size() ); ++i ) {
        const bool selected = i == current;
        const std::string &label = tabs[i].label;
        const float label_w = mobile_ui::text_size( face::bold, 13.0f, label ).x;
        const float tab_w = label_w + 16.0f * u;
        // the touch area is the whole height of the header, and a little wider
        if( mobile_ui::pressable( "##drawer_tab" + std::to_string( i ), ImVec2( x - 2.0f * u, min.y ),
                                  ImVec2( tab_w + 4.0f * u, head_h + 2.0f * u ) ) ) {
            current = i;
        }
        line_text( draw, face::bold, 13.0f, ImVec2( x + 8.0f * u, min.y + 8.0f * u ), 32.0f * u,
                   selected ? mobile_ui::col_fg : mobile_ui::col_muted, label );
        if( selected ) {
            draw->AddRectFilled( ImVec2( x, head_bottom - 2.0f * u ), ImVec2( x + tab_w, head_bottom ),
                                 mobile_ui::col_accent );
        }
        x += tab_w + 4.0f * u;
    }
    bool close_held = false;
    const ImVec2 close_pos( max.x - 46.0f * u, min.y );
    if( mobile_ui::pressable( "##drawer_close", close_pos, ImVec2( 46.0f * u, head_h + 2.0f * u ),
                              &close_held ) ) {
        closed = true;
    }
    mobile_ui::icon( draw, "close", ImVec2( max.x - 33.0f * u, min.y + 15.0f * u ), 18.0f,
                     close_held ? mobile_ui::col_fg : mobile_ui::col_muted );
    draw->AddLine( ImVec2( min.x, head_bottom ), ImVec2( max.x, head_bottom ), mobile_ui::col_line,
                   hairline );

    // --- content ---
    const float dots_h = 19.0f * u;
    const ImVec2 content_min( min.x, head_bottom + hairline );
    const ImVec2 content_size( max.x - min.x, max.y - dots_h - content_min.y );
    ImGui::SetCursorScreenPos( content_min );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( ( "##drawer_content" + std::to_string( current ) ).c_str(), content_size,
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float used = draw_blocks( tabs[current], ImVec2( base.x + 14.0f * u, base.y + 12.0f * u ),
                                    content_size.x - 28.0f * u );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, used + 28.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // --- page dots ---
    const float count = static_cast<float>( tabs.size() );
    float dot_x = ( min.x + max.x ) * 0.5f - ( count * 10.0f - 5.0f ) * u * 0.5f + 2.5f * u;
    for( int i = 0; i < static_cast<int>( tabs.size() ); ++i ) {
        draw->AddCircleFilled( ImVec2( dot_x, max.y - dots_h + 8.5f * u ), 2.5f * u,
                               i == current ? mobile_ui::col_accent : mobile_ui::col_line );
        dot_x += 10.0f * u;
    }

    // Last, so that everything above gets its taps first: a tap outside closes.
    if( mobile_ui::pressable( "##drawer_outside", origin, size ) ) {
        const ImVec2 at = ImGui::GetIO().MousePos;
        if( at.x < min.x || at.x > max.x || at.y < min.y || at.y > max.y ) {
            closed = true;
        }
    }
}

} // namespace

action_id show( const drawer which )
{
    drawer_window window( which );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "TIMEOUT" );
    while( !window.closed && !window.open_map && window.chosen == ACTION_NULL ) {
        ui_manager::redraw();
        // while the drawer slides in, redraw without waiting for input
        if( window.sliding() ) {
            ctxt.set_timeout( 15 );
        } else {
            ctxt.reset_timeout();
        }
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            break;
        } else if( action == "LEFT" ) {
            // a swipe to the left brings in the next tab
            window.turn_page( 1 );
        } else if( action == "RIGHT" ) {
            window.turn_page( -1 );
        }
    }
    if( which == drawer::actions ) {
        actions_page = window.page();
    }
    return window.open_map ? ACTION_MAP : window.chosen;
}

void open_on_map()
{
    start_on_map = true;
}

void request_actions()
{
    actions_requested = true;
}

bool take_actions_request()
{
    const bool was = actions_requested;
    actions_requested = false;
    return was;
}

#else // !TILES

action_id show( drawer )
{
    return ACTION_NULL;
}

void open_on_map()
{
}

void request_actions()
{
}

bool take_actions_request()
{
    return false;
}

#endif // TILES

} // namespace mobile_drawer
