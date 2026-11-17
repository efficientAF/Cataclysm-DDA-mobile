// The list of items, creatures and terrain nearby for phones.  A member of
// surroundings_menu so that it reads upstream's own lists, filters and sorting;
// what the player taps goes back to surroundings_menu::execute as one of its
// action names, and upstream's code there does the work.
#include "surroundings_menu.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "item.h"
#include "map.h"
#include "mapdata.h"
#include "mobile_list.h"
#include "monster.h"
#include "npc.h"
#include "safemode_ui.h"
#include "string_formatter.h"
#include "translations.h"

static const trait_id trait_INATTENTIVE( "INATTENTIVE" );

static std::string join_description( const std::vector<std::string> &parts )
{
    std::string text;
    for( const std::string &part : parts ) {
        if( part == "--" ) {
            text += "\n";
        } else {
            text += part + "\n";
        }
    }
    return text;
}

std::string surroundings_menu::mobile_action()
{
    hide_ui = true;
    if( mobile_tab < 0 ) {
        mobile_tab = switch_tab == surroundings_menu_tab_enum::num_tabs ? static_cast<int>
                     ( selected_tab ) : static_cast<int>( switch_tab );
    }

    mobile_list::spec what;
    what.title = _( "Nearby" );
    what.tabs = { item_data.title, monster_data.title, terfurn_data.title };
    // what each button of a row stands for
    std::vector<std::vector<std::string>> row_actions;
    // its place in its tab's filtered list
    std::vector<size_t> row_source;
    const std::string several = _( "Next place" );

    for( size_t i = 0; i < item_data.filtered_list.size(); ++i ) {
        map_entity_stack<item> *const it = item_data.filtered_list[i];
        const item *const thing = it->get_selected_entity();
        if( thing == nullptr ) {
            continue;
        }
        mobile_list::row r;
        r.tab = 0;
        r.name = it->get_selected_name();
        r.right = it->get_selected_distance_string();
        r.detail = it->get_category();
        std::string &description = mobile_descriptions[thing];
        if( description.empty() ) {
            description = thing->info( true );
        }
        r.description = description;
        r.action = _( "Details" );
        r.more_actions = { _( "Travel to" ), _( "Compare" ) };
        std::vector<std::string> actions = { "EXAMINE", "TRAVEL_TO", "COMPARE" };
        if( it->entities.size() > 1 ) {
            r.more_actions.push_back( several );
            actions.emplace_back( "RIGHT" );
        }
        what.rows.push_back( r );
        row_actions.push_back( actions );
        row_source.push_back( i );
    }
    const int first_monster = static_cast<int>( what.rows.size() );
    const bool inattentive = you.has_trait( trait_INATTENTIVE );
    for( size_t i = 0; i < monster_data.filtered_list.size(); ++i ) {
        map_entity_stack<Creature> *const it = monster_data.filtered_list[i];
        const Creature *const mon = it->get_selected_entity();
        if( mon == nullptr ) {
            continue;
        }
        mobile_list::row r;
        r.tab = 1;
        r.name = it->get_selected_name();
        r.right = it->get_selected_distance_string();
        if( inattentive ) {
            r.detail = _( "Unknown" );
        } else if( mon->is_monster() ) {
            r.detail = mon->as_monster()->get_attitude().first;
        } else if( mon->is_npc() ) {
            r.detail = mon->as_npc()->get_current_status();
        }
        if( !inattentive && mon->sees( get_map(), you ) ) {
            r.detail += ( r.detail.empty() ? "" : " · " ) + std::string( _( "sees you" ) );
        }
        std::string &description = mobile_descriptions[mon];
        if( description.empty() ) {
            description = join_description( mon->extended_description() );
        }
        r.description = description;
        r.action = _( "Look at" );
        std::vector<std::string> actions = { "look" };
        // the same conditions as upstream's buttons
        if( rl_dist( you.pos_bub(), mon->pos_bub() ) <= max_gun_range ) {
            r.more_actions.emplace_back( _( "Fire" ) );
            actions.emplace_back( "fire" );
        }
        r.more_actions.emplace_back( _( "Travel to" ) );
        actions.emplace_back( "TRAVEL_TO" );
        if( !get_safemode().empty() ) {
            const std::string mon_name = mon->is_monster() ? mon->as_monster()->name() : "human";
            if( get_safemode().has_rule( mon_name, Creature::Attitude::ANY ) ) {
                r.more_actions.emplace_back( _( "Safe mode: remove" ) );
                actions.emplace_back( "SAFEMODE_BLACKLIST_REMOVE" );
            } else {
                r.more_actions.emplace_back( _( "Safe mode: add" ) );
                actions.emplace_back( "SAFEMODE_BLACKLIST_ADD" );
            }
        }
        what.rows.push_back( r );
        row_actions.push_back( actions );
        row_source.push_back( i );
    }
    const int first_terfurn = static_cast<int>( what.rows.size() );
    for( size_t i = 0; i < terfurn_data.filtered_list.size(); ++i ) {
        map_entity_stack<map_data_common_t> *const it = terfurn_data.filtered_list[i];
        const map_data_common_t *const thing = it->get_selected_entity();
        if( thing == nullptr ) {
            continue;
        }
        mobile_list::row r;
        r.tab = 2;
        r.name = it->get_selected_name();
        r.right = it->get_selected_distance_string();
        std::string &description = mobile_descriptions[thing];
        if( description.empty() ) {
            description = join_description( thing->extended_description() );
        }
        r.description = description;
        r.action = _( "Travel to" );
        r.more_actions = { _( "Look at" ) };
        std::vector<std::string> actions = { "TRAVEL_TO", "look" };
        if( it->entities.size() > 1 ) {
            r.more_actions.push_back( several );
            actions.emplace_back( "RIGHT" );
        }
        what.rows.push_back( r );
        row_actions.push_back( actions );
        row_source.push_back( i );
    }

    const tab_data *const shown = get_tab_data( static_cast<surroundings_menu_tab_enum>
                                  ( std::clamp( mobile_tab, 0, 2 ) ) );
    what.empty_text = shown->has_filter() ? _( "Nothing matches the filter." ) :
                      _( "Nothing in sight." );
    what.buttons = { _( "Filter" ), _( "Sort" ) };
    if( item_data.has_filter() || monster_data.has_filter() || terfurn_data.has_filter() ) {
        what.buttons.emplace_back( _( "Clear filter" ) );
    }
    if( mobile_selected >= static_cast<int>( what.rows.size() ) ) {
        mobile_selected = -1;
    }

    const mobile_list::outcome did = mobile_list::show_all( what, mobile_tab, mobile_selected );

    // upstream's code acts on the tab and the entry that are selected
    selected_tab = static_cast<surroundings_menu_tab_enum>( std::clamp( mobile_tab, 0, 2 ) );
    switch_tab = selected_tab;
    if( did.went_back() ) {
        return "QUIT";
    }
    if( did.button >= 0 ) {
        // filtering and sorting change the rows
        mobile_selected = -1;
        if( did.button == 0 ) {
            return selected_tab == surroundings_menu_tab_enum::monsters ? "FILTER_MONSTERS" : "FILTER";
        }
        return did.button == 1 ? "SORT" : "RESET_FILTER";
    }
    if( did.row < 0 || did.row >= static_cast<int>( row_actions.size() ) ) {
        return "QUIT";
    }
    const size_t source = row_source[static_cast<size_t>( did.row )];
    if( did.row < first_monster ) {
        selected_tab = surroundings_menu_tab_enum::items;
        item_data.selected_entry = item_data.filtered_list[source];
    } else if( did.row < first_terfurn ) {
        selected_tab = surroundings_menu_tab_enum::monsters;
        monster_data.selected_entry = monster_data.filtered_list[source];
    } else {
        selected_tab = surroundings_menu_tab_enum::terfurn;
        terfurn_data.selected_entry = terfurn_data.filtered_list[source];
    }
    switch_tab = selected_tab;
    update_path_end();
    const std::vector<std::string> &actions = row_actions[static_cast<size_t>( did.row )];
    const std::string action = actions[static_cast<size_t>( std::clamp( did.action, 0,
                                                static_cast<int>( actions.size() ) - 1 ) )];
    if( action == "look" ) {
        // upstream's look-around starts where the view is
        if( const std::optional<tripoint_rel_ms> pos = get_selected_pos() ) {
            you.view_offset = *pos;
        }
    }
    return action;
}
