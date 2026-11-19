#include "mobile_controls.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>

#include "input.h"
#include "input_context.h"
#include "localized_comparator.h"
#include "mobile_list.h"
#include "output.h"
#include "mobile_prompt.h"
#include "popup.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

namespace mobile_controls
{

// Turns upstream's short key names into words: "ALT_X" is "L1 + X", the left
// bumper being the modifier.
std::string button_name( const int code )
{
    std::string key = inp_mngr.get_keyname( code, input_event_t::gamepad, true );
    std::string prefix;
    if( key.rfind( "ALT_", 0 ) == 0 ) {
        prefix = "L1 + ";
        key = key.substr( 4 );
    }
    static const std::map<std::string, std::string> names = {
        { "LB", "L1" }, { "RB", "R1" }, { "LT", "L2" }, { "RT", "R2" },
        { "LS", translate_marker( "Left stick click" ) }, { "RS", translate_marker( "Right stick click" ) },
        { "UP", translate_marker( "D-pad up" ) }, { "DOWN", translate_marker( "D-pad down" ) },
        { "LEFT", translate_marker( "D-pad left" ) }, { "RIGHT", translate_marker( "D-pad right" ) },
        { "START", translate_marker( "Start" ) }, { "BACK", translate_marker( "Select" ) },
    };
    const auto found = names.find( key );
    if( found != names.end() ) {
        return prefix + _( found->second );
    }
    // sticks and their rings: LS_UP_RIGHT, R_RADIAL_NE
    static const std::map<std::string, std::string> stems = {
        { "LS_", translate_marker( "Left stick" ) }, { "RS_", translate_marker( "Right stick" ) },
        { "L_RADIAL_", translate_marker( "Left stick ring" ) },
        { "R_RADIAL_", translate_marker( "Right stick ring" ) },
    };
    for( const std::pair<const std::string, std::string> &stem : stems ) {
        if( key.rfind( stem.first, 0 ) == 0 ) {
            std::string way = to_lower_case( key.substr( stem.first.size() ) );
            std::replace( way.begin(), way.end(), '_', '-' );
            // the rings name their eight directions by compass point
            static const std::map<std::string, std::string> compass = {
                { "n", translate_marker( "up" ) }, { "ne", translate_marker( "up-right" ) },
                { "e", translate_marker( "right" ) }, { "se", translate_marker( "down-right" ) },
                { "s", translate_marker( "down" ) }, { "sw", translate_marker( "down-left" ) },
                { "w", translate_marker( "left" ) }, { "nw", translate_marker( "up-left" ) },
                { "up", translate_marker( "up" ) }, { "down", translate_marker( "down" ) },
                { "left", translate_marker( "left" ) }, { "right", translate_marker( "right" ) },
                { "up-right", translate_marker( "up-right" ) }, { "down-right", translate_marker( "down-right" ) },
                { "up-left", translate_marker( "up-left" ) }, { "down-left", translate_marker( "down-left" ) },
            };
            const auto named = compass.find( way );
            return prefix + string_format( "%s %s", _( stem.second ),
                                           named != compass.end() ? _( named->second ) : way );
        }
    }
    return prefix + key;
}

static std::string buttons_text( const std::vector<int> &codes )
{
    std::string text;
    for( const int code : codes ) {
        text += ( text.empty() ? "" : ", " ) + button_name( code );
    }
    return text;
}

// "ADVANCED_INVENTORY" as "Advanced inventory".
static std::string context_name( const std::string &category )
{
    if( category == "DEFAULTMODE" ) {
        return _( "Play screen" );
    }
    if( category == "default" ) {
        return _( "Everywhere (menus and lists)" );
    }
    std::string name = to_lower_case( category );
    std::replace( name.begin(), name.end(), '_', ' ' );
    if( !name.empty() ) {
        name[0] = static_cast<char>( std::toupper( static_cast<unsigned char>( name[0] ) ) );
    }
    return name;
}

#if defined(TILES)

// Waits for a button of the controller.  Nothing for six seconds, a tap or a
// key gives up.
static std::optional<int> wait_for_button( const std::string &for_what )
{
    input_context ctxt( "default", keyboard_mode::keycode );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "QUIT" );
    ctxt.set_timeout( 100 );
    static_popup note;
    note.message(
        _( "%s\n\nPress the button on the controller now.  Hold L1 with it for an L1 combination.  Tap the screen to cancel." ),
        for_what );
    const auto started = std::chrono::steady_clock::now();
    while( std::chrono::steady_clock::now() - started < std::chrono::seconds( 6 ) ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        const input_event event = ctxt.get_raw_input();
        if( event.type == input_event_t::gamepad && !event.sequence.empty() ) {
            return event.get_first_input();
        }
        if( action != "TIMEOUT" && action != "ANY_INPUT" ) {
            break;
        }
        if( action == "ANY_INPUT" && event.type != input_event_t::timeout &&
            event.type != input_event_t::error ) {
            // a key or a click
            break;
        }
    }
    return std::nullopt;
}

// The actions of one context.
static void show_context( const std::string &category )
{
    int tab = 0;
    int selected = -1;
    std::string notice;
    while( true ) {
        std::vector<binding> all = bindings();
        all.erase( std::remove_if( all.begin(), all.end(), [&category]( const binding & b ) {
            return b.category != category;
        } ), all.end() );
        // what has a button first, then by name
        std::stable_sort( all.begin(), all.end(), []( const binding & a, const binding & b ) {
            if( a.buttons.empty() != b.buttons.empty() ) {
                return b.buttons.empty();
            }
            return localized_compare( a.name, b.name );
        } );
        mobile_list::spec what;
        what.title = context_name( category );
        what.header = notice.empty() ? std::string(
                          _( "Pick an action, then Set and press its button.  Add gives it a second one." ) ) : notice;
        for( const binding &b : all ) {
            mobile_list::row r;
            r.name = b.name;
            r.right = b.buttons.empty() ? std::string( "–" ) : buttons_text( b.buttons );
            r.enabled = !b.buttons.empty();
            r.current = b.buttons != b.defaults;
            r.description = string_format( _( "Now: %s\nBy default: %s" ),
                                           b.buttons.empty() ? _( "no button" ) : buttons_text( b.buttons ),
                                           b.defaults.empty() ? _( "no button" ) : buttons_text( b.defaults ) );
            r.action = _( "Set" );
            r.more_actions = { _( "Add" ), _( "Clear" ), _( "Default" ) };
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= all.size() ) {
            return;
        }
        const binding &b = all[static_cast<size_t>( did.row )];
        std::vector<int> wanted;
        if( did.action == 0 || did.action == 1 ) {
            const std::optional<int> pressed = wait_for_button( b.name );
            if( !pressed ) {
                notice = _( "No button was pressed; nothing changed." );
                continue;
            }
            if( did.action == 1 ) {
                wanted = b.buttons;
            }
            if( std::find( wanted.begin(), wanted.end(), *pressed ) == wanted.end() ) {
                wanted.push_back( *pressed );
            }
        } else if( did.action == 3 ) {
            wanted = b.defaults;
        }
        const std::string action_id = b.action;
        const std::string action_name = b.name;
        const std::vector<std::string> lost = set_buttons( category, action_id, wanted );
        save();
        notice = wanted.empty() ? string_format( _( "%s has no button now." ), action_name ) :
                 string_format( _( "%1$s: %2$s." ), action_name, buttons_text( wanted ) );
        if( !lost.empty() ) {
            notice += "  " + string_format( _( "Taken from: %s." ), enumerate_as_string( lost ) );
        }
        // the list is sorted again; stay on the action that was changed
        std::vector<binding> after = bindings();
        after.erase( std::remove_if( after.begin(), after.end(), [&category]( const binding & o ) {
            return o.category != category;
        } ), after.end() );
        std::stable_sort( after.begin(), after.end(), []( const binding & x, const binding & y ) {
            if( x.buttons.empty() != y.buttons.empty() ) {
                return y.buttons.empty();
            }
            return localized_compare( x.name, y.name );
        } );
        for( size_t i = 0; i < after.size(); ++i ) {
            if( after[i].action == action_id ) {
                selected = static_cast<int>( i );
            }
        }
    }
}

void show()
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        const std::vector<binding> all = bindings();
        // the contexts, the play screen and the menus first
        std::vector<std::string> categories;
        std::map<std::string, std::pair<int, int>> counts;
        for( const binding &b : all ) {
            if( counts.find( b.category ) == counts.end() ) {
                categories.push_back( b.category );
            }
            std::pair<int, int> &count = counts[b.category];
            count.first += b.buttons.empty() ? 0 : 1;
            count.second += b.buttons != b.defaults ? 1 : 0;
        }
        const auto rank = []( const std::string & category ) {
            return category == "DEFAULTMODE" ? 0 : category == "default" ? 1 : 2;
        };
        std::stable_sort( categories.begin(), categories.end(), [&rank]( const std::string & a,
        const std::string & b ) {
            return rank( a ) != rank( b ) ? rank( a ) < rank( b ) : context_name( a ) < context_name( b );
        } );
        mobile_list::spec what;
        what.title = _( "Controller buttons" );
        what.header = _( "Each screen has its own buttons.  Pick one to see and change them." );
        what.empty_text = _( "No screen has controller buttons." );
        for( const std::string &category : categories ) {
            const std::pair<int, int> &count = counts[category];
            mobile_list::row r;
            r.name = context_name( category );
            r.right = string_format( _( "%d set" ), count.first );
            r.current = count.second > 0;
            r.description = count.second > 0 ?
                            string_format( _( "%1$d actions have a button; %2$d differ from the defaults." ), count.first,
                                           count.second ) :
                            string_format( _( "%d actions have a button, all as by default." ), count.first );
            r.action = _( "Open" );
            if( count.second > 0 ) {
                r.more_actions = { _( "All to default" ) };
            }
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= categories.size() ) {
            return;
        }
        const std::string &category = categories[static_cast<size_t>( did.row )];
        if( did.action == 1 ) {
            if( query_yn( _( "Put every controller button of \"%s\" back to its default?" ),
                          context_name( category ) ) ) {
                // defaults that moved between actions do not fight: clear all, then set
                for( const binding &b : all ) {
                    if( b.category == category && b.buttons != b.defaults ) {
                        set_buttons( category, b.action, {} );
                    }
                }
                for( const binding &b : all ) {
                    if( b.category == category && b.buttons != b.defaults ) {
                        set_buttons( category, b.action, b.defaults );
                    }
                }
                save();
            }
            continue;
        }
        show_context( category );
    }
}

#else // !TILES

void show() {}

#endif // TILES

} // namespace mobile_controls
