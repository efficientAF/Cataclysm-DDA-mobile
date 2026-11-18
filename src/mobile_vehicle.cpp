// The vehicle screen for phones.  These are members of veh_interact so that
// they can use upstream's own checks, requirement texts and selections; what
// the player picks ends up in sel_cmd and friends, and upstream's
// assign_activity does the work.
#include "veh_interact.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "messages.h"
#include "mobile_list.h"
#include "npc.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

static const faction_id faction_no_faction( "no_faction" );

static const proficiency_id proficiency_prof_aircraft_mechanic( "prof_aircraft_mechanic" );

// Shows what upstream would have printed in its message window.
static void tell( std::optional<std::string> &msg )
{
    if( msg.has_value() && !msg->empty() ) {
        popup( *msg );
    }
    msg.reset();
}

void veh_interact::mobile_overview( map &here, const overview_enable_t &enable,
                                    const overview_action_t &action )
{
    restore_on_out_of_scope prev_overview_enable( overview_enable );
    restore_on_out_of_scope prev_overview_action( overview_action );
    overview_enable = enable;
    // calc_overview marks nothing as selectable without an action
    overview_action = action;
    calc_overview( here );

    uilist menu;
    menu.text = title.value_or( veh->name );
    std::vector<vehicle_part *> parts;
    for( const part_option &opt : overview_opts ) {
        if( !opt.selectable || opt.part == nullptr ) {
            continue;
        }
        std::string desc;
        if( opt.part->is_seat() && opt.part->crew() != nullptr ) {
            // who has the seat, as upstream's overview says beside it
            desc = string_format( _( "Seat of %s" ), opt.part->crew()->get_name() );
        } else if( opt.part->is_broken() ) {
            desc = _( "Broken" );
        } else if( opt.part->ammo_remaining() > 0 && !opt.part->ammo_current().is_null() ) {
            desc = string_format( "%s × %d", item::nname( opt.part->ammo_current() ),
                                  opt.part->ammo_remaining() );
        } else if( opt.part->is_tank() || opt.part->is_fuel_store() ) {
            desc = _( "Empty" );
        }
        menu.addentry_desc( static_cast<int>( parts.size() ), true, MENU_AUTOASSIGN,
                            opt.part->name( false ),
                            desc );
        parts.push_back( opt.part );
    }
    if( parts.empty() ) {
        popup( _( "No part of the %s can be chosen for that." ), veh->name );
        return;
    }
    menu.query();
    if( menu.ret < 0 || static_cast<size_t>( menu.ret ) >= parts.size() ) {
        return;
    }
    vehicle_part &chosen = *parts[menu.ret];
    cursor_vp_mount = -chosen.mount;
    move_cursor( here, point_rel_ms::zero );
    if( action ) {
        action( here, chosen );
    }
}

void veh_interact::mobile_pick_tile( map &here )
{
    popup( _( "Tap a tile of the vehicle, or a spot next to it, then tap it again to choose it." ) );
    const std::optional<tripoint_bub_ms> chosen = g->look_around();
    if( !chosen ) {
        return;
    }
    const tripoint_bub_ms origin = veh->pos_bub( here );
    if( const optional_vpart_position ovp = here.veh_at( *chosen ) ) {
        if( &ovp->vehicle() == veh ) {
            cursor_vp_mount = -ovp->mount_pos();
            move_cursor( here, point_rel_ms::zero );
            return;
        }
    }
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        for( const point &d : {
                 point::north, point::south, point::east, point::west
             } ) {
            const point_rel_ms mount = vpr.mount_pos() + d;
            const tripoint_bub_ms where = origin + veh->coord_translate( mount );
            if( where.xy() == chosen->xy() ) {
                cursor_vp_mount = -mount;
                move_cursor( here, point_rel_ms::zero );
                return;
            }
        }
    }
    popup( _( "That spot is not part of the %s, or next to it." ), veh->name );
}

void veh_interact::mobile_install( map &here )
{
    const task_reason reason = cant_do( here, VEHICLE_INSTALL );
    if( reason == task_reason::INVALID_TARGET ) {
        popup( _( "Cannot install any part here." ) );
        return;
    }
    avatar &player_character = get_avatar();

    mobile_list::spec what;
    what.title = _( "Install a part" );
    what.header = veh->name;
    what.empty_text = _( "Nothing of this kind fits here." );
    std::vector<std::string> categories;
    for( const vpart_category &cat : vpart_category::all() ) {
        if( cat.get_id() == "_all" || cat.get_id() == "_filter" ) {
            continue;
        }
        categories.push_back( cat.get_id() );
        what.tabs.push_back( cat.name() );
    }
    std::vector<const vpart_info *> shown;
    // those that can be installed right now come first on each tab
    for( const bool wanted_ok : {
             true, false
         } ) {
        for( const vpart_info *vp : can_mount ) {
            sel_vpart_info = vp;
            const bool ok = update_part_requirements( here );
            if( ok != wanted_ok ) {
                continue;
            }
            mobile_list::row r;
            r.name = vp->name();
            r.enabled = ok;
            r.detail = ok ? std::string() : _( "Something is missing" );
            r.description = msg.value_or( std::string() );
            r.action = ok ? _( "Install" ) : std::string();
            for( size_t i = 0; i < categories.size(); i++ ) {
                if( vp->has_category( categories[i] ) ) {
                    r.tab = static_cast<int>( i );
                    what.rows.push_back( r );
                    shown.push_back( vp );
                }
            }
        }
    }
    msg.reset();
    sel_vpart_info = nullptr;
    sel_vehicle_part = nullptr;

    int tab = 0;
    int selected = -1;
    const int picked = mobile_list::show( what, tab, selected );
    if( picked < 0 || static_cast<size_t>( picked ) >= shown.size() ) {
        return;
    }
    switch( reason ) {
        case task_reason::LOW_MORALE:
            popup( _( "Your morale is too low to construct…" ) );
            return;
        case task_reason::LOW_LIGHT:
            popup( _( "It's too dark to see what you are doing…" ) );
            return;
        case task_reason::MOVING_VEHICLE:
            popup( _( "You can't install parts while driving." ) );
            return;
        default:
            break;
    }
    const vpart_info *vp = shown[picked];
    if( veh->would_install_prevent_flyable( *vp, player_character ) ) {
        if( !query_yn( _( "Installing this part will mean that this vehicle is no longer "
                          "flightworthy.  Continue?" ) ) ) {
            return;
        }
        veh->set_flyable( false );
    }
    if( veh->is_foldable() && !vp->folded_volume &&
        !query_yn( _( "Installing this part will make the vehicle unfoldable.  Continue?" ) ) ) {
        return;
    }
    sel_vpart_info = vp;
    sel_cmd = VEHICLE_INSTALL;
}

bool veh_interact::mobile_repair( map &here, int index )
{
    avatar &player_character = get_avatar();
    switch( cant_do( here, VEHICLE_REPAIR ) ) {
        case task_reason::LOW_MORALE:
            popup( _( "Your morale is too low to repair…" ) );
            return false;
        case task_reason::LOW_LIGHT:
            popup( _( "It's too dark to see what you are doing…" ) );
            return false;
        case task_reason::MOVING_VEHICLE:
            popup( _( "You can't repair stuff while driving." ) );
            return false;
        default:
            break;
    }
    vehicle_part &pt = veh->part( index );
    if( veh->would_repair_prevent_flyable( pt, player_character ) ) {
        if( !query_yn( _( "Repairing this part will mean that this vehicle is no longer "
                          "flightworthy.  Continue?" ) ) ) {
            return false;
        }
        veh->set_flyable( false );
    }
    sel_vehicle_part = &pt;
    sel_vpart_info = &pt.info();
    for( const Character *helper : player_character.get_crafting_helpers() ) {
        add_msg( m_info, _( "%s helps with this task…" ), helper->get_name() );
    }
    sel_cmd = VEHICLE_REPAIR;
    return true;
}

bool veh_interact::mobile_remove( map &here, int index )
{
    avatar &player_character = get_avatar();
    switch( cant_do( here, VEHICLE_REMOVE ) ) {
        case task_reason::LOW_MORALE:
            popup( _( "Your morale is too low to construct…" ) );
            return false;
        case task_reason::LOW_LIGHT:
            popup( _( "It's too dark to see what you are doing…" ) );
            return false;
        case task_reason::NOT_FREE:
            popup( _( "You cannot remove that part while something is attached to it." ) );
            return false;
        case task_reason::MOVING_VEHICLE:
            popup( _( "Better not remove something while driving." ) );
            return false;
        default:
            break;
    }
    // also points sel_vehicle_part and sel_vpart_info at the part
    if( !can_remove_part( here, index, player_character ) ) {
        tell( msg );
        return false;
    }
    msg.reset();
    if( veh->would_removal_prevent_flyable( veh->part( index ), player_character ) ) {
        if( !query_yn( _( "Removing this part will mean that this vehicle is no longer "
                          "flightworthy.  Continue?" ) ) ) {
            return false;
        }
        veh->set_flyable( false );
    }
    for( const Character *helper : player_character.get_crafting_helpers() ) {
        add_msg( m_info, _( "%s helps with this task…" ), helper->get_name() );
    }
    sel_cmd = VEHICLE_REMOVE;
    veh->recalculate_enchantment_cache();
    return true;
}

std::string veh_interact::mobile_repair_text( map &here, vehicle_part &pt, bool &ok )
{
    avatar &player_character = get_avatar();
    const vpart_info &vp = pt.info();
    std::string text;
    ok = true;
    if( pt.is_broken() ) {
        ok = format_reqs( text, vp.install_requirements(), vp.install_skills,
                          vp.install_time( player_character ) );
        if( vp.has_flag( "NEEDS_JACKING" ) ) {
            text += _( "<color_white>Additional requirements:</color>\n" );
            const std::pair<bool, std::string> res = calc_lift_requirements( here, vp );
            ok = ok && res.first;
            text += res.second;
        }
        if( pt.has_flag( vp_flag::carried_flag ) ) {
            text += colorize( _( "\nUnracking is required before replacing this part.\n" ), c_red );
            ok = false;
        }
    } else if( !pt.is_repairable() ) {
        text += colorize( _( "This part cannot be repaired.\n" ), c_light_red );
        ok = false;
    } else if( veh->has_part( "NO_MODIFY_VEHICLE" ) && !vp.has_flag( "SIMPLE_PART" ) ) {
        text += colorize( _( "This vehicle cannot be repaired.\n" ), c_light_red );
        ok = false;
    } else {
        const int levels = pt.base.repairable_levels();
        ok = format_reqs( text, vp.repair_requirements() * levels, vp.repair_skills,
                          vp.repair_time( player_character ) * levels );
    }
    if( veh->would_repair_prevent_flyable( pt, player_character ) &&
        !player_character.has_proficiency( proficiency_prof_aircraft_mechanic ) ) {
        text += string_format(
                    _( "\n<color_yellow>You require the \"%s\" proficiency to repair this part safely!</color>\n\n" ),
                    proficiency_prof_aircraft_mechanic->name() );
    }
    return text;
}

void veh_interact::mobile_main_loop( map &here )
{
    Character &player_character = get_player_character();
    const bool owned_by_player = veh->handle_potential_theft( player_character, true );
    const faction *owner_fac = g->faction_manager_ptr->get(
                                   veh->has_owner() ? veh->get_owner() : faction_no_faction );
    // upstream's requirement texts are folded to the width of its message window
    allocate_windows();

    enum whole_vehicle : int {
        refill, siphon, unload, mend, rename, relabel, crew, shape
    };
    int tab = 0;
    int selected = -1;
    while( sel_cmd == VEHICLE_QUIT ) {
        mobile_list::spec what;
        what.title = veh->name;
        what.header = parts_here.empty() ? _( "An empty spot next to the vehicle" ) :
                      string_format( _( "Working on: %s" ), veh->part( parts_here.front() ).name( false ) );
        what.tabs = { std::string( _( "Parts here" ) ), std::string( _( "Whole vehicle" ) ) };
        what.empty_text = _( "No parts here.  Install one, or choose another tile." );
        what.buttons = { std::string( _( "Install a part" ) ), std::string( _( "Other tile" ) ) };

        // what each row's buttons do: true for repair, false for remove
        std::vector<std::vector<bool>> row_actions;
        std::vector<int> row_part;
        for( size_t i = 0; i < parts_here.size(); i++ ) {
            vehicle_part &pt = veh->part( parts_here[i] );
            mobile_list::row r;
            r.name = pt.name( false );
            r.tab = 0;
            r.detail = pt.is_broken() ? _( "Broken" ) :
                       string_format( _( "Condition %d%%" ), static_cast<int>( pt.health_percent() * 100 ) );
            std::vector<std::string> buttons;
            std::vector<bool> actions;
            if( std::find( need_repair.begin(), need_repair.end(),
                           static_cast<int>( i ) ) != need_repair.end() ) {
                bool ok = true;
                r.description += colorize( pt.is_broken() ? _( "To replace it:" ) : _( "To repair it:" ),
                                           c_white ) + "\n" + mobile_repair_text( here, pt, ok ) + "\n";
                if( ok ) {
                    buttons.emplace_back( pt.is_broken() ? _( "Replace" ) : _( "Repair" ) );
                    actions.push_back( true );
                }
            }
            const bool removable = can_remove_part( here, parts_here[i], player_character );
            r.description += msg.value_or( std::string() );
            msg.reset();
            if( removable ) {
                buttons.emplace_back( _( "Remove" ) );
                actions.push_back( false );
            }
            if( !buttons.empty() ) {
                r.action = buttons.front();
                r.more_actions.assign( buttons.begin() + 1, buttons.end() );
            }
            what.rows.push_back( r );
            row_actions.push_back( actions );
            row_part.push_back( parts_here[i] );
        }
        sel_vehicle_part = nullptr;
        sel_vpart_info = nullptr;

        const size_t first_whole = what.rows.size();
        const auto add_whole = [&what]( const std::string & name, const std::string & detail,
        bool enabled ) {
            mobile_list::row r;
            r.name = name;
            r.detail = detail;
            r.tab = 1;
            r.enabled = enabled;
            r.action = name;
            what.rows.push_back( r );
        };
        // in the order of enum whole_vehicle
        add_whole( _( "Refill" ), _( "Fill a tank, a battery or a fuel store from what you have." ),
                   cant_do( here, VEHICLE_REFILL ) == task_reason::CAN_DO );
        add_whole( _( "Siphon" ), _( "Drain a tank into a container.  Needs a hose." ),
                   cant_do( here, VEHICLE_SIPHON ) == task_reason::CAN_DO );
        add_whole( _( "Unload" ), _( "Take solid fuel out of the vehicle." ),
                   cant_do( here, VEHICLE_UNLOAD ) == task_reason::CAN_DO );
        add_whole( _( "Mend" ), _( "Fix a fault in one of the parts." ),
                   cant_do( here, VEHICLE_MEND_FAULTS ) == task_reason::CAN_DO );
        add_whole( _( "Rename" ), _( "Give the vehicle another name." ), owned_by_player );
        add_whole( _( "Label this tile" ), _( "Put a label on the tile you are working on." ),
                   owned_by_player && cant_do( here, VEHICLE_RELABEL ) == task_reason::CAN_DO );
        add_whole( _( "Assign crew" ), _( "Give a seat to one of your allies." ),
                   owned_by_player && cant_do( here, VEHICLE_ASSIGN_CREW ) == task_reason::CAN_DO );
        add_whole( _( "Change shape" ), _( "Choose how the part on this tile looks." ),
                   !parts_here.empty() );

        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            return;
        }
        if( did.button == 1 ) {
            mobile_pick_tile( here );
            tab = 0;
            selected = -1;
            continue;
        }
        if( !veh->handle_potential_theft( player_character ) ) {
            continue;
        }
        if( did.button == 0 ) {
            mobile_install( here );
            continue;
        }
        if( did.row < 0 ) {
            continue;
        }
        if( static_cast<size_t>( did.row ) < first_whole ) {
            const std::vector<bool> &actions = row_actions[did.row];
            if( did.action < 0 || static_cast<size_t>( did.action ) >= actions.size() ) {
                continue;
            }
            if( actions[did.action] ) {
                mobile_repair( here, row_part[did.row] );
            } else {
                mobile_remove( here, row_part[did.row] );
            }
            continue;
        }
        const bool not_yours = !owned_by_player && owner_fac != nullptr;
        switch( static_cast<whole_vehicle>( did.row - first_whole ) ) {
            case refill:
                do_refill( here );
                break;
            case siphon:
                do_siphon( here );
                if( !player_character.activity.is_null() ) {
                    return;
                }
                cache_tool_availability();
                move_cursor( here, point_rel_ms::zero );
                break;
            case unload:
                if( do_unload( here ) ) {
                    return;
                }
                break;
            case mend:
                do_mend( here );
                if( !player_character.activity.is_null() ) {
                    return;
                }
                break;
            case rename:
                if( owned_by_player ) {
                    do_rename();
                } else if( not_yours ) {
                    popup( _( "You cannot rename this vehicle as it is owned by: %s." ), owner_fac->get_name() );
                }
                break;
            case relabel:
                if( owned_by_player ) {
                    do_relabel( here );
                } else if( not_yours ) {
                    popup( _( "You cannot relabel this vehicle as it is owned by: %s." ), owner_fac->get_name() );
                }
                break;
            case crew:
                if( owned_by_player ) {
                    do_assign_crew( here );
                } else if( not_yours ) {
                    popup( _( "You cannot assign crew on this vehicle as it is owned by: %s." ),
                           owner_fac->get_name() );
                }
                break;
            case shape:
                sel_cmd = VEHICLE_SHAPE;
                break;
        }
        tell( msg );
    }
}
