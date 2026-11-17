#include "mobile_camp.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "auto_pickup.h"
#include "basecamp.h"
#include "character.h"
#include "coordinates.h"
#include "faction_camp.h"
#include "game.h"
#include "mission_companion.h"
#include "mobile_list.h"
#include "npc.h"
#include "npctalk.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player_activity.h"
#include "string_formatter.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"

namespace mobile_camp
{

// --- Missions of a camp or a companion ---

void choose_mission( mission_data &mission_key, const tripoint_abs_omt &omt_pos,
                     const std::string &role_id, const std::string &title )
{
    const bool camp = role_id == base_camps::id;
    // upstream's tabs: the base and its eight expansions; tab n lists entries[n + 1],
    // the first one also the priority missions of entries[0]
    std::vector<std::vector<mission_entry>> lists;
    std::vector<std::string> tab_names;
    const auto main_list = [&]() {
        std::vector<mission_entry> list = mission_key.entries[0];
        for( const mission_entry &k : mission_key.entries[1] ) {
            bool has = false;
            for( const mission_entry &have : list ) {
                has = has || is_equal( k.id, have.id );
            }
            if( !has ) {
                list.push_back( k );
            }
        }
        return list;
    };
    lists.push_back( main_list() );
    if( camp ) {
        std::vector<std::string> titles( base_camps::all_directions.size() );
        for( const std::pair<const point_rel_omt, base_camps::direction_data> &direction :
             base_camps::all_directions ) {
            titles.at( direction.second.tab_order ) = direction.second.tab_title.translated();
        }
        tab_names.push_back( titles.front() );
        // like upstream, tabs with nothing on them are passed over
        for( size_t t = 1; t < titles.size() && t + 1 < mission_key.entries.size(); ++t ) {
            if( !mission_key.entries[t + 1].empty() ) {
                lists.push_back( mission_key.entries[t + 1] );
                tab_names.push_back( titles[t] );
            }
        }
    }

    mobile_list::spec what;
    what.title = talk_function::name_mission_tabs( omt_pos, role_id, title, base_camps::TAB_MAIN );
    if( tab_names.size() > 1 ) {
        what.tabs = tab_names;
    }
    what.empty_text = _( "There are no missions here." );
    std::vector<const mission_entry *> shown;
    for( size_t t = 0; t < lists.size(); ++t ) {
        for( const mission_entry &entry : lists[t] ) {
            mobile_list::row r;
            r.name = remove_color_tags( entry.name_display );
            r.tab = static_cast<int>( t );
            r.description = entry.text;
            r.enabled = entry.possible;
            if( entry.possible ) {
                r.action = entry.id.ret ? _( "Recall" ) : _( "Start" );
            } else {
                r.right = _( "Not now" );
            }
            for( const mission_entry &k : mission_key.entries[0] ) {
                r.current = r.current || is_equal( entry.id, k.id );
            }
            what.rows.push_back( r );
            shown.push_back( &entry );
        }
    }
    int tab = 0;
    int selected = -1;
    const int chosen = mobile_list::show( what, tab, selected );
    if( chosen >= 0 && static_cast<size_t>( chosen ) < shown.size() ) {
        mission_key.cur_key = *shown[static_cast<size_t>( chosen )];
        return;
    }
    // what upstream leaves behind on QUIT
    mission_entry dud;
    dud.id = { {No_Mission, "", {}, std::nullopt}, false};
    dud.name_display = "NONE";
    mission_key.cur_key = dud;
}

// --- Who works at a camp ---

void assign_workers( basecamp &camp )
{
    camp.validate_assignees();
    g->validate_npc_followers();
    int tab = 0;
    int selected = -1;
    while( true ) {
        std::vector<npc *> followers;
        overmap_buffer.populate_followers_vec( followers, true, true );
        mobile_list::spec what;
        //~ %s: the name of a camp
        what.title = string_format( _( "Assign to %s" ), camp.camp_name() );
        what.empty_text = _( "You have no companions following you." );
        for( npc *const guy : followers ) {
            mobile_list::row r;
            r.name = guy->disp_name();
            const bool here = guy->assigned_camp && *guy->assigned_camp == camp.camp_omt_pos();
            r.right = here ? _( "Assigned" ) : _( "Not assigned" );
            r.current = here;
            r.description = _( "A follower who is assigned to a camp stays there and does the jobs "
                               "you give them a priority for." );
            r.action = _( "Assign to this camp" );
            r.more_actions = { std::string( _( "Inspect" ) ) };
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= followers.size() ) {
            return;
        }
        npc &guy = *followers[static_cast<size_t>( did.row )];
        if( did.action == 1 ) {
            guy.disp_info();
        } else {
            talk_function::assign_camp( guy );
        }
    }
}

// The job priorities of one worker.
static void job_priorities( npc &guy )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        const std::vector<activity_id> jobs = guy.job.get_prioritised_vector();
        mobile_list::spec what;
        what.title = guy.disp_name();
        what.header = _( "Job priority: higher is done first, 0 is never done" );
        what.empty_text = _( "No current job." );
        what.buttons = { std::string( _( "Clear all priorities" ) ), std::string( _( "Set all priorities" ) ) };
        for( const activity_id &job : jobs ) {
            const int priority = guy.job.get_priority_of_job( job );
            const std::string verb = player_activity( job ).get_verb().translated();
            mobile_list::row r;
            r.name = uppercase_first_letter( verb );
            r.right = std::to_string( priority );
            r.current = priority > 0;
            r.description = priority > 0 ?
                            //~ %1$s: a job, e.g. "farming"; %2$d: its priority
                            string_format( _( "%1$s has priority %2$d." ), r.name, priority ) :
                            //~ %s: a job, e.g. "farming"
                            string_format( _( "%s is turned off." ), r.name );
            r.action = _( "Set priority" );
            if( priority > 0 ) {
                r.more_actions = { std::string( _( "Turn off" ) ) };
            }
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            return;
        }
        if( did.button == 0 ) {
            guy.job.clear_all_priorities();
        } else if( did.button == 1 ) {
            int priority = 0;
            if( query_int( priority, false, _( "Priority for all jobs " ) ) ) {
                guy.job.set_all_priorities( priority );
            }
        } else if( did.row >= 0 && static_cast<size_t>( did.row ) < jobs.size() ) {
            const activity_id &job = jobs[static_cast<size_t>( did.row )];
            if( did.action == 1 ) {
                guy.job.set_task_priority( job, 0 );
                continue;
            }
            int priority = 0;
            if( query_int( priority, false, _( "Priority for %s " ),
                           player_activity( job ).get_verb() ) ) {
                guy.job.set_task_priority( job, priority );
            }
        }
    }
}

void assign_jobs( basecamp &camp )
{
    camp.validate_assignees();
    int tab = 0;
    int selected = -1;
    while( true ) {
        std::vector<npc *> stationed;
        for( const npc_ptr &elem : camp.get_npcs_assigned() ) {
            if( elem ) {
                stationed.push_back( elem.get() );
            }
        }
        mobile_list::spec what;
        //~ %s: the name of a camp
        what.title = string_format( _( "Jobs at %s" ), camp.camp_name() );
        what.empty_text = _( "There are no NPCs stationed here" );
        for( npc *const guy : stationed ) {
            mobile_list::row r;
            r.name = guy->disp_name();
            if( guy->has_job() ) {
                int count = 0;
                for( const activity_id &job : guy->job.get_prioritised_vector() ) {
                    const int priority = guy->job.get_priority_of_job( job );
                    r.description += string_format( _( "%s : %s" ), player_activity( job ).get_verb(),
                                                    std::to_string( priority ) ) + "\n";
                    count += priority > 0 ? 1 : 0;
                }
                r.right = string_format( n_gettext( "%d job", "%d jobs", count ), count );
            } else {
                r.description = _( "No current job." );
                r.right = _( "No job" );
            }
            r.action = _( "Job priorities" );
            r.more_actions = { std::string( _( "Inspect" ) ) };
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= stationed.size() ) {
            return;
        }
        npc &guy = *stationed[static_cast<size_t>( did.row )];
        if( did.action == 1 ) {
            guy.disp_info();
        } else {
            job_priorities( guy );
        }
    }
}

// --- The rules a follower keeps ---

static std::string parsed( const std::string &snippet, const npc &guy )
{
    std::string text = snippet;
    parse_tags( text, get_player_character(), guy );
    return remove_color_tags( text );
}

// A rule with several values: a sheet of them, the one in force marked.
template<typename T>
static void choose_value( const std::string &title, T &rule, const std::map<T, std::string> &values,
                          const npc &guy )
{
    uilist menu;
    menu.title = title;
    std::vector<T> order;
    for( const std::pair<const T, std::string> &value : values ) {
        const int index = static_cast<int>( order.size() );
        menu.addentry( index, true, MENU_AUTOASSIGN, parsed( value.second, guy ) );
        if( value.first == rule ) {
            menu.selected = index;
        }
        order.push_back( value.first );
    }
    menu.query();
    if( menu.ret >= 0 && static_cast<size_t>( menu.ret ) < order.size() ) {
        rule = order[static_cast<size_t>( menu.ret )];
    }
}

template<typename T>
static mobile_list::row value_row( const std::string &name, const T &rule,
                                   const std::map<T, std::string> &values, const npc &guy )
{
    mobile_list::row r;
    r.name = name;
    const auto found = values.find( rule );
    if( found != values.end() ) {
        r.detail = parsed( found->second, guy );
    }
    r.description = _( "Choices:" );
    for( const std::pair<const T, std::string> &value : values ) {
        r.description += "\n· " + parsed( value.second, guy );
    }
    r.action = _( "Change" );
    return r;
}

// Upstream's import and export of rules between followers
// (follower_rules_ui_impl::rules_transfer_popup), asked in two steps.
static void transfer_rules( npc &guy, const bool exporting )
{
    std::vector<npc *> others;
    for( npc &other : g->all_npcs() ) {
        if( &other != &guy && other.is_player_ally() ) {
            others.push_back( &other );
        }
    }
    if( others.empty() ) {
        popup( _( "Functions unavailable.  %s is your only follower." ), guy.disp_name() );
        return;
    }
    uilist who;
    who.title = exporting ?
                string_format( _( "Exporting rules from %s" ), guy.disp_name() ) :
                string_format( _( "Importing rules to %s" ), guy.disp_name() );
    for( size_t i = 0; i < others.size(); ++i ) {
        who.addentry( static_cast<int>( i ), true, MENU_AUTOASSIGN, others[i]->disp_name() );
    }
    who.query();
    if( who.ret < 0 || static_cast<size_t>( who.ret ) >= others.size() ) {
        return;
    }
    npc &other = *others[static_cast<size_t>( who.ret )];
    uilist which;
    which.title = exporting ?
                  //~ %s: a follower's name
                  string_format( _( "Which rules should %s get?" ), other.disp_name() ) :
                  //~ %s: a follower's name
                  string_format( _( "Which rules to take from %s?" ), other.disp_name() );
    which.addentry( 0, true, MENU_AUTOASSIGN, _( "Behaviors" ) );
    which.addentry( 1, true, MENU_AUTOASSIGN, _( "Aiming" ) );
    which.addentry( 2, true, MENU_AUTOASSIGN, _( "Engagement" ) );
    which.addentry( 3, true, MENU_AUTOASSIGN, _( "CBM recharge" ) );
    which.addentry( 4, true, MENU_AUTOASSIGN, _( "CBM reserve" ) );
    which.addentry( 5, true, MENU_AUTOASSIGN, _( "Pickup list" ) );
    which.addentry( 6, true, MENU_AUTOASSIGN, _( "ALL" ) );
    which.query();
    if( which.ret < 0 ) {
        return;
    }
    const bool all = which.ret == 6;
    npc &from = exporting ? guy : other;
    npc &to = exporting ? other : guy;
    if( which.ret == 0 || all ) {
        to.rules.flags = from.rules.flags;
        to.rules.overrides = from.rules.overrides;
        to.rules.override_enable = from.rules.override_enable;
    }
    if( which.ret == 1 || all ) {
        to.rules.aim = from.rules.aim;
    }
    if( which.ret == 2 || all ) {
        to.rules.engagement = from.rules.engagement;
    }
    if( which.ret == 3 || all ) {
        to.rules.cbm_recharge = from.rules.cbm_recharge;
    }
    if( which.ret == 4 || all ) {
        to.rules.cbm_reserve = from.rules.cbm_reserve;
    }
    if( which.ret == 5 || all ) {
        to.rules.pickup_whitelist = from.rules.pickup_whitelist;
    }
}

void follower_rules( npc &guy, const std::map<combat_engagement, std::string> &engagement,
                     const std::map<aim_rule, std::string> &aim,
                     const std::map<cbm_recharge_rule, std::string> &recharge,
                     const std::map<cbm_reserve_rule, std::string> &reserve )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        //~ %s: a follower's name
        what.title = string_format( _( "Rules for your follower, %s" ), guy.disp_name() );
        what.tabs = { std::string( _( "Behaviors" ) ), std::string( _( "Combat" ) ) };
        what.buttons = { std::string( _( "Default ALL" ) ), std::string( _( "Import" ) ),
                         std::string( _( "Export" ) )
                       };
        std::vector<ally_rule> flags;
        for( const std::pair<const std::string, ally_rule_data> &rule : ally_rule_strs ) {
            const bool on = guy.rules.has_flag( rule.second.rule );
            mobile_list::row r;
            r.name = parsed( on ? rule.second.rule_true_text : rule.second.rule_false_text, guy );
            r.right = on ? _( "On" ) : _( "Off" );
            r.current = on;
            r.description = string_format( _( "On: %1$s\n\nOff: %2$s" ),
                                           parsed( rule.second.rule_true_text, guy ),
                                           parsed( rule.second.rule_false_text, guy ) );
            r.action = on ? _( "Turn off" ) : _( "Turn on" );
            r.more_actions = { std::string( _( "Default" ) ) };
            what.rows.push_back( r );
            flags.push_back( rule.second.rule );
        }
        const size_t first_value = what.rows.size();
        const auto add = [&what]( mobile_list::row r ) {
            r.tab = 1;
            what.rows.push_back( std::move( r ) );
        };
        add( value_row( _( "Engagement rules:" ), guy.rules.engagement, engagement, guy ) );
        add( value_row( _( "Aiming rules:" ), guy.rules.aim, aim, guy ) );
        // like upstream, only for followers with bionics
        if( !guy.get_bionics().empty() ) {
            add( value_row( _( "CBM recharging rules:" ), guy.rules.cbm_recharge, recharge, guy ) );
            add( value_row( _( "CBM reserve rules" ), guy.rules.cbm_reserve, reserve, guy ) );
        }

        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            return;
        }
        if( did.button == 0 ) {
            if( query_yn( _( "Set all of %s's rules back to the default?" ), guy.disp_name() ) ) {
                guy.rules = npc_follower_rules();
            }
        } else if( did.button == 1 || did.button == 2 ) {
            transfer_rules( guy, did.button == 2 );
        } else if( did.row >= 0 && static_cast<size_t>( did.row ) < first_value ) {
            const ally_rule rule = flags[static_cast<size_t>( did.row )];
            if( did.action == 1 ) {
                guy.rules.clear_flag( rule );
                guy.rules.clear_override( rule );
            } else {
                // what upstream's checkbox does
                const bool now_on = !guy.rules.has_flag( rule );
                guy.rules.toggle_flag( rule );
                guy.rules.toggle_specific_override_state( rule, now_on );
            }
        } else if( did.row >= 0 ) {
            switch( static_cast<size_t>( did.row ) - first_value ) {
                case 0:
                    choose_value( _( "Engagement rules:" ), guy.rules.engagement, engagement, guy );
                    break;
                case 1:
                    choose_value( _( "Aiming rules:" ), guy.rules.aim, aim, guy );
                    break;
                case 2:
                    choose_value( _( "CBM recharging rules:" ), guy.rules.cbm_recharge, recharge, guy );
                    break;
                case 3:
                    choose_value( _( "CBM reserve rules" ), guy.rules.cbm_reserve, reserve, guy );
                    break;
                default:
                    break;
            }
        }
    }
}

} // namespace mobile_camp
