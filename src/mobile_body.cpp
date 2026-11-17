#include "mobile_body.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "avatar_action.h"
#include "body_status_ui.h"
#include "bodygraph.h"
#include "bodypart.h"
#include "character.h"
#include "character_attire.h"
#include "creature.h"
#include "debug.h"
#include "effect.h"
#include "flag.h"
#include "game_inventory.h"
#include "item.h"
#include "item_location.h"
#include "line.h"
#include "messages.h"
#include "mobile_list.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"

static const bodygraph_id bodygraph_full_body( "full_body" );

static const flag_id json_flag_HIDDEN( "HIDDEN" );

static const trait_id trait_DEBUG_HS( "DEBUG_HS" );

namespace mobile_body
{

namespace
{

std::string join( const std::vector<std::string> &parts, const std::string &with )
{
    std::string out;
    for( const std::string &part : parts ) {
        if( part.empty() ) {
            continue;
        }
        if( !out.empty() ) {
            out += with;
        }
        out += part;
    }
    return out;
}

std::string trimmed( std::string text )
{
    while( !text.empty() && ( text.back() == '\n' || text.back() == ' ' ) ) {
        text.pop_back();
    }
    return text;
}

std::string hp_bar( const int cur, const int max )
{
    return remove_color_tags( get_hp_bar( cur, max ).first );
}

// Upstream's text for the part that is selected on the summary tab.
std::string info_text( const body_status_uistate &bs )
{
    std::string out;
    for( const std::string &line : bs.info_txt ) {
        out += ( line == "--" ? std::string() : line ) + "\n";
    }
    return out;
}

// The rows of the summary tab: the parts of the graph that is shown, each with
// upstream's info text.
void add_part_rows( body_status_uistate &bs, mobile_list::spec &what, const int tab,
                    std::vector<int> &part_of_row )
{
    const body_status_tab tab_before = bs.selected_tab;
    bs.selected_tab = body_status_tab::BS_TAB_SUMMARY;
    for( size_t i = 0; i < bs.partlist.size(); ++i ) {
        const bodygraph_part_info &part = bs.partlist[i];
        mobile_list::row r;
        r.tab = tab;
        r.name = uppercase_first_letter( part.sbp_id.is_null() ? part.bp_id->name.translated() :
                                         part.sbp_id->name.translated() );
        r.enabled = part.present_on_character;
        if( part.present_on_character ) {
            bs.update_selected( static_cast<int>( i ) );
            r.right = hp_bar( bs.info.part_hp_cur, bs.info.part_hp_max );
            r.detail = string_format( _( "Encumbrance %1$d · Coverage %2$d%%" ), bs.info.total_encumbrance,
                                      bs.info.avg_coverage );
            r.description = info_text( bs );
            if( !bs.parent_bodypart.has_value() && !part.bg_part->nested_graph.is_null() ) {
                r.action = _( "Its parts" );
            }
        } else {
            r.detail = _( "Missing" );
        }
        what.rows.push_back( r );
        part_of_row.push_back( static_cast<int>( i ) );
    }
    bs.selected_tab = tab_before;
}

// The parts of one body part, as upstream's nested graph.
void sub_parts( body_status_uistate &bs, const int index )
{
    const bodygraph_part_info part = bs.partlist[static_cast<size_t>( index )];
    const bodygraph_id next_graph = part.bg_part->nested_graph;
    if( next_graph.is_null() || !next_graph.is_valid() ) {
        return;
    }
    bs.parent_bodypart = part.bp_id;
    bs.displayed_graph_id = next_graph;
    bs.prepare_partlist();
    mobile_list::spec what;
    what.title = uppercase_first_letter( part.bp_id->name.translated() );
    what.empty_text = _( "Nothing to show." );
    std::vector<int> part_of_row;
    add_part_rows( bs, what, 0, part_of_row );
    int tab = 0;
    int selected = -1;
    mobile_list::show( what, tab, selected );
    bs.parent_bodypart.reset();
    bs.displayed_graph_id = bodygraph_full_body;
    bs.prepare_partlist();
}

mobile_list::row medical_row( const Character &you, const bodypart_id &part )
{
    mobile_list::row r;
    r.name = uppercase_first_letter( body_part_name( part, 1 ) );
    const bool splinted = you.worn_with_flag( flag_SPLINT, part );
    const bool broken = you.is_limb_broken( part );
    std::vector<std::string> marks;
    if( splinted ) {
        marks.emplace_back( _( "Splinted" ) );
    } else if( broken ) {
        marks.emplace_back( _( "Broken" ) );
    }
    for( const effect &eff : you.get_effects_from_bp( part ) ) {
        std::string name = eff.get_speed_name();
        if( name.empty() ) {
            name = eff.disp_name();
        }
        if( !name.empty() ) {
            marks.push_back( name );
        }
    }
    const bool wounded = you.has_wounds( part.id() );
    if( wounded ) {
        marks.emplace_back( _( "Wounded" ) );
    }
    r.detail = join( marks, " · " );
    // the name, health and marks as upstream's list has them
    const std::string upstream_line = remove_color_tags( medical_ui::get_modified_limb_name( you,
                                      part ) );
    const size_t mark_at = upstream_line.find( '[' );
    std::string health = upstream_line.substr( 0, mark_at );
    if( health.rfind( r.name, 0 ) == 0 ) {
        health = health.substr( r.name.size() );
    }
    while( !health.empty() && health.back() == ' ' ) {
        health.pop_back();
    }
    while( !health.empty() && health.front() == ' ' ) {
        health.erase( health.begin() );
    }
    r.right = health;
    r.current = broken || wounded || !marks.empty();
    r.description = string_format( "%s\n%s\n\n%s\n%s\n\n%s\n%s", _( "Limb Effects:" ),
                                   trimmed( medical_ui::get_limb_effects( you, part ) ),
                                   _( "Limb Wounds:" ), trimmed( medical_ui::get_limb_wounds( you, part ) ),
                                   _( "Limb Scores/Modifiers:" ),
                                   trimmed( medical_ui::get_limb_scores_and_modifiers( you, part ) ) );
    // upstream offers its debug wound menu on any limb
    if( wounded || debug_mode || you.has_trait( trait_DEBUG_HS ) ) {
        r.action = _( "Treat wounds" );
    }
    return r;
}

std::string stat_text( const int cur, const int base )
{
    return cur == base ? std::to_string( cur ) : string_format( "%d (%d)", cur, base );
}

void add_stat_rows( const Character &you, mobile_list::spec &what, const int tab )
{
    const auto add = [&]( const std::string & name, const std::string & value,
    const std::string & description ) {
        mobile_list::row r;
        r.tab = tab;
        r.name = name;
        r.right = value;
        r.description = description;
        what.rows.push_back( r );
    };
    add( _( "Current speed" ), std::to_string( you.get_speed() ),
         _( "How fast you act.  100 is normal; pain, hunger, thirst, a heavy load and many effects lower it." ) );
    add( _( "Base move cost" ), std::to_string( you.run_cost( 100 ) ),
         _( "What a step on flat ground costs you.  100 is normal; lower is faster." ) );
    add( _( "Strength" ), stat_text( you.get_str(), you.get_str_base() ), std::string() );
    add( _( "Dexterity" ), stat_text( you.get_dex(), you.get_dex_base() ), std::string() );
    add( _( "Intelligence" ), stat_text( you.get_int(), you.get_int_base() ), std::string() );
    add( _( "Perception" ), stat_text( you.get_per(), you.get_per_base() ), std::string() );
}

} // namespace

bool body_status( body_status_ui &ui )
{
    body_status_uistate &bs = ui.bs_uistate;
    Character &you = ui.displayed_character;
    if( bs.initial_tab == body_status_tab::BS_TAB_SORT_ARMOR ) {
        // upstream comes back here after an item was taken off
        sort_armor( you );
        return false;
    }
    int tab = bs.initial_tab == body_status_tab::BS_TAB_MEDICAL ? 1 : 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = string_format( _( "Status of: %s" ), you.name );
        what.tabs = { _( "Body parts" ), _( "Medical" ), _( "Stats" ) };
        what.empty_text = _( "Nothing to show." );
        std::vector<int> part_of_row;
        bs.displayed_graph_id = bodygraph_full_body;
        bs.prepare_partlist();
        add_part_rows( bs, what, 0, part_of_row );
        const size_t first_medical = what.rows.size();
        bs.all_bodyparts = you.get_all_body_parts( get_body_part_flags::sorted );
        for( const bodypart_id &part : bs.all_bodyparts ) {
            mobile_list::row r = medical_row( you, part );
            r.tab = 1;
            what.rows.push_back( r );
        }
        const size_t first_stat = what.rows.size();
        add_stat_rows( you, what, 2 );
        what.buttons.emplace_back( _( "Sort armor" ) );
        if( you.is_avatar() ) {
            what.buttons.emplace_back( _( "Use an item" ) );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            return bs.wound_fixed;
        }
        if( did.button == 0 ) {
            sort_armor( you );
            if( uistate.open_menu ) {
                // something was taken off; the game comes back to the armor screen
                return bs.wound_fixed;
            }
            continue;
        }
        if( did.button == 1 ) {
            // upstream's "APPLY"
            avatar_action::use_item( *you.as_avatar() );
            return bs.wound_fixed;
        }
        const size_t row = static_cast<size_t>( did.row );
        if( row < first_medical ) {
            sub_parts( bs, part_of_row[row] );
        } else if( row < first_stat ) {
            // as body_status_callback::confirm
            if( you.pick_wound_fix( bs.all_bodyparts[row - first_medical] ) ) {
                bs.wound_fixed = true;
                return true;
            }
        }
    }
}

namespace
{

using worn_iterator = std::list<item>::iterator;

// the tab the armor screen was on, for when the game comes back to it
// NOLINTNEXTLINE(cata-static-declarations)
bodypart_str_id armor_tab_part = bodypart_str_id::NULL_ID();

std::string volume_text( const units::volume &volume )
{
    return string_format( "%.2f %s", convert_volume( units::to_milliliter( volume ) ),
                          volume_units_abbr() );
}

std::string part_encumbrance_text( const Character &guy, const bodypart_id &bp )
{
    const int total = guy.get_part_encumbrance( bp );
    const int layers = guy.get_part_layer_penalty( bp );
    //~ %1$s: body part, %2$d: encumbrance from what is worn, %3$d: extra encumbrance from layering
    return string_format( _( "%1$s encumbrance: %2$d + %3$d" ),
                          uppercase_first_letter( body_part_name( bp, 1 ) ),
                          total - layers, layers );
}

// What upstream's screen says about one worn item, seen from one body part
// (or from all of them).
std::string armor_text( Character &guy, const worn_iterator &it, const bodypart_id &bp )
{
    const item &worn_item = *it;
    std::vector<std::string> lines;
    lines.push_back( clothing_info::clothing_layer( worn_item ) );
    for( const std::string &line : clothing_info::clothing_flags_description( worn_item ) ) {
        lines.push_back( line );
    }
    const item_penalties penalties = guy.worn.get_item_penalties( it, guy, bp );
    const auto names = []( const std::vector<bodypart_id> &parts ) {
        std::set<std::string> unique;
        for( const bodypart_id &part : parts ) {
            unique.insert( body_part_name( part, 1 ) );
        }
        return join( std::vector<std::string>( unique.begin(), unique.end() ), ", " );
    };
    if( !penalties.body_parts_with_stacking_penalty.empty() ) {
        lines.push_back( string_format(
                             _( "Wearing several items on the same layer adds encumbrance here: %s." ),
                             names( penalties.body_parts_with_stacking_penalty ) ) );
    }
    if( !penalties.body_parts_with_out_of_order_penalty.empty() ) {
        lines.push_back( penalties.bad_items_within.empty() ?
                         string_format(
                             _( "Wearing this outside items it would normally be beneath adds encumbrance here: %s." ),
                             names( penalties.body_parts_with_out_of_order_penalty ) ) :
                         string_format( _( "Wearing this outside your %1$s adds encumbrance here: %2$s." ),
                                        *penalties.bad_items_within.begin(),
                                        names( penalties.body_parts_with_out_of_order_penalty ) ) );
    }
    lines.emplace_back( " " );
    for( const std::string &line : clothing_info::clothing_properties( worn_item, guy, bp ) ) {
        lines.push_back( line );
    }
    if( worn_item.get_volume_capacity() > 0_ml ) {
        lines.push_back( string_format( _( "Storage: %s" ),
                                        volume_text( worn_item.get_volume_capacity() ) ) );
    }
    lines.emplace_back( " " );
    for( const std::string &line : clothing_info::clothing_protection( worn_item, bp ) ) {
        lines.push_back( line );
    }
    lines.emplace_back( " " );
    if( bp == bodypart_str_id::NULL_ID() ) {
        for( const bodypart_id &part : guy.get_all_body_parts( get_body_part_flags::sorted ) ) {
            if( worn_item.covers( part ) ) {
                lines.push_back( part_encumbrance_text( guy, part ) );
            }
        }
    } else {
        lines.push_back( part_encumbrance_text( guy, bp ) );
    }
    return join( lines, "\n" );
}

// Upstream's right pane: each body part with what covers it.  Returns the part
// whose items the player wants to see, if any.
std::optional<bodypart_id> encumbrance_list( Character &guy )
{
    mobile_list::spec what;
    what.title = _( "Encumbrance" );
    what.header = _( "Worn items + layering penalty" );
    std::vector<bodypart_id> shown;
    std::set<bodypart_id> done;
    for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::sorted ) ) {
        // as upstream, a pair of limbs wearing the same is one entry
        const bool both = guy.worn.items_cover_opposite_bp( guy, bp );
        if( both && done.count( bp->opposite_part.id() ) > 0 ) {
            continue;
        }
        done.insert( bp );
        mobile_list::row r;
        r.name = uppercase_first_letter( body_part_name( bp, both ? 2 : 1 ) );
        const int total = guy.get_part_encumbrance( bp );
        const int layers = guy.get_part_layer_penalty( bp );
        r.right = string_format( "%d + %d", total - layers, layers );
        const std::vector<layering_item_info> items = guy.worn.items_cover_bp( guy, bp );
        std::vector<std::string> lines;
        for( const layering_item_info &elem : items ) {
            std::string line = string_format( "%s: %d%s", elem.name, elem.encumber,
                                              elem.penalties.badness() > 0 ? "+" : "" );
            if( elem.storage > 0_ml ) {
                line += string_format( _( ", holds %s" ), volume_text( elem.storage ) );
            }
            if( elem.hidden ) {
                line += _( ", hidden" );
            }
            lines.push_back( line );
        }
        r.detail = items.empty() ? std::string( _( "Nothing worn" ) ) :
                   string_format( n_gettext( "%d item", "%d items", items.size() ), static_cast<int>( items.size() ) );
        r.description = items.empty() ? std::string( _( "No armor on this limb!" ) ) :
                        string_format(
                            _( "Innermost first; the number is the item's encumbrance, + marks a layering penalty.\n\n%s" ),
                            join( lines, "\n" ) );
        if( !items.empty() ) {
            r.action = _( "Show these items" );
        }
        what.rows.push_back( r );
        shown.push_back( bp );
    }
    int tab = 0;
    int selected = -1;
    const int chosen = mobile_list::show( what, tab, selected );
    if( chosen < 0 || static_cast<size_t>( chosen ) >= shown.size() ) {
        return std::nullopt;
    }
    return shown[static_cast<size_t>( chosen )];
}

// EQUIP_ARMOR of upstream's screen.
void equip( Character &guy )
{
    item_location loc = game_menus::inv::wear( guy );
    if( !loc ) {
        return;
    }
    const std::string item_name = loc->display_name();
    item_location obtained = loc.obtain( guy );
    if( obtained ) {
        if( !guy.wear( obtained ) && guy.is_npc() ) {
            popup( _( "Can't put this on!" ) );
        }
    } else {
        guy.add_msg_if_player( _( "You chose not to wear the %s." ), item_name );
    }
}

} // namespace

void sort_armor( Character &guy )
{
    Character &player_character = get_player_character();
    outfit &worn = guy.worn;
    bodypart_id tab_part = armor_tab_part.id();
    const item *keep = nullptr;
    while( true ) {
        // the checks of upstream's loop
        if( guy.is_avatar() ) {
            if( player_character.get_moves() < 0 ) {
                uistate.open_menu = [guy_ptr = &guy]() {
                    guy_ptr->worn.sort_armor( *guy_ptr );
                };
                return;
            }
        } else {
            if( rl_dist( player_character.pos_bub(), guy.pos_bub() ) > 1 ) {
                guy.add_msg_if_npc( m_bad, _( "%s is too far to sort armor." ), guy.get_name() );
                return;
            }
            if( guy.attitude_to( player_character ) != Creature::Attitude::FRIENDLY ) {
                guy.add_msg_if_npc( m_bad, _( "%s is not friendly!" ), guy.get_name() );
                return;
            }
        }
        guy.calc_encumbrance();

        mobile_list::spec what;
        what.title = guy.is_avatar() ? std::string( _( "Sort Armor" ) ) :
                     string_format( "%s: %s", _( "Sort Armor" ), guy.get_name() );
        what.header = _( "Innermost at the top, outermost at the bottom" );
        what.empty_text = _( "Nothing is worn." );
        what.tabs.emplace_back( _( "All" ) );
        std::vector<bodypart_id> tab_parts = { bodypart_str_id::NULL_ID().id() };
        for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::sorted ) ) {
            if( !worn.filter_by_bodypart( bp ).empty() ) {
                tab_parts.push_back( bp );
                what.tabs.push_back( uppercase_first_letter( body_part_name( bp, 1 ) ) );
            }
        }
        int tab = 0;
        int selected = -1;
        // each row: the items of its tab, and its place among them
        std::vector<std::vector<worn_iterator>> subsets;
        std::vector<std::pair<int, int>> where;
        for( size_t t = 0; t < tab_parts.size(); ++t ) {
            const bodypart_id &bp = tab_parts[t];
            if( bp == tab_part ) {
                tab = static_cast<int>( t );
            }
            subsets.push_back( worn.filter_by_bodypart( bp ) );
            const std::vector<worn_iterator> &subset = subsets.back();
            for( size_t i = 0; i < subset.size(); ++i ) {
                const worn_iterator &it = subset[i];
                mobile_list::row r;
                r.tab = static_cast<int>( t );
                r.name = it->tname();
                const item_penalties penalties = worn.get_item_penalties( it, guy, bp );
                std::vector<std::string> marks;
                if( bp == bodypart_str_id::NULL_ID() ) {
                    if( it->get_volume_capacity() > 0_ml ) {
                        r.right = volume_text( it->get_volume_capacity() );
                    }
                } else {
                    r.right = string_format( "%d%s", it->get_encumber( guy, bp ),
                                             penalties.badness() > 0 ? "+" : "" );
                    marks.push_back( string_format( _( "Coverage %d%%" ), it->get_coverage( bp ) ) );
                }
                if( penalties.badness() > 0 ) {
                    marks.emplace_back( _( "Layering penalty" ) );
                }
                if( it->has_flag( json_flag_HIDDEN ) ) {
                    marks.emplace_back( _( "Hidden" ) );
                }
                if( it->is_favorite ) {
                    marks.emplace_back( _( "Favorite" ) );
                }
                if( it->has_flag( flag_INTEGRATED ) ) {
                    marks.emplace_back( _( "Part of you" ) );
                }
                r.detail = join( marks, " · " );
                r.current = penalties.badness() > 0;
                r.description = armor_text( guy, it, bp );
                r.action = _( "Move in" );
                r.more_actions = { _( "Move out" ), _( "Take off" ), _( "More…" ) };
                if( keep == &*it && static_cast<int>( t ) == tab && bp == tab_part ) {
                    selected = static_cast<int>( what.rows.size() );
                }
                what.rows.push_back( r );
                where.emplace_back( static_cast<int>( t ), static_cast<int>( i ) );
            }
        }
        what.buttons = { _( "Wear something" ), _( "Sort by layer" ), _( "Encumbrance" ) };

        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        tab_part = tab_parts[static_cast<size_t>( std::clamp( tab, 0,
                                                  static_cast<int>( tab_parts.size() ) - 1 ) )];
        armor_tab_part = tab_part.id();
        keep = selected >= 0 && static_cast<size_t>( selected ) < where.size() ?
               &*subsets[static_cast<size_t>( where[static_cast<size_t>( selected )].first )]
               [static_cast<size_t>( where[static_cast<size_t>( selected )].second )] : nullptr;
        if( did.went_back() ) {
            uistate.open_menu.reset();
            return;
        }
        if( did.button == 0 ) {
            equip( guy );
            continue;
        }
        if( did.button == 1 ) {
            // upstream's SORT_ARMOR
            worn.sort();
            continue;
        }
        if( did.button == 2 ) {
            if( const std::optional<bodypart_id> part = encumbrance_list( guy ) ) {
                tab_part = *part;
                keep = nullptr;
            }
            continue;
        }
        if( did.row < 0 || static_cast<size_t>( did.row ) >= where.size() ) {
            continue;
        }
        const size_t which_subset = static_cast<size_t>( where[static_cast<size_t>( did.row )].first );
        const std::vector<worn_iterator> &subset = subsets[which_subset];
        const int index = where[static_cast<size_t>( did.row )].second;
        const worn_iterator chosen = subset[static_cast<size_t>( index )];
        if( did.action == 0 || did.action == 1 ) {
            // MOVE_ARMOR_IN and MOVE_ARMOR_OUT of body_status_ui::handle_action
            const int other = did.action == 0 ? index - 1 : index + 1;
            if( other < 0 || other >= static_cast<int>( subset.size() ) ) {
                continue;
            }
            worn_iterator target = subset[static_cast<size_t>( other )];
            if( chosen->has_flag( flag_INTEGRATED ) || target->has_flag( flag_INTEGRATED ) ) {
                popup( _( "Can't move your integrated armor!" ) );
                continue;
            }
            worn.splice( did.action == 0 ? target : ++target, chosen );
        } else if( did.action == 2 ) {
            // REMOVE_ARMOR
            if( player_character.query_yn( _( "Remove selected armor?" ) ) ) {
                uistate.open_menu = [guy_ptr = &guy]() {
                    guy_ptr->worn.sort_armor( *guy_ptr );
                };
                guy.takeoff( item_location( guy, &*chosen ) );
                return;
            }
        } else {
            uilist menu;
            menu.title = chosen->tname();
            menu.addentry( 0, chosen->is_sided(), MENU_AUTOASSIGN, _( "Change side" ) );
            menu.addentry( 1, true, MENU_AUTOASSIGN, chosen->has_flag( json_flag_HIDDEN ) ?
                           _( "Show on your character" ) : _( "Hide on your character" ) );
            menu.addentry( 2, true, MENU_AUTOASSIGN, chosen->is_favorite ?
                           _( "Remove from favorites" ) : _( "Mark as favorite" ) );
            menu.query();
            if( menu.ret == 0 ) {
                // CHANGE_SIDE
                if( player_character.query_yn( _( "Swap side for %s?" ), chosen->tname() ) ) {
                    guy.change_side( *chosen );
                }
            } else if( menu.ret == 1 ) {
                // TOGGLE_CLOTH
                if( chosen->has_flag( json_flag_HIDDEN ) ) {
                    chosen->unset_flag( json_flag_HIDDEN );
                } else {
                    chosen->set_flag( json_flag_HIDDEN );
                }
            } else if( menu.ret == 2 ) {
                // TOGGLE_FAVORITE
                chosen->set_favorite( !chosen->is_favorite );
            }
        }
    }
}

void morale( const morale_data &data )
{
    const auto signed_or_dash = []( const int num ) {
        return num == 0 ? std::string( "-" ) : string_format( "%+d", num );
    };
    mobile_list::spec what;
    what.title = _( "Morale" );
    what.header = string_format( "%s %s  ·  %s %d", _( "Total morale:" ), signed_or_dash( data.total ),
                                 _( "Focus trends towards:" ), data.focus_equilibrium );
    what.empty_text = _( "Nothing affects your morale" );
    const auto caption = [&what]( const std::string & name, const std::string & value,
    const std::string & description ) {
        mobile_list::row r;
        r.name = name;
        r.right = value;
        r.description = description;
        r.current = true;
        what.rows.push_back( r );
    };
    const auto sources = [&what]( const std::vector<std::pair<std::string, int>> &list,
    const std::string & of_what ) {
        for( const std::pair<std::string, int> &source : list ) {
            mobile_list::row r;
            r.name = source.first;
            r.right = string_format( "%d%%", source.second );
            r.description = string_format( of_what, source.second );
            what.rows.push_back( r );
        }
    };
    if( !data.positive.empty() || !data.negative.empty() ) {
        caption( _( "Total positive morale" ), signed_or_dash( data.total_positive ),
                 _( "Everything that lifts your morale, added up.  The sources follow, each with its share." ) );
        sources( data.positive, _( "This is %d%% of what lifts your morale." ) );
        caption( _( "Total negative morale" ), signed_or_dash( -data.total_negative ),
                 _( "Everything that lowers your morale, added up.  The sources follow, each with its share." ) );
        sources( data.negative, _( "This is %d%% of what lowers your morale." ) );
    }
    // upstream's bottom lines
    caption( _( "Total morale:" ), signed_or_dash( data.total ),
             _( "Your morale as the game uses it.  It pulls your focus up or down." ) );
    const auto line = [&what]( const std::string & name, const std::string & value ) {
        mobile_list::row r;
        r.name = name;
        r.right = value;
        what.rows.push_back( r );
    };
    line( data.modifier_label, string_format( "%d%%", data.modifier ) );
    if( data.pain_penalty != 0 ) {
        line( _( "Pain level:" ), signed_or_dash( -data.pain_penalty ) );
    }
    if( data.sleepiness_penalty != 0 ) {
        line( _( "Sleepiness level:" ), signed_or_dash( -data.sleepiness_penalty ) );
    }
    line( _( "Focus trends towards:" ), std::to_string( data.focus_equilibrium ) );
    int tab = 0;
    int selected = -1;
    mobile_list::show( what, tab, selected );
}

void blood_test( const std::vector<std::string> &lines, const std::vector<bool> &good )
{
    mobile_list::spec what;
    what.title = _( "Blood Test Results" );
    what.empty_text = _( "No effects." );
    for( size_t i = 0; i < lines.size(); ++i ) {
        mobile_list::row r;
        r.name = lines[i];
        const bool is_good = i < good.size() && good[i];
        r.right = is_good ? _( "Good" ) : _( "Bad" );
        r.current = !is_good;
        what.rows.push_back( r );
    }
    int tab = 0;
    int selected = -1;
    mobile_list::show( what, tab, selected );
}

} // namespace mobile_body
