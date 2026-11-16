#include "mobile_inventory.h"

#include "coordinates.h"
#include "item_location.h"

#if defined(TILES)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "cata_imgui.h"
#include "color.h"
#include "contents_change_handler.h"
#include "coordinates.h"
#include "enums.h"
#include "game.h"
#include "game_inventory.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "item.h"
#include "item_category.h"
#include "item_location.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "line.h"
#include "localized_comparator.h"
#include "map.h"
#include "map_selector.h"
#include "messages.h"
#include "mobile_ui.h"
#include "output.h"
#include "player_activity.h"
#include "pocket_type.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "uistate.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "veh_type.h"
#include "vpart_position.h"
#include "units.h"
#include "units_utility.h"

namespace mobile_inventory
{

using mobile_ui::face;

namespace
{

// colors of the canvas that mobile_ui does not name
constexpr ImU32 col_group_head = IM_COL32( 21, 25, 19, 255 );
constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_row_on = IM_COL32( 240, 145, 63, 31 );
constexpr ImU32 col_glyph_bg = IM_COL32( 5, 6, 5, 255 );
constexpr ImU32 col_glyph_line = IM_COL32( 255, 255, 255, 15 );
constexpr ImU32 col_tab_on = IM_COL32( 42, 49, 39, 255 );
constexpr ImU32 col_input = IM_COL32( 9, 11, 8, 255 );
constexpr ImU32 col_bottom_bar = IM_COL32( 18, 22, 16, 255 );
constexpr ImU32 col_desc = IM_COL32( 201, 207, 191, 255 );
constexpr ImU32 col_chip_text = IM_COL32( 195, 201, 185, 255 );
constexpr ImU32 col_meter_good = IM_COL32( 108, 191, 85, 255 );
constexpr ImU32 col_meter_low = IM_COL32( 230, 137, 45, 255 );
constexpr ImU32 col_button_soft = IM_COL32( 255, 255, 255, 15 );

// where an item is
enum class place : int {
    wielded,
    worn,
    pack,
    ground,
};

enum class act : int {
    consume,
    read,
    use,
    wear,
    wield,
    unwield,
    take_off,
    drop,
    open,
    pick_up,
    more,
};

struct action {
    act kind = act::more;
    std::string label;
};

// One row of a list: an item, or a stack of items that look the same.
struct entry {
    std::vector<item_location> locs;
    place where = place::pack;
    // what the stack is grouped under: the container, or the map tile
    const void *parent = nullptr;
    std::string name;
    std::string quantity;
    std::string sub;
    std::string meta_top;
    std::string meta_bottom;
    std::string symbol;
    ImU32 color = mobile_ui::col_fg;
    std::string category_id;
    std::string category;
    int category_rank = 0;
    std::string status;
    units::mass weight = 0_gram;
    units::volume volume = 0_ml;
};

struct fact {
    std::string label;
    std::string value;
};

// What the detail pane shows; built once when an item is selected.
struct item_detail {
    std::string description;
    std::vector<fact> facts;
    std::vector<action> actions;
};

enum class request_kind : int {
    none,
    close,
    action,
    pick_up,
};

struct request {
    request_kind kind = request_kind::none;
    act what = act::more;
    // the items to act on
    std::vector<item_location> locs;
};

// What survives closing and reopening the screen around an action.
struct screen_state {
    tab current = tab::carried;
    std::string filter;
    std::string search;
    std::string toast;
    // Not empty while the screen only offers what is at these tiles, to pick up.
    std::set<tripoint_bub_ms> targets;
    std::string title;
    // the newest message before an action that closed the screen for a while
    std::optional<std::string> message_before;
    bool select_wielded = false;
};

} // namespace

static screen_state state;

// --- Text helpers ---

static std::string format_weight( const units::mass &weight )
{
    const double value = convert_weight( weight );
    return string_format( value < 10.0 ? "%.2f %s" : "%.1f %s", value, weight_units() );
}

static std::string format_weight_number( const units::mass &weight )
{
    return string_format( "%.1f", convert_weight( weight ) );
}

static std::string format_vol( const units::volume &volume )
{
    return string_format( "%s %s", format_volume( volume ), volume_units_abbr() );
}

// The text cut to fit the width, ending in an ellipsis if it was cut.
static std::string fit( const face f, const float size_units, const std::string &text,
                        const float width )
{
    if( mobile_ui::text_size( f, size_units, text ).x <= width ) {
        return text;
    }
    std::string cut = text;
    while( !cut.empty() ) {
        // drop one UTF-8 character
        do {
            cut.pop_back();
        } while( !cut.empty() && ( static_cast<unsigned char>( cut.back() ) & 0xC0 ) == 0x80 );
        if( !cut.empty() && static_cast<unsigned char>( cut.back() ) >= 0xC0 ) {
            cut.pop_back();
        }
        while( !cut.empty() && cut.back() == ' ' ) {
            cut.pop_back();
        }
        if( mobile_ui::text_size( f, size_units, cut + "…" ).x <= width ) {
            break;
        }
    }
    return cut + "…";
}

static void text_centered( ImDrawList *const draw, const face f, const float size_units,
                           const ImVec2 &min, const ImVec2 &max, const ImU32 color, const std::string &text )
{
    const ImVec2 size = mobile_ui::text_size( f, size_units, text );
    mobile_ui::text( draw, f, size_units, ImVec2( min.x + ( max.x - min.x - size.x ) * 0.5f,
                     min.y + ( max.y - min.y - size.y ) * 0.5f ), color, text );
}

// The canvas's body font, for ImGui's own text field.
static ImFont *body_font()
{
    for( ImFont *const font : ImGui::GetIO().Fonts->Fonts ) {
        if( std::strstr( font->GetDebugName(), "Barlow-Medium" ) != nullptr ) {
            return font;
        }
    }
    return ImGui::GetFont();
}

static bool contains_no_case( const std::string &text, const std::string &part )
{
    return part.empty() || lcmatch( text, part );
}

// --- Reading the game ---

// The one thing to eat or drink that the item is, or holds.
static item *consumable_of( item &it )
{
    if( it.is_comestible() ) {
        return &it;
    }
    const std::list<item *> contents = it.all_items_top( pocket_type::CONTAINER );
    if( contents.size() == 1 && contents.front()->is_comestible() ) {
        return contents.front();
    }
    return nullptr;
}

static std::string consume_label( const item &food )
{
    const std::string &kind = food.get_comestible()->comesttype;
    if( kind == "DRINK" ) {
        return _( "Drink" );
    }
    if( kind == "MED" ) {
        return _( "Take" );
    }
    return _( "Eat" );
}

static std::string covered_parts( const item &it, const Character &you )
{
    std::vector<std::string> names;
    for( const bodypart_id &bp : you.get_all_body_parts() ) {
        if( !it.covers( bp ) ) {
            continue;
        }
        const std::string name = body_part_name_as_heading( bp, 2 );
        if( std::find( names.begin(), names.end(), name ) == names.end() ) {
            names.push_back( name );
        }
    }
    std::string joined;
    for( const std::string &name : names ) {
        joined += ( joined.empty() ? "" : ", " ) + name;
    }
    return uppercase_first_letter( joined );
}

static std::string layer_names( const item &it )
{
    std::string joined;
    for( const layer_level layer : it.get_layer() ) {
        const std::string name = item::layer_to_string( layer );
        if( joined.find( name ) == std::string::npos ) {
            joined += ( joined.empty() ? "" : ", " ) + name;
        }
    }
    return joined;
}

// The item's color, lifted where it would be too dark to read on the glyph box.
static ImU32 glyph_color( const item &it )
{
    ImVec4 color = cataimgui::imvec4_from_color( it.color() );
    const float brightest = std::max( { color.x, color.y, color.z } );
    if( brightest < 0.55f ) {
        const float lift = 0.55f - brightest;
        color.x += lift;
        color.y += lift;
        color.z += lift;
    }
    color.w = 1.0f;
    return ImGui::ColorConvertFloat4ToU32( color );
}

static void fill_entry_text( entry &e, const avatar &you, const std::string &direction )
{
    const item &it = *e.locs.front();
    const int count = static_cast<int>( e.locs.size() );
    e.name = uppercase_first_letter( remove_color_tags( it.tname( count, tname::unprefixed_tname ) ) );
    e.quantity = count > 1 ? string_format( "×%d", count ) : std::string();
    e.symbol = it.symbol();
    e.color = glyph_color( it );
    const item_category &category = it.get_category_of_contents();
    e.category_id = category.get_id().str();
    e.category = category.name_header();
    e.category_rank = category.sort_rank();
    e.weight = 0_gram;
    e.volume = 0_ml;
    for( const item_location &loc : e.locs ) {
        e.weight += loc->weight();
        e.volume += loc->volume();
    }
    switch( e.where ) {
        case place::wielded:
            e.status = _( "Wielded" );
            e.sub = e.status;
            break;
        case place::worn:
            e.status = _( "Worn" );
            e.sub = covered_parts( it, you );
            break;
        case place::pack:
            e.status = e.locs.front().has_parent() ?
                       string_format( _( "In your %s" ), e.locs.front().parent_item()->type_name() ) :
                       _( "In your pack" );
            e.sub = e.category;
            break;
        case place::ground:
            e.status = direction.empty() ? _( "On the ground here" ) :
                       string_format( _( "On the ground, %s" ), direction );
            e.sub = e.category;
            break;
    }
    if( e.where == place::worn ) {
        e.meta_top = layer_names( it );
        e.meta_bottom = string_format( _( "enc %d" ), it.get_avg_encumber( you ) );
    } else {
        e.meta_top = format_weight( e.weight );
        e.meta_bottom = e.where == place::ground ? ( direction.empty() ? _( "Here" ) : direction ) :
                        format_vol( e.volume );
    }
}

// Adds the item to the list, onto a stack of the same thing if there is one.
static void add_item( std::vector<entry> &list, const item_location &loc, const place where,
                      const void *const parent )
{
    for( entry &e : list ) {
        if( e.where == where && e.parent == parent && where != place::worn &&
            where != place::wielded && e.locs.front()->display_stacked_with( *loc ) ) {
            e.locs.push_back( loc );
            return;
        }
    }
    entry e;
    e.locs.push_back( loc );
    e.where = where;
    e.parent = parent;
    list.push_back( e );
}

static void sort_entries( std::vector<entry> &list )
{
    std::stable_sort( list.begin(), list.end(), []( const entry & a, const entry & b ) {
        const bool a_wielded = a.where == place::wielded;
        const bool b_wielded = b.where == place::wielded;
        if( a_wielded != b_wielded ) {
            return a_wielded;
        }
        if( a.category_rank != b.category_rank ) {
            return a.category_rank < b.category_rank;
        }
        if( a.category != b.category ) {
            return localized_compare( a.category, b.category );
        }
        return localized_compare( a.name, b.name );
    } );
}

static std::vector<entry> read_carried( avatar &you )
{
    std::vector<entry> list;
    const item_location weapon = you.get_wielded_item();
    if( weapon ) {
        add_item( list, weapon, place::wielded, nullptr );
    }
    for( const item_location &clothing : you.top_items_loc() ) {
        item_location parent = clothing;
        for( item *const inside : parent->all_items_top( pocket_type::CONTAINER ) ) {
            add_item( list, item_location( parent, inside ), place::pack, parent.get_item() );
        }
    }
    for( entry &e : list ) {
        fill_entry_text( e, you, std::string() );
    }
    sort_entries( list );
    return list;
}

static std::vector<entry> read_worn( avatar &you )
{
    std::vector<entry> list;
    for( const item_location &clothing : you.top_items_loc() ) {
        add_item( list, clothing, place::worn, nullptr );
    }
    for( entry &e : list ) {
        fill_entry_text( e, you, std::string() );
    }
    return list;
}

// Items on the avatar's tile and the tiles around it that can be reached, as
// upstream's pick-up menu collects them: what lies on the map, and what is in
// vehicle cargo there.  With targets, only those tiles.
static std::vector<entry> read_nearby( avatar &you, const std::set<tripoint_bub_ms> &targets )
{
    std::vector<entry> list;
    map &here = get_map();
    const tripoint_bub_ms center = you.pos_bub();
    std::vector<tripoint_bub_ms> tiles( targets.begin(), targets.end() );
    if( tiles.empty() ) {
        for( const tripoint_bub_ms &pos : closest_points_first( center, 1 ) ) {
            if( pos == center || here.clear_path( center, pos, rl_dist( center, pos ), 1, 100 ) ) {
                tiles.push_back( pos );
            }
        }
    }
    const auto can_pick = []( const item & it ) {
        return !it.made_of_from_type( phase_id::GAS ) &&
               ( !it.made_of_from_type( phase_id::LIQUID ) || it.is_frozen_liquid() );
    };
    for( const tripoint_bub_ms &pos : tiles ) {
        const std::string direction = pos == center ? std::string() :
                                      direction_name_short( direction_from( center.raw(), pos.raw() ) );
        // stacks are made per tile, and apart for the map and the vehicle
        std::vector<entry> tile;
        if( here.accessible_items( pos ) ) {
            for( item &it : here.i_at( pos ) ) {
                if( can_pick( it ) ) {
                    add_item( tile, item_location( map_cursor( pos ), &it ), place::ground, &here );
                }
            }
        }
        if( const std::optional<vpart_reference> cargo = here.veh_at( pos ).cargo() ) {
            const vehicle_cursor cursor( cargo->vehicle(), cargo->part_index() );
            for( item &it : cargo->items() ) {
                if( can_pick( it ) ) {
                    add_item( tile, item_location( cursor, &it ), place::ground, &cargo->vehicle() );
                }
            }
        }
        for( entry &e : tile ) {
            fill_entry_text( e, you, direction );
            list.push_back( e );
        }
    }
    sort_entries( list );
    return list;
}

// What to call a place that things are picked up from.
static std::string place_name( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( const std::optional<vpart_reference> cargo = here.veh_at( pos ).cargo() ) {
        if( here.i_at( pos ).empty() ) {
            return uppercase_first_letter( remove_color_tags( cargo->part().name() ) );
        }
    }
    return uppercase_first_letter( here.name( pos ) );
}

static item_detail read_detail( const entry &e, avatar &you )
{
    item_detail d;
    item_location loc = e.locs.front();
    item &it = *loc;

    // the flavor text, the way upstream's item info composes it
    const iteminfo_query query( std::vector<iteminfo_parts> { iteminfo_parts::DESCRIPTION } );
    std::vector<iteminfo> info;
    it.info( info, &query );
    for( const iteminfo &part : info ) {
        if( part.sType == "DESCRIPTION" && !part.sName.empty() &&
            part.sName.find_first_not_of( "-" ) != std::string::npos ) {
            d.description = remove_color_tags( part.sName );
            break;
        }
    }

    d.facts.push_back( { _( "Weight" ), format_weight( e.weight ) } );
    d.facts.push_back( { _( "Volume" ), format_vol( e.volume ) } );
    d.facts.push_back( { _( "Type" ), e.category } );
    const std::string condition = trim( remove_color_tags( it.durability_indicator( true ) ) );
    if( !condition.empty() ) {
        d.facts.push_back( { _( "Condition" ), uppercase_first_letter( condition ) } );
    }
    if( it.is_armor() ) {
        d.facts.push_back( { _( "Layer" ), layer_names( it ) } );
        d.facts.push_back( { _( "Warmth" ), string_format( "%d", it.get_warmth() ) } );
        d.facts.push_back( { _( "Encumbrance" ), string_format( "%d", it.get_avg_encumber( you ) ) } );
        const std::string covers = covered_parts( it, you );
        if( !covers.empty() ) {
            d.facts.push_back( { _( "Covers" ), covers } );
        }
    }

    const auto add = [&d]( const act kind, const std::string & label ) {
        d.actions.push_back( { kind, label } );
    };
    const item *const food = consumable_of( it );
    const bool usable = it.type->has_use();
    const bool has_contents = it.is_container() && !it.is_corpse() && it.num_item_stacks() > 0;
    switch( e.where ) {
        case place::ground:
            add( act::pick_up, _( "Pick up" ) );
            add( act::wield, _( "Wield" ) );
            if( it.is_armor() ) {
                add( act::wear, _( "Wear" ) );
            }
            break;
        case place::worn:
            add( act::take_off, _( "Take off" ) );
            if( usable ) {
                add( act::use, _( "Use" ) );
            }
            add( act::drop, _( "Drop" ) );
            break;
        case place::wielded:
        case place::pack: {
            if( e.where == place::wielded ) {
                add( act::unwield, _( "Unwield" ) );
            }
            if( food != nullptr ) {
                add( act::consume, consume_label( *food ) );
            }
            if( it.is_book() ) {
                add( act::read, _( "Read" ) );
            }
            if( it.is_armor() ) {
                add( act::wear, _( "Wear" ) );
            }
            if( usable && food == nullptr ) {
                add( act::use, _( "Use" ) );
            }
            // something made to be held offers that before dropping it
            const bool holdable = it.is_tool() || it.is_gun() || e.category_id == "weapons";
            if( e.where == place::pack && holdable ) {
                add( act::wield, _( "Wield" ) );
            }
            add( act::drop, _( "Drop" ) );
            if( e.where == place::pack && !holdable ) {
                add( act::wield, _( "Wield" ) );
            }
            break;
        }
    }
    if( has_contents && e.where != place::ground ) {
        add( act::open, _( "Open" ) );
    }
    add( act::more, _( "More…" ) );
    return d;
}

// --- Doing things, with upstream's functions ---

static void perform( const request &req )
{
    avatar &you = get_avatar();
    if( req.locs.empty() || !req.locs.front() ) {
        return;
    }
    item_location loc = req.locs.front();
    switch( req.what ) {
        case act::consume: {
            if( loc->is_comestible() ) {
                avatar_action::eat_or_use( you, loc );
            } else if( item *const food = consumable_of( *loc ) ) {
                avatar_action::eat_or_use( you, item_location( loc, food ) );
            }
            break;
        }
        case act::read:
            you.read( loc );
            break;
        case act::use: {
            // as upstream's item menu does for "activate"; uses that only the
            // contents have are under "More"
            contents_change_handler handler;
            handler.unseal_pocket_containing( loc );
            avatar_action::use_item( you, loc );
            handler.handle_by( you );
            break;
        }
        case act::wear: {
            contents_change_handler handler;
            handler.unseal_pocket_containing( loc );
            you.wear( loc );
            handler.handle_by( you );
            break;
        }
        case act::wield: {
            const ret_val<void> can = you.can_wield( *loc );
            if( can.success() ) {
                contents_change_handler handler;
                handler.unseal_pocket_containing( loc );
                you.wield( loc );
                handler.handle_by( you );
                state.select_wielded = true;
            } else {
                add_msg( m_info, "%s", can.c_str() );
            }
            break;
        }
        case act::unwield:
            you.unwield();
            break;
        case act::take_off:
            you.takeoff( loc );
            break;
        case act::drop: {
            drop_locations what;
            for( const item_location &one : req.locs ) {
                what.emplace_back( one, one->count() );
            }
            you.drop( what, you.pos_bub() );
            break;
        }
        case act::open:
            game_menus::inv::common( loc );
            break;
        case act::pick_up: {
            drop_locations what;
            for( const item_location &one : req.locs ) {
                what.emplace_back( one, one->count() );
            }
            you.pick_up( what );
            break;
        }
        case act::more:
            g->inventory_item_menu( loc );
            break;
    }
}

// --- The screen ---

namespace
{

class inventory_window : public cataimgui::window
{
    public:
        inventory_window() : cataimgui::window( "MOBILE_INVENTORY",
                                                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                    ImGuiWindowFlags_NoBackground ) {
            reload();
        }

        request pending;

        // Go back one step.  Returns false if there is nothing to step back from.
        bool back() {
            if( !landscape && sheet_open ) {
                sheet_open = false;
                return true;
            }
            return false;
        }
        // A sideways swipe (or arrow key) moves the row of category chips.
        void scroll_chips( const float units ) {
            chip_scroll += units;
        }

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        std::array<std::vector<entry>, 3> lists;
        // index into the current tab's list
        int selected = -1;
        item_detail selected_detail;
        bool sheet_open = false;
        bool landscape = true;
        // indexes into the nearby list
        std::set<int> picks;
        float chip_scroll = 0.0f;

        void reload();
        void select( int index );
        std::vector<entry> &current() {
            return lists[static_cast<size_t>( state.current )];
        }
        void switch_tab( tab to );
        bool shown( const entry &e ) const;

        void draw_left( const ImVec2 &pos, const ImVec2 &size, bool interactive );
        void draw_meter( const ImVec2 &pos, float width, const std::string &label, double value,
                         double capacity, const std::string &value_text );
        void draw_list( float width, bool interactive );
        void draw_row( ImDrawList *draw, int index, const ImVec2 &pos, float width, bool interactive );
        void draw_glyph( ImDrawList *draw, const entry &e, const ImVec2 &pos, float box_units,
                         float font_units, float radius_units ) const;
        bool button( ImDrawList *draw, const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                     const std::string &label, float font_units, bool primary, bool enabled ) const;
        // Lays out the detail of the selected item; draws it if draw is not null.
        // Returns the height it takes.
        float detail_content( ImDrawList *draw, const ImVec2 &base, float width );
        void draw_detail_pane( const ImVec2 &pos, const ImVec2 &size );
        void draw_detail_sheet( const ImVec2 &origin, const ImVec2 &size );
};

void inventory_window::reload()
{
    avatar &you = get_avatar();
    lists[static_cast<size_t>( tab::carried )] = read_carried( you );
    lists[static_cast<size_t>( tab::worn )] = read_worn( you );
    lists[static_cast<size_t>( tab::nearby )] = read_nearby( you, state.targets );
    picks.clear();
    selected = -1;
    sheet_open = false;
    if( state.select_wielded ) {
        state.select_wielded = false;
        if( state.current == tab::carried ) {
            const std::vector<entry> &list = current();
            for( size_t i = 0; i < list.size(); ++i ) {
                if( list[i].where == place::wielded ) {
                    selected = static_cast<int>( i );
                    selected_detail = read_detail( list[i], you );
                }
            }
        }
    }
}

void inventory_window::select( const int index )
{
    selected = index;
    state.toast.clear();
    if( index >= 0 ) {
        selected_detail = read_detail( current()[index], get_avatar() );
        sheet_open = true;
    }
}

void inventory_window::switch_tab( const tab to )
{
    if( to == state.current ) {
        return;
    }
    state.current = to;
    state.toast.clear();
    state.filter.clear();
    selected = -1;
    sheet_open = false;
    chip_scroll = 0.0f;
}

bool inventory_window::shown( const entry &e ) const
{
    if( state.current != tab::worn && !state.filter.empty() && e.category_id != state.filter ) {
        return false;
    }
    return contains_no_case( e.name, state.search );
}

bool inventory_window::button( ImDrawList *const draw, const std::string &id, const ImVec2 &pos,
                               const ImVec2 &size, const std::string &label, const float font_units,
                               const bool primary, const bool enabled ) const
{
    bool held = false;
    const bool tapped = enabled && mobile_ui::pressable( id, pos, size, &held );
    const ImVec2 max( pos.x + size.x, pos.y + size.y );
    ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                 ( held ? mobile_ui::col_button_down : col_button_soft );
    ImU32 ink = primary ? mobile_ui::col_on_accent : mobile_ui::col_fg;
    if( !enabled ) {
        // the canvas shows a disabled button at 40% opacity
        fill = ( fill & ~IM_COL32_A_MASK ) | ( ( ( fill >> IM_COL32_A_SHIFT ) * 2 / 5 ) <<
                                               IM_COL32_A_SHIFT );
        ink = ( ink & ~IM_COL32_A_MASK ) | ( 102u << IM_COL32_A_SHIFT );
    }
    mobile_ui::box( draw, pos, max, fill, primary ? 0 : mobile_ui::col_line, 12.0f );
    text_centered( draw, face::bold, font_units, pos, max, ink,
                   fit( face::bold, font_units, label, size.x - 12.0f * mobile_ui::unit() ) );
    return tapped;
}

void inventory_window::draw_glyph( ImDrawList *const draw, const entry &e, const ImVec2 &pos,
                                   const float box_units, const float font_units, const float radius_units ) const
{
    const float u = mobile_ui::unit();
    const ImVec2 max( pos.x + box_units * u, pos.y + box_units * u );
    mobile_ui::box( draw, pos, max, col_glyph_bg, col_glyph_line, radius_units );
    text_centered( draw, face::mono, font_units, pos, max, e.color, e.symbol );
}

void inventory_window::draw_meter( const ImVec2 &pos, const float width, const std::string &label,
                                   const double value, const double capacity, const std::string &value_text )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float row_h = 14.0f * u;
    const ImVec2 label_size = mobile_ui::text_size( face::body, 12.0f, label );
    mobile_ui::text( draw, face::body, 12.0f, ImVec2( pos.x, pos.y + ( row_h - label_size.y ) * 0.5f ),
                     mobile_ui::col_muted, label );
    const ImVec2 value_size = mobile_ui::text_size( face::mono, 11.5f, value_text );
    mobile_ui::text( draw, face::mono, 11.5f, ImVec2( pos.x + width - value_size.x,
                     pos.y + ( row_h - value_size.y ) * 0.5f ), mobile_ui::col_fg, value_text );
    const ImVec2 bar_min( pos.x + 62.0f * u, pos.y + 3.5f * u );
    const ImVec2 bar_max( pos.x + width - value_size.x - 8.0f * u, pos.y + 10.5f * u );
    if( bar_max.x <= bar_min.x ) {
        return;
    }
    draw->AddRectFilled( bar_min, bar_max, IM_COL32( 255, 255, 255, 20 ), 4.0f * u );
    const double part = capacity > 0.0 ? std::clamp( value / capacity, 0.0, 1.0 ) :
                        ( value > 0.0 ? 1.0 : 0.0 );
    if( part > 0.0 ) {
        const float fill_w = std::max( ( bar_max.x - bar_min.x ) * static_cast<float>( part ), 4.0f * u );
        draw->AddRectFilled( bar_min, ImVec2( bar_min.x + fill_w, bar_max.y ),
                             part > 0.85 ? col_meter_low : col_meter_good, 4.0f * u );
    }
}

void inventory_window::draw_row( ImDrawList *const draw, const int index, const ImVec2 &pos,
                                 const float width, const bool interactive )
{
    const float u = mobile_ui::unit();
    const entry &e = current()[index];
    const bool nearby = state.current == tab::nearby;
    const float row_h = 56.0f * u;
    const ImVec2 max( pos.x + width, pos.y + row_h );
    const std::string id = std::to_string( index );
    const bool picked = nearby && picks.count( index ) > 0;
    const bool on = nearby ? picked : ( landscape && selected == index );

    float right = max.x - 14.0f * u;
    bool held = false;
    bool chevron_held = false;
    if( nearby ) {
        // the row selects it for picking up; the arrow at the end shows what it is
        const float chevron_w = 36.0f * u;
        const ImVec2 chevron_pos( right - chevron_w, pos.y + 6.0f * u );
        if( interactive ) {
            if( mobile_ui::pressable( "##row" + id, pos, ImVec2( chevron_pos.x - pos.x, row_h ), &held ) ) {
                if( picked ) {
                    picks.erase( index );
                } else {
                    picks.insert( index );
                }
            }
            if( mobile_ui::pressable( "##about" + id, chevron_pos, ImVec2( chevron_w + 14.0f * u,
                                      44.0f * u ), &chevron_held ) ) {
                select( index );
            }
        }
        right = chevron_pos.x - 12.0f * u;
    } else if( interactive && mobile_ui::pressable( "##row" + id, pos, ImVec2( width, row_h ),
               &held ) ) {
        select( index );
    }

    if( on ) {
        draw->AddRectFilled( pos, max, col_row_on );
    } else if( held ) {
        draw->AddRectFilled( pos, max, mobile_ui::col_fill_soft );
    }
    draw->AddLine( ImVec2( pos.x, max.y ), max, col_row_line, std::max( 1.0f, u ) );

    float x = pos.x + 14.0f * u;
    if( nearby ) {
        const ImVec2 box_min( x, pos.y + 15.0f * u );
        const ImVec2 box_max( x + 26.0f * u, pos.y + 41.0f * u );
        if( picked ) {
            draw->AddRectFilled( box_min, box_max, mobile_ui::col_accent, 7.0f * u );
            mobile_ui::icon( draw, "check", ImVec2( box_min.x + 5.0f * u, box_min.y + 5.0f * u ), 16.0f,
                             mobile_ui::col_on_accent );
        } else {
            draw->AddRect( box_min, box_max, IM_COL32( 255, 255, 255, 77 ), 7.0f * u, 0, 1.5f * u );
        }
        x += 38.0f * u;
        if( chevron_held ) {
            mobile_ui::box( draw, ImVec2( max.x - 50.0f * u, pos.y + 6.0f * u ),
                            ImVec2( max.x - 14.0f * u, pos.y + 50.0f * u ), mobile_ui::col_fill_soft, 0, 12.0f );
        }
        mobile_ui::icon( draw, "chev", ImVec2( max.x - 43.0f * u, pos.y + 17.0f * u ), 22.0f,
                         mobile_ui::col_muted );
    }
    draw_glyph( draw, e, ImVec2( x, pos.y + 10.0f * u ), 36.0f, 19.0f, 9.0f );
    x += 48.0f * u;

    // weight and volume (or layer and encumbrance) at the right
    const float meta_line = 11.0f * 1.35f * u;
    const ImVec2 top_size = mobile_ui::text_size( face::mono, 11.0f, e.meta_top );
    const ImVec2 bottom_size = mobile_ui::text_size( face::mono, 11.0f, e.meta_bottom );
    const float meta_w = std::min( std::max( top_size.x, bottom_size.x ), ( right - x ) * 0.45f );
    const float meta_y = pos.y + ( row_h - meta_line * 2.0f ) * 0.5f;
    const std::string meta_top = fit( face::mono, 11.0f, e.meta_top, meta_w );
    const std::string meta_bottom = fit( face::mono, 11.0f, e.meta_bottom, meta_w );
    mobile_ui::text( draw, face::mono, 11.0f, ImVec2( right - mobile_ui::text_size( face::mono, 11.0f,
                     meta_top ).x, meta_y ), mobile_ui::col_muted, meta_top );
    mobile_ui::text( draw, face::mono, 11.0f, ImVec2( right - mobile_ui::text_size( face::mono, 11.0f,
                     meta_bottom ).x, meta_y + meta_line ), mobile_ui::col_muted, meta_bottom );

    // name with quantity, and a line under it
    const float text_w = right - meta_w - 12.0f * u - x;
    const ImVec2 quantity_size = e.quantity.empty() ? ImVec2( 0.0f, 0.0f ) :
                                 mobile_ui::text_size( face::body, 12.5f, e.quantity );
    const float quantity_w = e.quantity.empty() ? 0.0f : quantity_size.x + 5.0f * u;
    const std::string name = fit( face::bold, 14.5f, e.name, text_w - quantity_w );
    const ImVec2 name_size = mobile_ui::text_size( face::bold, 14.5f, name );
    const std::string sub = fit( face::body, 12.0f, e.sub, text_w );
    const ImVec2 sub_size = mobile_ui::text_size( face::body, 12.0f, sub );
    const float text_y = pos.y + ( row_h - name_size.y - sub_size.y - 1.0f * u ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( x, text_y ), mobile_ui::col_fg, name );
    if( !e.quantity.empty() ) {
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( x + name_size.x + 5.0f * u,
                         text_y + name_size.y - quantity_size.y - 0.5f * u ), mobile_ui::col_muted, e.quantity );
    }
    mobile_ui::text( draw, face::body, 12.0f, ImVec2( x, text_y + name_size.y + 1.0f * u ),
                     mobile_ui::col_muted, sub );
}

void inventory_window::draw_list( const float width, const bool interactive )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const ImVec2 clip_min = draw->GetClipRectMin();
    const ImVec2 clip_max = draw->GetClipRectMax();
    const std::vector<entry> &list = current();
    const float row_h = 56.0f * u;
    const float head_h = 27.0f * u;
    float y = base.y;

    std::vector<int> visible;
    for( size_t i = 0; i < list.size(); ++i ) {
        if( shown( list[i] ) ) {
            visible.push_back( static_cast<int>( i ) );
        }
    }
    if( visible.empty() ) {
        const std::string message = state.current == tab::nearby && list.empty() ?
                                    _( "Nothing on the ground around you." ) : _( "No items match." );
        const float wrap = width - 40.0f * u;
        const ImVec2 size = mobile_ui::text_size( face::body, 13.5f, message, wrap );
        mobile_ui::text( draw, face::body, 13.5f, ImVec2( base.x + ( width - size.x ) * 0.5f,
                         y + 28.0f * u ), mobile_ui::col_muted, message, wrap );
        ImGui::Dummy( ImVec2( 1.0f, size.y + 56.0f * u ) );
        return;
    }

    const bool grouped = state.current != tab::worn;
    // the group a row belongs to: "Wielded", or its category
    const auto group_of = [&list]( const int index ) {
        const entry &e = list[index];
        return e.where == place::wielded ? std::string( " wielded" ) : e.category_id;
    };
    for( size_t n = 0; n < visible.size(); ++n ) {
        const int index = visible[n];
        if( grouped && ( n == 0 || group_of( visible[n - 1] ) != group_of( index ) ) ) {
            const entry &e = list[index];
            if( y + head_h >= clip_min.y && y <= clip_max.y ) {
                draw->AddRectFilled( ImVec2( base.x, y ), ImVec2( base.x + width, y + head_h ), col_group_head );
                draw->AddLine( ImVec2( base.x, y + head_h ), ImVec2( base.x + width, y + head_h ),
                               col_row_line, std::max( 1.0f, u ) );
                const bool wielded = e.where == place::wielded;
                mobile_ui::text( draw, face::mono, 10.5f, ImVec2( base.x + 14.0f * u, y + 8.0f * u ),
                                 mobile_ui::col_muted, to_upper_case( wielded ? _( "Wielded" ) : e.category ) );
                if( !wielded ) {
                    units::mass total = 0_gram;
                    for( size_t m = n; m < visible.size() && group_of( visible[m] ) == group_of( index ); ++m ) {
                        total += list[visible[m]].weight;
                    }
                    const std::string weight = to_upper_case( format_weight( total ) );
                    mobile_ui::text( draw, face::mono, 10.5f,
                                     ImVec2( base.x + width - 14.0f * u - mobile_ui::text_size( face::mono, 10.5f, weight ).x,
                                             y + 8.0f * u ), mobile_ui::col_muted, weight );
                }
            }
            y += head_h;
        }
        if( y + row_h >= clip_min.y && y <= clip_max.y ) {
            draw_row( draw, index, ImVec2( base.x, y ), width, interactive );
        }
        y += row_h;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
}

void inventory_window::draw_left( const ImVec2 &pos, const ImVec2 &size, const bool interactive )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    avatar &you = get_avatar();
    const float right = pos.x + size.x;
    float y = pos.y;

    // only one place to pick up from: no tabs, and its name as the title
    const bool limited = !state.targets.empty();
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( limited ? state.title : _( "Inventory" ), pos, size.x, &bar_h ) &&
        interactive ) {
        pending.kind = request_kind::close;
    }
    y += bar_h;

    // weight and volume
    y += 10.0f * u;
    const units::mass weight = you.weight_carried();
    const units::mass weight_max = you.weight_capacity();
    draw_meter( ImVec2( pos.x + 14.0f * u, y ), size.x - 28.0f * u, _( "Weight" ),
                units::to_gram<double>( weight ), units::to_gram<double>( weight_max ),
                string_format( "%s / %s %s", format_weight_number( weight ),
                               format_weight_number( weight_max ), weight_units() ) );
    y += 19.0f * u;
    // how full the pockets are: what is in them, of what they hold
    const units::volume volume_max = you.volume_capacity();
    units::volume volume = 0_ml;
    for( const item_location &holder : you.top_items_loc() ) {
        volume += holder->get_contents_volume();
    }
    if( const item_location weapon = you.get_wielded_item() ) {
        volume += weapon->get_contents_volume();
    }
    draw_meter( ImVec2( pos.x + 14.0f * u, y ), size.x - 28.0f * u, _( "Volume" ),
                units::to_milliliter<double>( volume ), units::to_milliliter<double>( volume_max ),
                string_format( "%s / %s %s", format_volume( volume ), format_volume( volume_max ),
                               volume_units_abbr() ) );
    y += 18.0f * u;

    // tabs
    if( !limited ) {
        y += 10.0f * u;
        const ImVec2 tabs_min( pos.x + 12.0f * u, y );
        const ImVec2 tabs_max( right - 12.0f * u, y + 44.0f * u );
        mobile_ui::box( draw, tabs_min, tabs_max, mobile_ui::col_fill_soft, 0, 12.0f );
        const std::array<std::pair<tab, std::string>, 3> tabs = { {
                { tab::carried, _( "Carried" ) }, { tab::worn, _( "Worn" ) }, { tab::nearby, _( "Nearby" ) }
            }
        };
        const float tab_w = ( tabs_max.x - tabs_min.x - 6.0f * u ) / 3.0f;
        for( size_t i = 0; i < tabs.size(); ++i ) {
            const ImVec2 tab_min( tabs_min.x + 3.0f * u + tab_w * static_cast<float>( i ), y + 3.0f * u );
            const ImVec2 tab_max( tab_min.x + tab_w, y + 41.0f * u );
            const bool on = state.current == tabs[i].first;
            if( interactive && mobile_ui::pressable( "##tab" + std::to_string( i ), ImVec2( tab_min.x, y ),
                    ImVec2( tab_w, 44.0f * u ) ) ) {
                switch_tab( tabs[i].first );
            }
            if( on ) {
                mobile_ui::box( draw, tab_min, tab_max, col_tab_on, 0, 9.0f );
            }
            const ImU32 ink = on ? mobile_ui::col_fg : mobile_ui::col_muted;
            const ImVec2 label_size = mobile_ui::text_size( face::bold, 13.5f, tabs[i].second );
            if( tabs[i].first != tab::nearby ) {
                text_centered( draw, face::bold, 13.5f, tab_min, tab_max, ink, tabs[i].second );
                continue;
            }
            // Nearby carries a count of what is there
            const std::string count = std::to_string( lists[static_cast<size_t>( tab::nearby )].size() );
            const ImVec2 count_size = mobile_ui::text_size( face::mono, 10.5f, count );
            const float badge_w = count_size.x + 12.0f * u;
            const float badge_h = count_size.y + 2.0f * u;
            const float total_w = label_size.x + 6.0f * u + badge_w;
            const float label_x = tab_min.x + ( tab_w - total_w ) * 0.5f;
            const float mid_y = ( tab_min.y + tab_max.y ) * 0.5f;
            mobile_ui::text( draw, face::bold, 13.5f, ImVec2( label_x, mid_y - label_size.y * 0.5f ), ink,
                             tabs[i].second );
            const ImVec2 badge_min( label_x + label_size.x + 6.0f * u, mid_y - badge_h * 0.5f );
            const ImVec2 badge_max( badge_min.x + badge_w, badge_min.y + badge_h );
            draw->AddRectFilled( badge_min, badge_max, IM_COL32( 255, 255, 255, 31 ), badge_h * 0.5f );
            text_centered( draw, face::mono, 10.5f, badge_min, badge_max, ink, count );
        }
        y += 44.0f * u;
    }

    // search
    y += 10.0f * u;
    {
        const ImVec2 field_pos( pos.x + 12.0f * u, y );
        const float field_w = size.x - 24.0f * u;
        const float font_px = 15.0f * u;
        ImGui::PushFont( body_font(), font_px );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 38.0f * u,
                             ( 44.0f * u - ImGui::GetFontSize() ) * 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 11.0f * u );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, u ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, col_input );
        ImGui::PushStyleColor( ImGuiCol_Border, mobile_ui::col_line );
        ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
        ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
        ImGui::SetCursorScreenPos( field_pos );
        ImGui::SetNextItemWidth( field_w );
        ImGui::BeginDisabled( !interactive );
        ImGui::InputTextWithHint( "##inventory_search", state.current == tab::nearby ?
                                  _( "Search nearby items" ) : _( "Search your items" ), &state.search );
        ImGui::EndDisabled();
        if( ImGui::IsItemActive() ) {
            draw->AddRect( field_pos, ImVec2( field_pos.x + field_w, field_pos.y + 44.0f * u ),
                           mobile_ui::col_accent, 11.0f * u, 0, 2.0f * u );
        }
        ImGui::PopStyleColor( 4 );
        ImGui::PopStyleVar( 3 );
        ImGui::PopFont();
        mobile_ui::icon( draw, "search", ImVec2( pos.x + 24.0f * u, y + 13.0f * u ), 18.0f,
                         mobile_ui::col_muted );
        y += 44.0f * u;
    }

    // category chips: "All" and the categories that are there
    if( state.current != tab::worn ) {
        std::vector<std::pair<std::string, std::string>> chips = { { std::string(), _( "All" ) } };
        {
            const std::vector<entry> &list = current();
            bool filter_present = state.filter.empty();
            for( const entry &e : list ) {
                const auto same = [&e]( const std::pair<std::string, std::string> &chip ) {
                    return chip.first == e.category_id;
                };
                if( std::none_of( chips.begin(), chips.end(), same ) ) {
                    chips.emplace_back( e.category_id, e.category );
                }
                filter_present = filter_present || e.category_id == state.filter;
            }
            if( !filter_present ) {
                state.filter.clear();
            }
        }
        const float chips_h = 49.0f * u;
        float content_w = 12.0f * u;
        for( const std::pair<std::string, std::string> &chip : chips ) {
            content_w += mobile_ui::text_size( face::bold, 13.0f, chip.second ).x + 26.0f * u + 6.0f * u;
        }
        content_w += 6.0f * u;
        ImGui::SetCursorScreenPos( ImVec2( pos.x, y ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ImGui::BeginChild( "##inventory_chips", ImVec2( size.x, chips_h ), ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse );
        chip_scroll = std::clamp( chip_scroll, 0.0f, std::max( 0.0f, ( content_w - size.x ) / u ) );
        ImGui::SetScrollX( chip_scroll * u );
        ImDrawList *const inner = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        float x = base.x + 12.0f * u;
        for( size_t i = 0; i < chips.size(); ++i ) {
            const float chip_w = mobile_ui::text_size( face::bold, 13.0f, chips[i].second ).x + 26.0f * u;
            const ImVec2 chip_min( x, base.y + 10.0f * u );
            const ImVec2 chip_max( x + chip_w, base.y + 39.0f * u );
            const bool on = state.filter == chips[i].first;
            bool held = false;
            if( interactive && mobile_ui::pressable( "##chip" + std::to_string( i ),
                    ImVec2( x - 3.0f * u, base.y + 2.0f * u ), ImVec2( chip_w + 6.0f * u, 45.0f * u ), &held ) ) {
                state.filter = chips[i].first;
            }
            inner->AddRectFilled( chip_min, chip_max, on ? mobile_ui::col_accent :
                                  ( held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 10 ) ), 14.5f * u );
            if( !on ) {
                inner->AddRect( chip_min, chip_max, mobile_ui::col_line, 14.5f * u, 0, std::max( 1.0f, u ) );
            }
            text_centered( inner, face::bold, 13.0f, chip_min, chip_max,
                           on ? mobile_ui::col_on_accent : col_chip_text, chips[i].second );
            x += chip_w + 6.0f * u;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( content_w, 1.0f ) );
        ImGui::EndChild();
        ImGui::PopStyleVar();
        y += chips_h;
    } else {
        y += 10.0f * u;
    }

    // the list, and under it the pick-up bar on Nearby
    const bool nearby = state.current == tab::nearby;
    const float bottom = pos.y + size.y;
    const float bottom_bar_h = nearby ? 68.0f * u + safe.w : 0.0f;
    const float list_h = std::max( bottom - bottom_bar_h - y, 1.0f );
    ImGui::SetCursorScreenPos( ImVec2( pos.x, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( ( "##inventory_list" + std::to_string( static_cast<int>
                         ( state.current ) ) ).c_str(), ImVec2( size.x, list_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    draw_list( size.x, interactive );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if( nearby ) {
        const std::vector<entry> &list = lists[static_cast<size_t>( tab::nearby )];
        const ImVec2 bar_min( pos.x, bottom - bottom_bar_h );
        draw->AddRectFilled( bar_min, ImVec2( right, bottom ), col_bottom_bar );
        draw->AddLine( bar_min, ImVec2( right, bar_min.y ), mobile_ui::col_line, std::max( 1.0f, u ) );
        const ImVec2 button_pos( pos.x + 12.0f * u, bar_min.y + 10.0f * u );
        const float buttons_w = size.x - 24.0f * u;
        if( picks.empty() ) {
            if( button( draw, "##pick_all", button_pos, ImVec2( buttons_w, 48.0f * u ),
                        _( "Pick up everything" ), 15.0f, true, interactive && !list.empty() ) ) {
                pending.kind = request_kind::pick_up;
                for( const entry &e : list ) {
                    pending.locs.insert( pending.locs.end(), e.locs.begin(), e.locs.end() );
                }
            }
        } else {
            units::mass total = 0_gram;
            for( const int index : picks ) {
                total += list[index].weight;
            }
            const float half = ( buttons_w - 8.0f * u ) * 0.5f;
            if( button( draw, "##pick_clear", button_pos, ImVec2( half, 48.0f * u ), _( "Clear" ), 15.0f,
                        false, interactive ) ) {
                picks.clear();
            }
            if( button( draw, "##pick_up", ImVec2( button_pos.x + half + 8.0f * u, button_pos.y ),
                        ImVec2( half, 48.0f * u ),
                        string_format( _( "Pick up %1$d · %2$s" ), static_cast<int>( picks.size() ),
                                       format_weight( total ) ), 15.0f, true, interactive ) ) {
                pending.kind = request_kind::pick_up;
                for( const int index : picks ) {
                    pending.locs.insert( pending.locs.end(), list[index].locs.begin(), list[index].locs.end() );
                }
            }
        }
    }
}

float inventory_window::detail_content( ImDrawList *const draw, const ImVec2 &base,
                                        const float width )
{
    const float u = mobile_ui::unit();
    const std::vector<entry> &list = current();
    if( selected < 0 || selected >= static_cast<int>( list.size() ) ) {
        const std::string message =
            _( "Select an item to see what it is and what you can do with it." );
        const float wrap = width - 40.0f * u;
        const ImVec2 size = mobile_ui::text_size( face::body, 13.5f, message, wrap );
        if( draw != nullptr ) {
            mobile_ui::text( draw, face::body, 13.5f, ImVec2( base.x + ( width - size.x ) * 0.5f,
                             base.y + 28.0f * u ), mobile_ui::col_muted, message, wrap );
        }
        return size.y + 56.0f * u;
    }
    const entry &e = list[selected];
    const item_detail &d = selected_detail;
    const float inner_w = width - 32.0f * u;
    float y = base.y + 4.0f * u;

    // head: glyph, name, where it is
    {
        const float text_x = base.x + 74.0f * u;
        const float text_w = base.x + width - 16.0f * u - text_x;
        const float name_h = mobile_ui::text_size( face::display, 20.0f, e.name, text_w ).y;
        const std::string status = e.quantity.empty() ? e.status : e.quantity + " · " + e.status;
        const float status_h = mobile_ui::text_size( face::body, 12.5f, status, text_w ).y;
        const float head_h = std::max( 46.0f * u, name_h + 2.0f * u + status_h );
        if( draw != nullptr ) {
            draw_glyph( draw, e, ImVec2( base.x + 16.0f * u, y + ( head_h - 46.0f * u ) * 0.5f ), 46.0f,
                        24.0f, 12.0f );
            const float text_y = y + ( head_h - name_h - 2.0f * u - status_h ) * 0.5f;
            mobile_ui::text( draw, face::display, 20.0f, ImVec2( text_x, text_y ), mobile_ui::col_fg,
                             e.name, text_w );
            mobile_ui::text( draw, face::body, 12.5f, ImVec2( text_x, text_y + name_h + 2.0f * u ),
                             mobile_ui::col_muted, status, text_w );
        }
        y += head_h + 10.0f * u;
    }

    if( !d.description.empty() ) {
        if( draw != nullptr ) {
            mobile_ui::text( draw, face::body, 13.5f, ImVec2( base.x + 16.0f * u, y ), col_desc,
                             d.description, inner_w );
        }
        y += mobile_ui::text_size( face::body, 13.5f, d.description, inner_w ).y + 12.0f * u;
    }

    // fact tiles, three to a row
    {
        const float gap = 6.0f * u;
        const float cell_w = ( inner_w - gap * 2.0f ) / 3.0f;
        const float value_w = cell_w - 18.0f * u;
        const float label_h = mobile_ui::text_size( face::mono, 9.5f, "X" ).y;
        for( size_t first = 0; first < d.facts.size(); first += 3 ) {
            const size_t last = std::min( first + 3, d.facts.size() );
            float row_h = 0.0f;
            for( size_t i = first; i < last; ++i ) {
                row_h = std::max( row_h, mobile_ui::text_size( face::bold, 13.5f, d.facts[i].value,
                                  value_w ).y );
            }
            row_h += 14.0f * u + label_h + 3.0f * u;
            for( size_t i = first; draw != nullptr && i < last; ++i ) {
                const ImVec2 cell( base.x + 16.0f * u + static_cast<float>( i - first ) * ( cell_w + gap ), y );
                mobile_ui::box( draw, cell, ImVec2( cell.x + cell_w, cell.y + row_h ),
                                mobile_ui::col_fill_soft, 0, 9.0f );
                mobile_ui::text( draw, face::mono, 9.5f, ImVec2( cell.x + 9.0f * u, cell.y + 7.0f * u ),
                                 mobile_ui::col_muted, to_upper_case( d.facts[i].label ) );
                mobile_ui::text( draw, face::bold, 13.5f, ImVec2( cell.x + 9.0f * u,
                                 cell.y + 7.0f * u + label_h + 3.0f * u ), mobile_ui::col_fg, d.facts[i].value, value_w );
            }
            y += row_h + gap;
        }
        y += 12.0f * u - gap;
    }

    // actions; the first is the usual one
    {
        y += 4.0f * u;
        const float gap = 8.0f * u;
        const int columns = std::max( 1, static_cast<int>( ( inner_w + gap ) / ( 104.0f * u + gap ) ) );
        const float cell_w = ( inner_w - gap * static_cast<float>( columns - 1 ) ) /
                             static_cast<float>( columns );
        const int count = static_cast<int>( d.actions.size() );
        for( int i = 0; draw != nullptr && i < count; ++i ) {
            const ImVec2 cell( base.x + 16.0f * u + static_cast<float>( i % columns ) * ( cell_w + gap ),
                               y + static_cast<float>( i / columns ) * ( 48.0f * u + gap ) );
            if( button( draw, "##act" + std::to_string( i ), cell, ImVec2( cell_w, 48.0f * u ),
                        d.actions[i].label, 14.0f, i == 0, true ) ) {
                pending.kind = request_kind::action;
                pending.what = d.actions[i].kind;
                pending.locs = e.locs;
            }
        }
        const int rows = ( count + columns - 1 ) / columns;
        y += static_cast<float>( rows ) * ( 48.0f * u + gap ) - gap + 14.0f * u;
    }
    return y - base.y;
}

void inventory_window::draw_detail_pane( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->AddLine( pos, ImVec2( pos.x, pos.y + size.y ), mobile_ui::col_line, std::max( 1.0f, u ) );
    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + 8.0f * u ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##inventory_detail", ImVec2( size.x, size.y - 8.0f * u ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float height = detail_content( ImGui::GetWindowDrawList(), base, size.x );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void inventory_window::draw_detail_sheet( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    // A layer of its own over the whole screen, so that it covers the list and
    // takes its taps.
    ImGui::SetCursorScreenPos( origin );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##inventory_sheet_layer", size, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    draw->AddRectFilled( origin, screen_max, mobile_ui::col_dim );
    const bool outside = mobile_ui::pressable( "##sheet_outside", origin, size );

    const float grab_h = 22.0f * u;
    const float content_h = detail_content( nullptr, origin, size.x );
    const float max_h = ( size.y - safe.y ) * 0.84f;
    const float sheet_h = std::min( max_h, grab_h + content_h + safe.w + 6.0f * u );
    const ImVec2 pos( origin.x, screen_max.y - sheet_h );
    draw->AddRectFilled( pos, screen_max, mobile_ui::col_sheet, 22.0f * u,
                         ImDrawFlags_RoundCornersTop );
    draw->AddRect( pos, ImVec2( screen_max.x, screen_max.y + 30.0f * u ), mobile_ui::col_line,
                   22.0f * u, ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    draw->AddRectFilled( ImVec2( pos.x + size.x * 0.5f - 21.0f * u, pos.y + 9.0f * u ),
                         ImVec2( pos.x + size.x * 0.5f + 21.0f * u, pos.y + 14.0f * u ),
                         IM_COL32( 255, 255, 255, 71 ), 3.0f * u );

    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + grab_h ) );
    ImGui::BeginChild( "##inventory_sheet_scroll", ImVec2( size.x,
                       sheet_h - grab_h - safe.w - 6.0f * u ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float height = detail_content( ImGui::GetWindowDrawList(), base, size.x );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height ) );
    ImGui::EndChild();

    if( outside && ImGui::GetIO().MousePos.y < pos.y ) {
        sheet_open = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void inventory_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();

    landscape = size.x > size.y;
    const ImVec2 inner_pos( origin.x + safe.x, origin.y + safe.y );
    const ImVec2 inner_size( size.x - safe.x - safe.z, size.y - safe.y );
    if( landscape ) {
        const float detail_w = std::floor( inner_size.x * 0.44f );
        draw_left( inner_pos, ImVec2( inner_size.x - detail_w, inner_size.y ), true );
        draw_detail_pane( ImVec2( inner_pos.x + inner_size.x - detail_w, inner_pos.y ),
                          ImVec2( detail_w, inner_size.y ) );
    } else {
        const bool sheet = sheet_open && selected >= 0;
        draw_left( inner_pos, inner_size, !sheet );
        if( sheet ) {
            draw_detail_sheet( origin, size );
        }
    }

    // what the game said about the last action
    if( !state.toast.empty() ) {
        ImDrawList *const top = ImGui::GetForegroundDrawList();
        const float wrap = std::min( size.x - 48.0f * u, 420.0f * u );
        const ImVec2 text_size = mobile_ui::text_size( face::body, 13.0f, state.toast, wrap );
        const ImVec2 box_size( text_size.x + 28.0f * u, text_size.y + 20.0f * u );
        const ImVec2 box_min( origin.x + ( size.x - box_size.x ) * 0.5f,
                              origin.y + size.y - safe.w - ( state.current == tab::nearby ? 84.0f : 16.0f ) * u -
                              box_size.y );
        const ImVec2 box_max( box_min.x + box_size.x, box_min.y + box_size.y );
        top->AddRectFilled( box_min, box_max, IM_COL32( 30, 36, 28, 245 ), 12.0f * u );
        top->AddRect( box_min, box_max, mobile_ui::col_line, 12.0f * u, 0, std::max( 1.0f, u ) );
        mobile_ui::text( top, face::body, 13.0f, ImVec2( box_min.x + 14.0f * u, box_min.y + 10.0f * u ),
                         mobile_ui::col_fg, state.toast, wrap );
    }
}

} // namespace

static std::string last_message()
{
    const std::vector<std::pair<std::string, std::string>> recent = Messages::recent_messages( 1 );
    return recent.empty() ? std::string() : remove_color_tags( recent.back().second );
}

// Shows the screen until something is asked of it.
static request ask()
{
    inventory_window window;
    input_context ctxt( "default" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    while( window.pending.kind == request_kind::none ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            if( imclient->want_text_input() ) {
                // back first leaves the search field
                imclient->clear_text_focus();
            } else if( !window.back() ) {
                window.pending.kind = request_kind::close;
            }
        } else if( action == "LEFT" || action == "RIGHT" ) {
            if( !imclient->want_text_input() ) {
                window.scroll_chips( action == "LEFT" ? 120.0f : -120.0f );
            }
        }
        if( window.pending.kind != request_kind::none ) {
            state.toast.clear();
        }
    }
    return window.pending;
}

void show( const tab start )
{
    state.current = start;
    state.targets.clear();
    avatar &you = get_avatar();
    if( state.message_before ) {
        // back from an action that took game time: show what the game said
        const std::string now = last_message();
        state.toast = now != *state.message_before ? now : std::string();
        state.message_before.reset();
    }
    while( true ) {
        request req = ask();
        // The screen is gone now, so that upstream's prompts and menus can show.
        if( req.kind == request_kind::close ) {
            state.toast.clear();
            return;
        }
        if( req.kind == request_kind::pick_up ) {
            req.what = act::pick_up;
        }
        const std::string before = last_message();
        perform( req );
        const std::string after = last_message();
        state.toast = after != before ? after : std::string();
        if( you.activity || you.get_moves() <= 0 || you.is_dead_state() ) {
            // Game time has to pass.  Come back when the avatar can act again,
            // unless the action was one that leaves for the game anyway.
            const bool returns = req.what != act::read && req.what != act::more &&
                                 req.what != act::open;
            if( returns && !you.is_dead_state() ) {
                const tab reopen = state.current;
                state.message_before = before;
                uistate.open_menu = [reopen]() {
                    show( reopen );
                };
            } else {
                state.toast.clear();
            }
            return;
        }
    }
}

drop_locations pick_up_from( const std::set<tripoint_bub_ms> &targets )
{
    drop_locations picked;
    state.current = tab::nearby;
    state.targets = targets;
    state.title = targets.size() == 1 ? place_name( *targets.begin() ) : _( "Pick up" );
    state.filter.clear();
    state.search.clear();
    state.toast.clear();
    state.message_before.reset();
    if( targets.empty() ) {
        state.title = _( "Nearby" );
        const tripoint_bub_ms center = get_avatar().pos_bub();
        for( const tripoint_bub_ms &pos : closest_points_first( center, 1 ) ) {
            if( pos == center || get_map().clear_path( center, pos, rl_dist( center, pos ), 1, 100 ) ) {
                state.targets.insert( pos );
            }
        }
    }
    const request req = ask();
    state.targets.clear();
    if( req.kind == request_kind::pick_up ) {
        // the caller picks them up, as it does with upstream's menu
        for( const item_location &loc : req.locs ) {
            picked.emplace_back( loc, loc->count() );
        }
    } else if( req.kind == request_kind::action ) {
        if( req.what == act::pick_up ) {
            for( const item_location &loc : req.locs ) {
                picked.emplace_back( loc, loc->count() );
            }
        } else {
            perform( req );
        }
    }
    return picked;
}

} // namespace mobile_inventory

#else // !TILES

namespace mobile_inventory
{

void show( tab )
{
}

drop_locations pick_up_from( const std::set<tripoint_bub_ms> & )
{
    return drop_locations();
}

} // namespace mobile_inventory

#endif // TILES
