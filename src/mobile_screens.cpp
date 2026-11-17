#include "mobile_screens.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "character.h"
#include "item.h"
#include "item_location.h"
#include "magic.h"
#include "messages.h"
#include "mission.h"
#include "mobile_list.h"
#include "mutation.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"
#include "units.h"
#include "units_utility.h"

namespace mobile_screens
{

void bionics( avatar &you )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = _( "Bionics" );
        what.header = string_format( _( "Power: %1$s / %2$s" ), units::display( you.get_power_level() ),
                                     units::display( you.get_max_power_level() ) );
        what.tabs = { std::string( _( "Active" ) ), std::string( _( "Passive" ) ) };
        what.empty_text = _( "None installed." );
        std::vector<bionic *> shown;
        for( bionic &bio : *you.my_bionics ) {
            const bionic_data &data = bio.info();
            mobile_list::row r;
            r.name = data.name.translated();
            r.tab = data.activated ? 0 : 1;
            r.description = data.description.translated();
            if( data.activated ) {
                r.right = bio.powered ? _( "On" ) : _( "Off" );
                r.current = bio.powered;
                r.action = bio.powered ? _( "Turn off" ) : _( "Turn on" );
                std::string cost;
                if( data.power_activate > 0_kJ ) {
                    cost = string_format( _( "%s to turn on" ), units::display( data.power_activate ) );
                }
                if( data.charge_time > 0_turns && data.power_over_time > 0_kJ ) {
                    cost += ( cost.empty() ? "" : " · " ) + ( data.charge_time == 1_turns ?
                            string_format( _( "%s/turn" ), units::display( data.power_over_time ) ) :
                            string_format( _( "%s/%d turns" ), units::display( data.power_over_time ),
                                           to_turns<int>( data.charge_time ) ) );
                }
                r.detail = cost.empty() ? _( "No power cost" ) : cost;
            }
            // a bionic that holds a weapon of your choosing, as upstream's "Select action" menu
            if( data.activated && !bio.powered && bio.can_install_weapon() ) {
                if( bio.has_weapon() ) {
                    r.description += "\n\n" + string_format( _( "Installed weapon: %s" ),
                                     bio.get_weapon().tname() );
                    r.more_actions.emplace_back( _( "Uninstall weapon" ) );
                } else {
                    r.description += "\n\n" + std::string( _( "No weapon is installed in it." ) );
                    r.action = _( "Install weapon" );
                }
            }
            what.rows.push_back( r );
            shown.push_back( &bio );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= shown.size() ) {
            return;
        }
        bionic &bio = *shown[static_cast<size_t>( did.row )];
        const bionic_data &data = bio.info();
        if( bio.powered ) {
            you.deactivate_bionic( bio );
            if( data.deactivated_close_ui ) {
                return;
            }
        } else if( bio.can_install_weapon() && bio.has_weapon() && did.action == 1 ) {
            if( std::optional<item> weapon = bio.uninstall_weapon() ) {
                you.i_add_or_drop( *weapon );
            }
        } else if( bio.can_install_weapon() && !bio.has_weapon() ) {
            // upstream's choice, from what you carry
            std::vector<item *> valid_weapons = you.items_with( [&bio]( const item & it ) {
                return it.has_any_flag( bio.id->installable_weapon_flags );
            } );
            if( valid_weapons.empty() ) {
                popup( _( "You don't have any items you can install in this bionic" ) );
                continue;
            }
            uilist menu;
            menu.title = _( "Select weapon to install" );
            for( size_t i = 0; i < valid_weapons.size(); i++ ) {
                menu.addentry( static_cast<int>( i ), true, MENU_AUTOASSIGN, valid_weapons[i]->tname() );
            }
            menu.query();
            if( menu.ret >= 0 && static_cast<size_t>( menu.ret ) < valid_weapons.size() ) {
                item &new_weapon = *valid_weapons[static_cast<size_t>( menu.ret )];
                if( bio.can_install_weapon( new_weapon ) && bio.install_weapon( new_weapon ) ) {
                    item_location loc( you, &new_weapon );
                    loc.remove_item();
                } else {
                    popup( _( "Unable to install %s" ), new_weapon.tname() );
                }
            }
        } else {
            bool close_ui = false;
            you.activate_bionic( bio, false, &close_ui );
            if( data.activated_close_ui || close_ui ) {
                // it wants the map: a target, a direction
                return;
            }
        }
    }
}

void mutations( avatar &you )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = _( "Traits and mutations" );
        what.tabs = { std::string( _( "Usable" ) ), std::string( _( "Always on" ) ) };
        what.empty_text = _( "None." );
        std::vector<trait_id> shown;
        for( const trait_id &trait : you.get_mutations() ) {
            const mutation_branch &data = *trait;
            if( !data.player_display ) {
                continue;
            }
            mobile_list::row r;
            r.name = you.mutation_name( trait );
            r.tab = data.activated ? 0 : 1;
            r.description = you.mutation_desc( trait );
            if( data.activated ) {
                const bool on = you.has_active_mutation( trait );
                r.right = on ? _( "On" ) : _( "Off" );
                r.current = on;
                r.action = on ? _( "Stop using" ) : _( "Use" );
            }
            what.rows.push_back( r );
            shown.push_back( trait );
        }
        const int chosen = mobile_list::show( what, tab, selected );
        if( chosen < 0 || static_cast<size_t>( chosen ) >= shown.size() ) {
            return;
        }
        const trait_id &trait = shown[static_cast<size_t>( chosen )];
        const mutation_branch &data = *trait;
        if( you.has_active_mutation( trait ) ) {
            you.add_msg_if_player( m_neutral, _( "You stop using your %s." ), you.mutation_name( trait ) );
            you.deactivate_mutation( trait );
            return;
        }
        // the same checks as upstream's screen
        if( ( !data.hunger || you.get_kcal_percent() >= 0.8f ) &&
            ( !data.thirst || you.get_thirst() <= 400 ) &&
            ( !data.sleepiness || you.get_sleepiness() <= 400 ) &&
            ( !data.mana || you.magic->available_mana() >= data.cost ) &&
            ( !data.stamina || you.get_stamina() >= data.cost ) ) {
            you.add_msg_if_player( m_neutral, string_format( data.activation_msg.translated(),
                                   you.mutation_name( trait ) ) );
            you.activate_mutation( trait );
            return;
        }
        popup( _( "You don't have enough in you to activate your %s!" ), you.mutation_name( trait ) );
    }
}

void missions( avatar &you )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = _( "Missions" );
        what.tabs = { _( "Active" ), _( "Completed" ), _( "Failed" ) };
        what.empty_text = _( "No missions here." );
        std::vector<mission *> shown;
        const mission *const following = you.get_active_mission();
        const auto add = [&]( const std::vector<mission *> &list, const int which ) {
            for( mission *const m : list ) {
                if( m == nullptr ) {
                    continue;
                }
                mobile_list::row r;
                r.name = m->name();
                r.tab = which;
                r.description = m->get_description();
                if( which == 0 ) {
                    r.current = m == following;
                    r.right = r.current ? _( "Following" ) : std::string();
                    r.action = r.current ? std::string() : _( "Follow this mission" );
                    if( m->has_deadline() ) {
                        r.detail = string_format( _( "Deadline: %s" ), to_string( m->get_deadline() ) );
                    }
                }
                what.rows.push_back( r );
                shown.push_back( m );
            }
        };
        add( you.get_active_missions(), 0 );
        add( you.get_completed_missions(), 1 );
        add( you.get_failed_missions(), 2 );
        const int chosen = mobile_list::show( what, tab, selected );
        if( chosen < 0 || static_cast<size_t>( chosen ) >= shown.size() ) {
            return;
        }
        you.set_active_mission( *shown[static_cast<size_t>( chosen )] );
    }
}

} // namespace mobile_screens
