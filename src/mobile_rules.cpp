#include "mobile_rules.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "auto_pickup.h"
#include "character.h"
#include "creature.h"
#include "game.h"
#include "input_popup.h"
#include "mobile_list.h"
#include "options.h"
#include "output.h"
#include "safemode_ui.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "translation.h"
#include "translations.h"
#include "uilist.h"

namespace
{

// Upstream writes its tab titles as "[<Global>]", with the shortcut marked.
std::string plain_title( const std::string &title )
{
    std::string result;
    for( const char c : title ) {
        if( c != '[' && c != ']' && c != '<' && c != '>' ) {
            result += c;
        }
    }
    return result;
}

// A sheet of choices.  Returns the id of the one tapped, or a negative number.
int choose( const std::string &title, const std::vector<std::pair<int, std::string>> &choices )
{
    uilist menu;
    menu.text = title;
    for( const std::pair<int, std::string> &choice : choices ) {
        menu.addentry( choice.first, true, MENU_AUTOASSIGN, choice.second );
    }
    menu.query();
    return menu.ret;
}

// What the "More…" sheet of a rule offers, in both managers.
enum more_choice : int {
    more_flip = 0,
    more_up,
    more_down,
    more_copy,
    more_swap,
    more_test,
    more_remove,
};

// The row of the list screen that shows rule number index of tab number tab.
template<typename Lists>
int row_of( const Lists &lists, const size_t tab, const size_t index )
{
    size_t before = 0;
    for( size_t i = 0; i < tab && i < lists.size(); ++i ) {
        before += lists[i]->size();
    }
    return static_cast<int>( before + index );
}

// The things both managers do to a rule in a list: move it, copy it, hand it to
// the other list, remove it.  Returns false if it was none of these.  tab and
// line follow the rule.
template<typename Rules>
bool common_choice( const int choice, std::vector<Rules *> &lists, size_t &tab, size_t &line )
{
    Rules &cur = *lists[tab];
    switch( choice ) {
        case more_up:
            if( line > 0 ) {
                std::swap( cur[line], cur[line - 1] );
                line--;
            }
            return true;
        case more_down:
            if( line + 1 < cur.size() ) {
                std::swap( cur[line], cur[line + 1] );
                line++;
            }
            return true;
        case more_copy:
            cur.push_back( cur[line] );
            line = cur.size() - 1;
            return true;
        case more_swap: {
            if( lists.size() != 2 ) {
                return true;
            }
            const size_t other = ( tab + 1 ) % 2;
            lists[other]->push_back( cur[line] );
            cur.erase( cur.begin() + static_cast<std::ptrdiff_t>( line ) );
            tab = other;
            line = lists[other]->size() - 1;
            return true;
        }
        case more_remove:
            cur.erase( cur.begin() + static_cast<std::ptrdiff_t>( line ) );
            return true;
        default:
            return false;
    }
}

} // namespace

namespace mobile_rules
{

// Upstream's question for the text of a pickup rule.  Returns nothing entered
// as an empty text, which upstream does not accept as a rule.
static std::string ask_pickup_rule( const std::string &old )
{
    string_input_popup_imgui popup( 60, old );
    popup.set_label( _( "Pickup Rule:" ) );
    popup.set_description( _(
                               "* is used as a Wildcard.  A few Examples:\n"
                               "\n"
                               "wooden arrow    matches the itemname exactly\n"
                               "wooden ar*      matches items beginning with wood ar\n"
                               "*rrow           matches items ending with rrow\n"
                               "*avy fle*fi*arrow     multiple * are allowed\n"
                               "heAVY*woOD*arrOW      case insensitive search\n"
                               "\n"
                               "Pickup based on item materials:\n"
                               "m:kevlar        matches items made of Kevlar\n"
                               "M:copper        matches items made purely of copper\n"
                               "M:steel,iron    multiple materials allowed (OR search)"
                           ), c_white, true );
    return popup.query();
}

void pickup_rules( auto_pickup::user_interface &ui )
{
    if( ui.tabs.empty() ) {
        return;
    }
    const bool has_character = !get_player_character().name.empty();
    std::vector<auto_pickup::rule_list *> lists;
    for( auto_pickup::user_interface::tab &t : ui.tabs ) {
        lists.push_back( &t.new_rules );
    }
    int tab = 0;
    int selected = -1;
    ui.bStuffChanged = false;
    while( true ) {
        const bool enabled = get_option<bool>( "AUTO_PICKUP" );
        mobile_list::spec what;
        what.title = ui.title;
        what.header = enabled ? _( "Auto pickup is on." ) :
                      _( "Auto pickup is off: no rule picks anything up." );
        what.empty_text = _( "No rules in this list." );
        what.buttons = { std::string( _( "Add rule" ) ),
                         std::string( enabled ? _( "Turn auto pickup off" ) : _( "Turn auto pickup on" ) )
                       };
        // which tab and which rule each row is
        std::vector<std::pair<size_t, size_t>> where;
        for( size_t t = 0; t < ui.tabs.size(); ++t ) {
            what.tabs.push_back( plain_title( ui.tabs[t].title ) );
            const auto_pickup::rule_list &rules = *lists[t];
            for( size_t i = 0; i < rules.size(); ++i ) {
                const auto_pickup::rule &rule = rules[i];
                mobile_list::row r;
                r.name = rule.sRule.empty() ? _( "<empty rule>" ) : rule.sRule;
                r.detail = rule.bExclude ? _( "Exclude" ) : _( "Include" );
                r.right = rule.bActive ? _( "On" ) : _( "Off" );
                r.current = rule.bActive;
                r.enabled = rule.bActive;
                r.tab = static_cast<int>( t );
                r.description = rule.bExclude ?
                                _( "Items with a name like this are left where they are, even if another rule includes them." )
                                : _( "Items with a name like this are picked up when you walk over them." );
                if( !rule.bActive ) {
                    r.description += "\n\n" + std::string( _( "This rule is off and does nothing." ) );
                }
                r.action = _( "Edit" );
                r.more_actions = { std::string( rule.bActive ? _( "Turn off" ) : _( "Turn on" ) ),
                                   std::string( _( "More…" ) )
                                 };
                what.rows.push_back( r );
                where.emplace_back( t, i );
            }
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            break;
        }
        if( did.button == 1 ) {
            get_options().get_option( "AUTO_PICKUP" ).setNext();
            get_options().save();
            continue;
        }
        if( did.button == 0 ) {
            const size_t t = static_cast<size_t>( std::clamp( tab, 0,
                                                  static_cast<int>( lists.size() ) - 1 ) );
            const std::string text = ask_pickup_rule( "" );
            if( !text.empty() ) {
                lists[t]->push_back( auto_pickup::rule( wildcard_trim_rule( text ), true, false ) );
                ui.bStuffChanged = true;
                selected = row_of( lists, t, lists[t]->size() - 1 );
            }
            continue;
        }
        if( did.row < 0 || static_cast<size_t>( did.row ) >= where.size() ) {
            continue;
        }
        size_t t = where[static_cast<size_t>( did.row )].first;
        size_t line = where[static_cast<size_t>( did.row )].second;
        auto_pickup::rule &rule = ( *lists[t] )[line];
        if( did.action == 0 ) {
            const std::string text = ask_pickup_rule( rule.sRule );
            if( !text.empty() ) {
                rule.sRule = wildcard_trim_rule( text );
                ui.bStuffChanged = true;
            }
        } else if( did.action == 1 ) {
            rule.bActive = !rule.bActive;
            ui.bStuffChanged = true;
        } else {
            std::vector<std::pair<int, std::string>> choices;
            choices.emplace_back( more_flip, rule.bExclude ? _( "Change to Include" ) :
                                  _( "Change to Exclude" ) );
            choices.emplace_back( more_up, _( "Move up" ) );
            choices.emplace_back( more_down, _( "Move down" ) );
            choices.emplace_back( more_copy, _( "Copy" ) );
            if( lists.size() == 2 ) {
                choices.emplace_back( more_swap, string_format( _( "Move to the list \"%s\"" ),
                                      plain_title( ui.tabs[( t + 1 ) % 2].title ) ) );
            }
            if( has_character ) {
                choices.emplace_back( more_test, _( "Test: show what it matches" ) );
            }
            choices.emplace_back( more_remove, _( "Remove" ) );
            const int choice = choose( rule.sRule, choices );
            if( choice == more_flip ) {
                rule.bExclude = !rule.bExclude;
                ui.bStuffChanged = true;
            } else if( choice == more_test ) {
                rule.test_pattern();
            } else if( common_choice( choice, lists, t, line ) ) {
                ui.bStuffChanged = true;
                tab = static_cast<int>( t );
                selected = choice == more_remove ? -1 : row_of( lists, t, line );
            }
        }
    }

    if( !ui.bStuffChanged ) {
        return;
    }
    if( !query_yn( _( "Save changes?" ) ) ) {
        return;
    }
    for( auto_pickup::user_interface::tab &t : ui.tabs ) {
        t.rules.get() = t.new_rules;
    }
}

void show_matches( const std::string &title, const std::vector<std::string> &names,
                   const std::string &note )
{
    mobile_list::spec what;
    what.title = title;
    what.header = note;
    what.empty_text = _( "Nothing matches." );
    for( const std::string &name : names ) {
        mobile_list::row r;
        r.name = name;
        what.rows.push_back( r );
    }
    int tab = 0;
    int selected = -1;
    mobile_list::show( what, tab, selected );
}

void scores( const score_lists &lists )
{
    mobile_list::spec what;
    what.title = _( "Your scores" );
    what.tabs = { std::string( _( "Achievements" ) ), std::string( _( "Conducts" ) ),
                  std::string( _( "Scores" ) ), std::string( _( "Kills" ) )
                };
    what.empty_text = _( "Nothing to show here in this game." );
    int monsters = 0;
    for( const auto &entry : lists.monster_kills ) {
        monsters += std::get<0>( entry );
    }
    what.header = string_format( _( "Kills: %1$d monsters, %2$d NPCs" ), monsters,
                                 static_cast<int>( lists.npc_kills.size() ) );
    if( !lists.achievements_enabled ) {
        what.header += "  ·  " +
                       std::string( _( "Achievements and conducts are disabled for debug characters." ) );
    }
    // an achievement's text is its name on the first line, then lines about it
    const auto add_achievements = [&what]( const std::vector<std::string> &texts, const int tab ) {
        for( const std::string &text : texts ) {
            mobile_list::row r;
            r.tab = tab;
            const size_t name_end = text.find( '\n' );
            r.name = text.substr( 0, name_end );
            std::string rest;
            size_t at = name_end;
            while( at != std::string::npos && at + 1 < text.size() ) {
                const size_t line_end = text.find( '\n', at + 1 );
                const std::string line = trim( text.substr( at + 1, line_end == std::string::npos ?
                                               std::string::npos : line_end - at - 1 ) );
                if( !line.empty() ) {
                    rest += ( rest.empty() ? "" : "\n" ) + line;
                }
                at = line_end;
            }
            r.detail = rest;
            what.rows.push_back( r );
        }
    };
    if( lists.achievements_enabled ) {
        add_achievements( lists.achievements, 0 );
        add_achievements( lists.conducts, 1 );
    }
    for( const std::string &text : lists.scores ) {
        mobile_list::row r;
        r.tab = 2;
        r.name = text;
        what.rows.push_back( r );
    }
    for( const auto &entry : lists.monster_kills ) {
        mobile_list::row r;
        r.tab = 3;
        r.name = std::get<3>( entry );
        r.right = std::to_string( std::get<0>( entry ) );
        what.rows.push_back( r );
    }
    for( const std::string &name : lists.npc_kills ) {
        mobile_list::row r;
        r.tab = 3;
        r.name = name;
        r.detail = _( "NPC" );
        r.right = "1";
        what.rows.push_back( r );
    }
    int tab = 0;
    int selected = -1;
    mobile_list::show( what, tab, selected );
}

} // namespace mobile_rules

// The safe mode manager as a list screen.  What each choice does to a rule, and
// saving, follow safemode::show.
void safemode::mobile_show( const std::string &custom_name_in, const bool is_safemode_in )
{
    const std::vector<rules_class> global_rules_old = global_rules;
    const std::vector<rules_class> character_rules_old = character_rules;
    const bool has_character = !get_player_character().name.empty();
    std::vector<std::vector<rules_class> *> lists = { &global_rules };
    std::vector<std::string> titles = { std::string( _( "Global" ) ) };
    if( has_character ) {
        lists.push_back( &character_rules );
        titles.emplace_back( _( "Character" ) );
    }
    const auto category_name = []( const Categories category ) {
        return category == Categories::SOUND ? _( "Sound" ) : _( "Hostile" );
    };
    const auto list_name = []( const bool whitelist ) {
        return whitelist ? _( "Whitelist" ) : _( "Blacklist" );
    };
    const auto mode_name = []( const MovementModes mode ) {
        return mode == MovementModes::WALKING ? _( "Walking" ) :
               mode == MovementModes::DRIVING ? _( "Driving" ) : _( "Both" );
    };
    const auto has_attitude = []( const rules_class & rule ) {
        return rule.category == Categories::HOSTILE_SPOTTED;
    };
    const auto has_distance = []( const rules_class & rule ) {
        return rule.category == Categories::SOUND || !rule.whitelist;
    };

    // upstream's examples, shown there in a window next to the field
    const auto rule_help = []( const Categories category ) {
        return category == Categories::SOUND ?
               _( "* is used as a Wildcard.  A few Examples:\n"
                  "\n"
                  "footsteps      matches the sound name exactly\n"
                  "a loud ba*     matches sounds beginning with 'a loud ba'\n"
                  "*losion!       matches sounds ending with 'losion!'\n"
                  "a *oud*ba*     multiple * are allowed\n"
                  "*LoU*bA*       case insensitive search" ) :
               _( "* is used as a Wildcard.  A few Examples:\n"
                  "\n"
                  "human          matches every NPC\n"
                  "zombie         matches the monster name exactly\n"
                  "acidic zo*     matches monsters beginning with 'acidic zo'\n"
                  "*mbie          matches monsters ending with 'mbie'\n"
                  "*cid*zo*ie     multiple * are allowed\n"
                  "AcI*zO*iE      case insensitive search" );
    };

    int tab = 0;
    int selected = -1;
    bool changes_made = false;
    while( true ) {
        mobile_list::spec what;
        what.title = custom_name_in;
        what.header = string_format( "%s %s", _( "Safe mode enabled:" ),
                                     g->safe_mode == SAFE_MODE_ON ? _( "True" ) : _( "False" ) );
        what.tabs = titles;
        what.empty_text = empty() ?
                          _( "Safe mode manager is currently inactive.  Default rules are used.  Add a rule to activate." )
                          : _( "No rules in this list." );
        what.buttons = { std::string( _( "Add rule" ) ), std::string( _( "Add default rules" ) ) };
        std::vector<std::pair<size_t, size_t>> where;
        for( size_t t = 0; t < lists.size(); ++t ) {
            const std::vector<rules_class> &rules = *lists[t];
            for( size_t i = 0; i < rules.size(); ++i ) {
                const rules_class &rule = rules[i];
                mobile_list::row r;
                r.name = rule.rule.empty() ? _( "<empty rule>" ) : rule.rule;
                // upstream's category "Hostile" would read oddly next to the attitude
                r.detail = string_format( "%s · %s", list_name( rule.whitelist ), has_attitude( rule ) ?
                                          string_format( _( "Attitude: %s" ),
                                                  Creature::get_attitude_ui_data( rule.attitude ).first.translated() ) :
                                          std::string( category_name( rule.category ) ) );
                if( has_distance( rule ) ) {
                    r.detail += " · " + string_format( _( "Dist %d" ), rule.proximity );
                }
                r.detail += " · " + std::string( mode_name( rule.movement_mode ) );
                r.right = rule.active ? _( "On" ) : _( "Off" );
                r.current = rule.active;
                r.enabled = rule.active;
                r.tab = static_cast<int>( t );
                r.description = rule.category == Categories::SOUND ?
                                ( rule.whitelist ?
                                  _( "Sounds with a name like this do not stop you." ) :
                                  _( "Sounds with a name like this, heard within the distance, stop you while safe mode is on." ) )
                                : ( rule.whitelist ?
                                    _( "Creatures with a name like this do not stop you." ) :
                                    _( "Creatures with a name like this, seen within the distance, stop you while safe mode is on.  Distance 0 is as far as you can see." ) );
                if( !rule.active ) {
                    r.description += "\n\n" + std::string( _( "This rule is off and does nothing." ) );
                }
                r.action = _( "Change…" );
                r.more_actions = { std::string( rule.active ? _( "Turn off" ) : _( "Turn on" ) ),
                                   std::string( _( "More…" ) )
                                 };
                what.rows.push_back( r );
                where.emplace_back( t, i );
            }
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            break;
        }
        if( did.button >= 0 ) {
            const size_t t = static_cast<size_t>( std::clamp( tab, 0,
                                                  static_cast<int>( lists.size() ) - 1 ) );
            std::vector<rules_class> &cur = *lists[t];
            if( did.button == 0 ) {
                string_input_popup ask;
                const std::string text = ask.title( _( "Safe Mode Rule:" ) ).width( 30 ).description(
                                             rule_help( Categories::HOSTILE_SPOTTED ) ).query_string();
                if( ask.canceled() || text.empty() ) {
                    continue;
                }
                cur.emplace_back( wildcard_trim_rule( text ), true, false, Creature::Attitude::HOSTILE,
                                  get_option<int>( "SAFEMODEPROXIMITY" ), Categories::HOSTILE_SPOTTED,
                                  MovementModes::BOTH );
            } else {
                cur.emplace_back( "*", true, false, Creature::Attitude::HOSTILE,
                                  get_option<int>( "SAFEMODEPROXIMITY" ), Categories::HOSTILE_SPOTTED,
                                  MovementModes::BOTH );
                cur.emplace_back( "*", true, true, Creature::Attitude::HOSTILE, 5, Categories::SOUND,
                                  MovementModes::BOTH );
            }
            changes_made = true;
            selected = row_of( lists, t, cur.size() - 1 );
            continue;
        }
        if( did.row < 0 || static_cast<size_t>( did.row ) >= where.size() ) {
            continue;
        }
        size_t t = where[static_cast<size_t>( did.row )].first;
        size_t line = where[static_cast<size_t>( did.row )].second;
        rules_class &rule = ( *lists[t] )[line];
        if( did.action == 0 ) {
            enum change : int { text, list, category, attitude, distance, mode };
            std::vector<std::pair<int, std::string>> choices;
            choices.emplace_back( text, string_format( _( "Rule: %s" ), rule.rule ) );
            choices.emplace_back( list, string_format( _( "List: %s" ), list_name( rule.whitelist ) ) );
            choices.emplace_back( category, string_format( _( "Category: %s" ),
                                  category_name( rule.category ) ) );
            if( has_attitude( rule ) ) {
                choices.emplace_back( attitude, string_format( _( "Attitude: %s" ),
                                      Creature::get_attitude_ui_data( rule.attitude ).first.translated() ) );
            }
            if( has_distance( rule ) ) {
                choices.emplace_back( distance, string_format( _( "Distance: %d" ), rule.proximity ) );
            }
            choices.emplace_back( mode, string_format( _( "Mode: %s" ), mode_name( rule.movement_mode ) ) );
            switch( choose( _( "What do you want to change?" ), choices ) ) {
                case text: {
                    string_input_popup ask;
                    const std::string entered = ask.title( _( "Safe Mode Rule:" ) ).width( 30 ).text(
                                                    rule.rule ).description( rule_help( rule.category ) ).query_string();
                    if( !ask.canceled() && !entered.empty() ) {
                        rule.rule = wildcard_trim_rule( entered );
                        changes_made = true;
                    }
                    break;
                }
                case list:
                    rule.whitelist = !rule.whitelist;
                    changes_made = true;
                    break;
                case category:
                    rule.category = rule.category == Categories::SOUND ? Categories::HOSTILE_SPOTTED :
                                    Categories::SOUND;
                    changes_made = true;
                    break;
                case attitude: {
                    std::vector<std::pair<int, std::string>> attitudes;
                    for( const Creature::Attitude a : {
                             Creature::Attitude::HOSTILE, Creature::Attitude::NEUTRAL,
                             Creature::Attitude::FRIENDLY, Creature::Attitude::ANY
                         } ) {
                        attitudes.emplace_back( static_cast<int>( a ),
                                                Creature::get_attitude_ui_data( a ).first.translated() );
                    }
                    const int picked = choose( _( "Attitude" ), attitudes );
                    if( picked >= 0 ) {
                        rule.attitude = static_cast<Creature::Attitude>( picked );
                        changes_made = true;
                    }
                    break;
                }
                case distance: {
                    string_input_popup ask;
                    const std::string entered = ask
                                                .title( _( "Proximity Distance (0=max view distance)" ) )
                                                .width( 4 )
                                                .text( std::to_string( rule.proximity ) )
                                                .description( _( "Option: " ) + std::to_string( get_option<int>( "SAFEMODEPROXIMITY" ) ) +
                                                        " " + get_options().get_option( "SAFEMODEPROXIMITY" ).getDefaultText() )
                                                .max_length( 3 )
                                                .only_digits( true )
                                                .query_string();
                    if( ask.canceled() ) {
                        break;
                    }
                    if( entered.empty() ) {
                        rule.proximity = get_option<int>( "SAFEMODEPROXIMITY" );
                    } else {
                        //Let the options class handle the validity of the new value
                        options_manager::cOpt temp_option = get_options().get_option( "SAFEMODEPROXIMITY" );
                        temp_option.setValue( entered );
                        rule.proximity = temp_option.value_as<int>();
                    }
                    changes_made = true;
                    break;
                }
                case mode: {
                    std::vector<std::pair<int, std::string>> modes;
                    for( const MovementModes m : {
                             MovementModes::WALKING, MovementModes::DRIVING, MovementModes::BOTH
                         } ) {
                        modes.emplace_back( static_cast<int>( m ), mode_name( m ) );
                    }
                    const int picked = choose( _( "Mode" ), modes );
                    if( picked >= 0 ) {
                        rule.movement_mode = static_cast<MovementModes>( picked );
                        changes_made = true;
                    }
                    break;
                }
                default:
                    break;
            }
        } else if( did.action == 1 ) {
            rule.active = !rule.active;
            changes_made = true;
        } else {
            std::vector<std::pair<int, std::string>> choices;
            choices.emplace_back( more_up, _( "Move up" ) );
            choices.emplace_back( more_down, _( "Move down" ) );
            choices.emplace_back( more_copy, _( "Copy" ) );
            if( is_safemode_in && lists.size() == 2 ) {
                choices.emplace_back( more_swap, string_format( _( "Move to the list \"%s\"" ),
                                      titles[( t + 1 ) % 2] ) );
            }
            choices.emplace_back( more_test, _( "Test: show what it matches" ) );
            choices.emplace_back( more_remove, _( "Remove" ) );
            const int choice = choose( rule.rule, choices );
            if( choice == more_test ) {
                test_pattern( t == 0 ? GLOBAL_TAB : CHARACTER_TAB, static_cast<int>( line ) );
            } else if( common_choice( choice, lists, t, line ) ) {
                changes_made = true;
                tab = static_cast<int>( t );
                selected = choice == more_remove ? -1 : row_of( lists, t, line );
            }
        }
    }

    if( !changes_made ) {
        return;
    }
    if( query_yn( _( "Save changes?" ) ) ) {
        if( is_safemode_in ) {
            save_global();
            if( has_character ) {
                save_character();
            }
        } else {
            create_rules();
        }
    } else {
        global_rules = global_rules_old;
        character_rules = character_rules_old;
    }
}
