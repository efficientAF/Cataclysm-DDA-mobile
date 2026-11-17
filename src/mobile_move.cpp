#include "mobile_move.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "coordinates.h"
#include "inventory_ui.h"
#include "item_location.h"
#include "line.h"
#include "map.h"
#include "mobile_menu.h"
#include "output.h"
#include "player_activity.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"
#include "vehicle.h"
#include "vpart_position.h"

namespace mobile_move
{

bool wanted()
{
    return mobile_menu::enabled();
}

namespace
{
struct place {
    tripoint_bub_ms pos;
    std::string name;
    // items lying there and in a vehicle's cargo space there
    int items = 0;
    bool cargo = false;
};

// The nine tiles around the avatar that can hold items, nearest first.
std::vector<place> places_around()
{
    map &here = get_map();
    const avatar &you = get_avatar();
    const tripoint_bub_ms center = you.pos_bub();
    std::vector<place> places;
    for( const tripoint_bub_ms &pos : closest_points_first( center, 1 ) ) {
        if( pos != center && !here.clear_path( center, pos, 1, 1, 100 ) ) {
            continue;
        }
        place p;
        p.pos = pos;
        const std::optional<vpart_reference> cargo = here.veh_at( pos ).cargo();
        p.cargo = cargo.has_value();
        if( !here.can_put_items_ter_furn( pos ) && !p.cargo ) {
            continue;
        }
        p.items = here.accessible_items( pos ) ? static_cast<int>( here.i_at( pos ).size() ) : 0;
        if( cargo ) {
            p.items += static_cast<int>( cargo->items().size() );
        }
        const std::string what = cargo ? remove_color_tags( cargo->part().name() ) : here.name( pos );
        p.name = pos == center ? string_format( _( "Here: %s" ), what ) :
                 string_format( "%s: %s", direction_name( direction_from( center.raw(), pos.raw() ) ), what );
        places.push_back( p );
    }
    return places;
}
} // namespace

void show()
{
    avatar &you = get_avatar();
    const std::vector<place> places = places_around();

    // where from
    uilist from;
    from.text = _( "Move items from where?" );
    from.desc_enabled = true;
    for( size_t i = 0; i < places.size(); ++i ) {
        if( places[i].items > 0 ) {
            from.addentry_desc( static_cast<int>( i ), true, MENU_AUTOASSIGN, places[i].name,
                                string_format( n_gettext( "%d item", "%d items", places[i].items ), places[i].items ) );
        }
    }
    if( from.entries.empty() ) {
        popup( _( "There is nothing around you to move." ) );
        return;
    }
    from.query();
    if( from.ret < 0 || static_cast<size_t>( from.ret ) >= places.size() ) {
        return;
    }
    const place &source = places[static_cast<size_t>( from.ret )];

    // what
    inventory_multiselector selector( you );
    selector.set_title( string_format( _( "Move from %s" ), source.name ) );
    selector.add_map_items( source.pos );
    selector.add_vehicle_items( source.pos );
    if( selector.empty() ) {
        popup( _( "Nothing there can be moved." ) );
        return;
    }
    const drop_locations chosen = selector.execute();
    if( chosen.empty() ) {
        return;
    }

    // where to
    uilist to;
    to.text = string_format( n_gettext( "Move %d item to where?", "Move %d items to where?",
                                        chosen.size() ), static_cast<int>( chosen.size() ) );
    to.addentry( -2, true, MENU_AUTOASSIGN, _( "What you carry (pick up)" ) );
    for( size_t i = 0; i < places.size(); ++i ) {
        if( places[i].pos != source.pos ) {
            to.addentry( static_cast<int>( i ), true, MENU_AUTOASSIGN, places[i].name );
        }
    }
    to.query();
    if( to.ret == -2 ) {
        you.pick_up( chosen );
        return;
    }
    if( to.ret < 0 || static_cast<size_t>( to.ret ) >= places.size() ) {
        return;
    }
    const place &target = places[static_cast<size_t>( to.ret )];
    std::vector<item_location> items;
    std::vector<int> quantities;
    for( const drop_location &one : chosen ) {
        items.push_back( one.first );
        quantities.push_back( one.second );
    }
    you.assign_activity( move_items_activity_actor( items, quantities, target.cargo,
                         target.pos - you.pos_bub() ) );
}

} // namespace mobile_move
