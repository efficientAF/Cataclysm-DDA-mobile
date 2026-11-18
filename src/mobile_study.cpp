#include "mobile_study.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "character_id.h"
#include "game.h"
#include "localized_comparator.h"
#include "memory_fast.h"
#include "mobile_list.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "skill.h"
#include "string_formatter.h"
#include "translations.h"

namespace mobile_study
{

study_zone_ui_result query( std::map<std::string, std::set<skill_id>> &npc_skill_preferences )
{
    // The same skills and followers as upstream's study_zone_window.
    std::vector<skill_id> all_skills;
    for( const Skill &s : Skill::skills ) {
        all_skills.push_back( s.ident() );
    }
    std::sort( all_skills.begin(), all_skills.end(), []( const skill_id & a, const skill_id & b ) {
        return localized_compare( a->name(), b->name() );
    } );

    std::vector<std::string> npc_names;
    for( const character_id &id : g->get_follower_list() ) {
        const shared_ptr_fast<npc> npc_ptr = overmap_buffer.find_npc( id );
        if( npc_ptr && !npc_ptr->is_hallucination() && npc_ptr->is_player_ally() ) {
            npc_names.push_back( npc_ptr->name );
        }
    }
    // followers who have left are still in the preferences
    for( const std::pair<const std::string, std::set<skill_id>> &elem : npc_skill_preferences ) {
        if( std::find( npc_names.begin(), npc_names.end(), elem.first ) == npc_names.end() ) {
            npc_names.push_back( elem.first );
        }
    }
    std::sort( npc_names.begin(), npc_names.end(), localized_compare );

    bool changed = false;
    // a follower who is new to the zone studies everything
    for( const std::string &npc_name : npc_names ) {
        if( npc_skill_preferences.find( npc_name ) == npc_skill_preferences.end() ) {
            npc_skill_preferences[npc_name].insert( all_skills.begin(), all_skills.end() );
            changed = true;
        }
    }

    const auto studies = [&]( const std::string & npc_name, const skill_id & skill ) {
        const auto it = npc_skill_preferences.find( npc_name );
        return it != npc_skill_preferences.end() && it->second.count( skill ) > 0;
    };

    const int skill_count = static_cast<int>( all_skills.size() );
    const int npc_count = static_cast<int>( npc_names.size() );
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = _( "Study Zone Skill Preferences" );
        what.tabs = npc_names;
        what.empty_text =
            _( "You have no followers.  Followers study the skills chosen for them in this zone." );
        if( npc_count > 0 ) {
            what.header = _( "The skills each follower studies in this zone." );
            what.buttons = { std::string( _( "Check All" ) ), std::string( _( "Clear All" ) ),
                             std::string( _( "Done" ) )
                           };
        } else {
            what.buttons = { std::string( _( "Done" ) ) };
        }
        for( int n = 0; n < npc_count; ++n ) {
            for( const skill_id &skill : all_skills ) {
                const bool on = studies( npc_names[n], skill );
                mobile_list::row r;
                r.tab = n;
                r.name = skill->name();
                r.right = on ? _( "Studies" ) : _( "No" );
                r.current = on;
                r.description = skill->description();
                r.action = on ? _( "Don't study" ) : _( "Study" );
                what.rows.push_back( r );
            }
        }
        const mobile_list::outcome done = mobile_list::show_all( what, tab, selected );
        if( done.went_back() ) {
            return study_zone_ui_result::canceled;
        }
        if( done.button == static_cast<int>( what.buttons.size() ) - 1 ) {
            return changed ? study_zone_ui_result::changed : study_zone_ui_result::successful;
        }
        if( npc_count == 0 ) {
            continue;
        }
        if( done.button == 0 || done.button == 1 ) {
            // for the follower whose tab is open
            const std::string &npc_name = npc_names[std::clamp( tab, 0, npc_count - 1 )];
            if( done.button == 0 ) {
                npc_skill_preferences[npc_name].insert( all_skills.begin(), all_skills.end() );
            } else {
                // as upstream: a follower with nothing ticked is forgotten
                npc_skill_preferences.erase( npc_name );
            }
            changed = true;
        } else if( done.row >= 0 && done.row < npc_count * skill_count ) {
            const std::string &npc_name = npc_names[done.row / skill_count];
            const skill_id &skill = all_skills[done.row % skill_count];
            if( studies( npc_name, skill ) ) {
                std::set<skill_id> &npc_skills = npc_skill_preferences[npc_name];
                npc_skills.erase( skill );
                if( npc_skills.empty() ) {
                    npc_skill_preferences.erase( npc_name );
                }
            } else {
                npc_skill_preferences[npc_name].insert( skill );
            }
            changed = true;
        }
    }
}

} // namespace mobile_study
