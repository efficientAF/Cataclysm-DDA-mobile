#include "mobile_options.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cata_imgui.h"
#include "cata_utility.h"
#if defined(__ANDROID__)
#include "cursesport.h"
#endif
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "input_popup.h"
#include "auto_pickup.h"
#include "game.h"
#include "mobile_controls.h"
#include "mobile_hud_edit.h"
#include "mobile_prompt.h"
#include "mobile_settings.h"
#include "mobile_ui.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "popup.h"
#include "safemode_ui.h"
#include "string_formatter.h"
#include "touch_input.h"
#include "translations.h"
#include "ui_manager.h"
#include "uilist.h"

namespace mobile_options
{

using mobile_ui::face;
using cOpt = options_manager::cOpt;

namespace
{

constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_switch_off = IM_COL32( 255, 255, 255, 41 );
constexpr ImU32 col_field = IM_COL32( 8, 10, 7, 255 );
constexpr ImU32 col_lead = IM_COL32( 191, 198, 181, 255 );
constexpr ImU32 col_disabled = IM_COL32( 233, 236, 223, 102 );

enum class entry_kind : int {
    // the name of a page, above its search results
    page,
    // the name of an option group
    group,
    // a paragraph of explanation
    lead,
    // upstream's blank line
    gap,
    option,
    // a row that opens another screen (text is its name, sub what it is, link which)
    link,
};

// How a row of a tab's everyday list differs from upstream's plain option row.
struct tweak {
    enum class as : int {
        plain,
        // the choices side by side
        segment,
        slider,
    };
    std::string title = std::string();
    std::string sub = std::string();
    as kind = as::plain;
    // segment: option value and label; empty takes the option's own choices
    std::vector<std::pair<std::string, std::string>> choices = {};
    // slider: the range, the step and whether the number is shown as a share of hi
    int lo = 0;
    int hi = 0;
    int step = 1;
    bool percent = false;
};

// One line of a page.
struct entry {
    entry_kind kind = entry_kind::gap;
    // the header or the paragraph
    std::string text;
    cOpt *opt = nullptr;
    // only for a link
    std::string sub = std::string();
    std::string link = std::string();
    // a link that cannot be followed now is greyed out
    bool off = false;
    // an option: shown differently from upstream's row
    bool tweaked = false;
    tweak look = tweak();
};

// The rest of a tab's options that belong together, opened from its "More" list.
struct section {
    std::string name;
    std::string sub;
    std::vector<entry> entries;
};

struct page {
    std::string name;
    // what shows at once
    std::vector<entry> entries;
    std::vector<section> more = {};
};

} // namespace

// Reads the pages and groups of the options manager, which it keeps private.
struct access {
    static std::vector<page> pages( options_manager &options, bool ingame, bool world_options_only );
    // the tabs of the Settings screen
    static std::vector<page> tabs( options_manager &options, bool ingame );
};

std::vector<page> access::pages( options_manager &options, const bool ingame,
                                 const bool world_options_only )
{
    options_manager::options_container &world = options.world_options.has_value() ?
            *options.world_options.value() : options.options;
    std::vector<page> ret;
    // Everything about touch, mouse, controller and keyboard is gathered on one
    // page of its own, "Inputs", whichever of upstream's pages it is listed on.
    static const std::set<std::string> input_groups = {
        "mobile_touch_opts", "mouse_cont_opts", "android_keyboard_opts", "joystick_android_opts",
        "shortcut_android_opts", "gestures_android_opts", "shortcut_android_in_game_opts",
        "shortc_screen_opts"
    };
    static const std::set<std::string> input_options = {
        "ENABLE_JOYSTICK", "ENABLE_MOUSE", "HIDE_CURSOR", "EDGE_SCROLL", "MENU_SCROLL",
        "FAST_SCROLL_OFFSET", "SDL_KEYBOARD_MODE", "FORCE_CAPITAL_YN", "DIAG_MOVE_WITH_MODIFIERS_MODE",
        "QUERY_KEYBIND_REMOVAL", "ANDROID_TRAP_BACK_BUTTON", "ANDROID_VIBRATION"
    };
    page inputs;
    inputs.name = _( "Inputs" );
    inputs.entries.push_back( { entry_kind::link, _( "Controller" ), nullptr,
                                _( "A picture of the controller: what each button does when pressed and held, and its radial menus" ),
                                "controller" } );
    inputs.entries.push_back( {} );
    // in three parts, each in upstream's order: touch, mouse and controller, keyboard
    static const std::set<std::string> keyboard_options = {
        "SDL_KEYBOARD_MODE", "FORCE_CAPITAL_YN", "DIAG_MOVE_WITH_MODIFIERS_MODE", "QUERY_KEYBIND_REMOVAL"
    };
    std::array<std::vector<entry>, 3> parts;
    std::array<std::string, 3> part_group;
    const std::array<std::string, 3> part_name = { {
            _( "Touch" ), _( "Mouse and controller" ), _( "Keyboard" )
        }
    };
    size_t inputs_at = 0;
    for( const options_manager::Page &from : options.pages_ ) {
        const bool world_page = from.id_ == "world_default";
        if( world_options_only && !world_page ) {
            continue;
        }
        // the same choice of container as upstream's screen
        options_manager::options_container &container = ( ingame || world_options_only ) &&
                world_page ? world : options.options;
        page to;
        to.name = ingame && world_page ? _( "Current world" ) : from.name_.translated();
        if( ingame && world_page ) {
            to.entries.push_back( { entry_kind::lead,
                                    _( "Note: Some of these options may produce unexpected results if changed." ),
                                    nullptr } );
        }
        bool any = false;
        std::string group;
        for( const options_manager::PageItem &item : from.items_ ) {
            switch( item.type ) {
                case options_manager::ItemType::BlankLine:
                    if( !to.entries.empty() && to.entries.back().kind == entry_kind::option ) {
                        to.entries.push_back( {} );
                    }
                    break;
                case options_manager::ItemType::GroupHeader: {
                    const options_manager::Group &g = options.find_group( item.group );
                    to.entries.push_back( { entry_kind::group, g.name_.translated(), nullptr } );
                    const std::string tip = g.tooltip_.translated();
                    if( !tip.empty() ) {
                        to.entries.push_back( { entry_kind::lead, tip, nullptr } );
                    }
                    group = item.group;
                    break;
                }
                case options_manager::ItemType::Option: {
                    const auto found = container.find( item.data );
                    // what upstream hides stays hidden
                    if( found == container.end() || options.get_option( item.data ).is_hidden() ) {
                        break;
                    }
                    if( !world_options_only && ( input_groups.count( item.group ) > 0 ||
                                                 input_options.count( item.data ) > 0 ) ) {
                        // to the Inputs page.  A whole group of upstream's keeps its
                        // name; single options go under the part's own name.
                        const bool whole_group = input_groups.count( item.group ) > 0;
                        const size_t part = keyboard_options.count( item.data ) > 0 ||
                                            item.group == "android_keyboard_opts" ? 2 :
                                            whole_group && item.group != "mouse_cont_opts" ? 0 : 1;
                        const std::string heading = whole_group && part != 1 ?
                                                    options.find_group( item.group ).name_.translated() : part_name[part];
                        if( heading != part_group[part] ) {
                            part_group[part] = heading;
                            if( !parts[part].empty() ) {
                                parts[part].push_back( {} );
                            }
                            parts[part].push_back( { entry_kind::group, heading, nullptr } );
                        }
                        parts[part].push_back( { entry_kind::option, "", &found->second } );
                        break;
                    }
                    if( item.group != group ) {
                        // back out of a group
                        if( to.entries.empty() || to.entries.back().kind != entry_kind::gap ) {
                            to.entries.push_back( {} );
                        }
                        group = item.group;
                    }
                    to.entries.push_back( { entry_kind::option, "", &found->second } );
                    any = true;
                    break;
                }
            }
        }
        // a group whose options all went to Inputs leaves its heading behind
        while( !to.entries.empty() && to.entries.back().kind != entry_kind::option ) {
            to.entries.pop_back();
        }
        // a page without anything to change gets no chip
        if( any ) {
            ret.push_back( std::move( to ) );
            if( from.id_ == "interface" ) {
                inputs_at = ret.size();
            }
        }
    }
    for( const std::vector<entry> &part : parts ) {
        if( !part.empty() ) {
            inputs.entries.insert( inputs.entries.end(), part.begin(), part.end() );
            inputs.entries.push_back( {} );
        }
    }
    if( !world_options_only ) {
        ret.insert( ret.begin() + static_cast<std::ptrdiff_t>( inputs_at > 0 ? inputs_at : ret.size() ),
                    std::move( inputs ) );
    }
    return ret;
}

namespace
{

// The everyday rows that are worded or drawn differently from upstream's.
std::map<std::string, tweak> everyday_looks()
{
    using as = tweak::as;
    std::map<std::string, tweak> looks;
    const auto plain = [&looks]( const std::string & option, const std::string & title,
    const std::string & sub ) {
        tweak t;
        t.title = title;
        t.sub = sub;
        looks[option] = t;
    };
    const auto segment = [&looks]( const std::string & option, const std::string & title,
                                   const std::string & sub,
    const std::vector<std::pair<std::string, std::string>> &choices ) {
        tweak t;
        t.title = title;
        t.sub = sub;
        t.kind = as::segment;
        t.choices = choices;
        looks[option] = t;
    };
    const auto slider = [&looks]( const std::string & option, const std::string & title,
    const std::string & sub, const int lo, const int hi, const int step, const bool percent ) {
        tweak t;
        t.title = title;
        t.sub = sub;
        t.kind = as::slider;
        t.lo = lo;
        t.hi = hi;
        t.step = step;
        t.percent = percent;
        looks[option] = t;
    };
    // Input
    plain( "MOBILE_PAD", _( "Radial menus" ),
           _( "Hold a button and push a stick.  Off goes back to the game's own controller scheme" ) );
    slider( "MOBILE_PAD_HOLD_MS", _( "Hold time" ),
            _( "How long a button is held before its long press or its menus" ), 150, 500, 10, false );
    slider( "MOBILE_PAD_FLICK_MS", _( "Flick time" ),
            _( "How fast the stick must snap back to run a menu slot" ), 50, 400, 10, false );
    segment( "TOUCH_TAP_TO_ACT", _( "Tap on the map" ), _( "What a single tap next to you does" ), {
        { "true", _( "Smart action" ) }, { "false", _( "Tap key" ) }
    } );
    plain( "TOUCH_LONG_PRESS_MENU", _( "Long-press opens the action menu" ),
           _( "Hold a finger still on a map tile to choose what to do there" ) );
    slider( "TOUCH_LONG_PRESS_DELAY", _( "Long-press time" ),
            _( "How long to hold before the action menu opens" ), 300, 1500, 10, false );
    plain( "TOUCH_TWO_FINGER_GESTURES", _( "Pinch and two-finger drag" ),
           _( "Pinch to zoom and drag with two fingers to pan, as well as the Zoom and Pan controls" ) );
    plain( "TOUCH_ZOOM_NO_WRAP", _( "Zoom stops at the ends" ),
           _( "Off jumps from the closest zoom level to the furthest" ) );
    plain( "TOUCH_RECENTER_ON_MOVE", _( "Recenter when you move" ),
           _( "Taking a step brings the view back to you" ) );
    segment( "TOUCH_JOYSTICK_MODE", _( "Movement joystick" ),
    _( "Fixed is a pad on the HUD with a button to show and hide it; Floating starts where you drag on the map" ), {
        { "fixed", _( "Fixed" ) }, { "floating", _( "Floating" ) }
    } );
    plain( "ANDROID_SHOW_VIRTUAL_JOYSTICK", _( "Show the floating joystick" ),
           _( "Draws the Floating joystick under your finger while you drag" ) );
    plain( "TOUCH_MOUSE_AS_FINGER", _( "Mouse clicks count as touches" ),
           _( "For screens that report touches as a mouse, like the Steam Deck in Gaming Mode" ) );
    plain( "ANDROID_AUTO_KEYBOARD", _( "Show keyboard automatically" ),
           _( "Only when a screen needs typing, like search or naming" ) );
    plain( "TOUCH_MENU_TAP", _( "Tap items in text menus" ),
           _( "A tap in a classic menu picks the item under your finger at once" ) );
    // Display
    segment( "ANDROID_TEXT_SIZE", _( "Text size" ), "", {
        { "auto", _( "Auto" ) }, { "8x16", _( "S" ) }, { "10x20", _( "M" ) }, { "12x24", _( "L" ) },
        { "14x28", _( "XL" ) }
    } );
    plain( "MOBILE_MENUS", _( "Touch screens" ), _( "Off brings back the classic text screens" ) );
    segment( "USE_TILES", _( "Map graphics" ),
    _( "Tiles draws the map with pictures; ASCII draws it with letters and cannot zoom" ), {
        { "true", _( "Tiles" ) }, { "false", _( "ASCII" ) }
    } );
    plain( "TILES", _( "Tileset" ),
           _( "How the map is drawn.  Names ending in iso are drawn at an angle" ) );
    plain( "MOBILE_HUD", _( "Full-screen map" ),
           _( "Hides the sidebar and draws status and messages over the map" ) );
    // Sound
    plain( "SOUND_ENABLED", _( "Sound" ), _( "Music and sound effects" ) );
    plain( "SOUNDPACKS", _( "Sound pack" ), _( "A new pack is used after the game restarts" ) );
    slider( "MUSIC_VOLUME", _( "Music" ), "", 0, 128, 1, true );
    slider( "SOUND_EFFECT_VOLUME", _( "Sound effects" ), "", 0, 128, 1, true );
    slider( "AMBIENT_SOUND_VOLUME", _( "Ambient sounds" ), _( "Weather, fire, machines" ), 0, 128, 1,
            true );
    // Gameplay
    plain( "AUTOSAVE", _( "Autosave" ), _( "Saves the game every few minutes while you play" ) );
    plain( "ANDROID_QUICKSAVE", _( "Save when the app goes to the background" ), "" );
    plain( "AUTOSAFEMODE", _( "Safe mode turns itself back on" ),
           _( "Safe mode stops you and warns when a monster comes into view" ) );
    plain( "AUTO_PICKUP", _( "Auto pickup" ), _( "Pick up matching items as you walk over them" ) );
    // Interface: the choices side by side where they fit
    for( const char *const unit : {
             "USE_CELSIUS", "USE_METRIC_SPEEDS", "USE_METRIC_WEIGHTS", "VOLUME_UNITS", "DISTANCE_UNITS",
             "24_HOUR"
         } ) {
        tweak t;
        t.kind = as::segment;
        looks[unit] = t;
    }
    return looks;
}

// A row of a tab that opens another screen; kind gap if it has no place here.
entry link_entry( const std::string &id )
{
    entry e;
    e.kind = entry_kind::link;
    e.link = id;
    if( id == "controller" ) {
        e.text = _( "Controller buttons" );
        e.sub = _( "A picture of the controller: buttons, sticks and radial menus, for every screen" );
    } else if( id == "keys" ) {
#if defined(__ANDROID__)
        // a phone has no keys to assign
        e.kind = entry_kind::gap;
#endif
        e.text = _( "Keyboard keys" );
        e.sub = _( "Which key does what, for every screen" );
    } else if( id == "screen_sizes" ) {
        e.text = _( "Screen sizes" );
        e.sub = _( "Make each kind of screen smaller or larger" );
    } else if( id == "edit_hud" ) {
        e.text = _( "HUD layout" );
        e.off = !mobile_hud_edit::available();
        e.sub = e.off ? _( "Open Settings from a game to change the layout" ) :
                _( "Move, resize and hide the pieces on the play screen" );
    } else if( id == "safemode_rules" ) {
        e.text = _( "Safe mode rules" );
        e.sub = _( "What stops you and what does not" );
    } else if( id == "autopickup_rules" ) {
        e.text = _( "Auto pickup rules" );
        e.sub = _( "Which items are picked up" );
    }
    return e;
}

} // namespace

std::vector<page> access::tabs( options_manager &options, const bool ingame )
{
    options_manager::options_container &world = options.world_options.has_value() ?
            *options.world_options.value() : options.options;
    // every option upstream shows, in its order
    struct known {
        cOpt *opt;
        std::string name;
        std::string group;
        std::string page;
        bool used;
    };
    std::vector<known> all;
    for( const options_manager::Page &from : options.pages_ ) {
        // the same choice of container as upstream's screen
        options_manager::options_container &container = ingame &&
                from.id_ == "world_default" ? world : options.options;
        for( const options_manager::PageItem &item : from.items_ ) {
            if( item.type != options_manager::ItemType::Option ) {
                continue;
            }
            const auto found = container.find( item.data );
            // what upstream hides stays hidden
            if( found == container.end() || options.get_option( item.data ).is_hidden() ) {
                continue;
            }
            all.push_back( { &found->second, item.data, item.group, from.id_, false } );
        }
    }
    // "*group" is what is left of one of upstream's groups, "%page" of a page,
    // "!" of everything; anything else names one option
    const auto take = [&all]( const std::string & what ) {
        std::vector<cOpt *> ret;
        for( known &k : all ) {
            const bool wanted = what == "!" ||
                                ( what[0] == '*' ? k.group == what.substr( 1 ) :
                                  what[0] == '%' ? k.page == what.substr( 1 ) : k.name == what );
            if( wanted && !k.used ) {
                k.used = true;
                ret.push_back( k.opt );
            }
        }
        return ret;
    };
    const std::map<std::string, tweak> looks = everyday_looks();

    struct more_plan {
        std::string name;
        std::string sub;
        std::vector<std::string> what;
    };
    struct tab_plan {
        std::string name;
        // "#" starts a heading, "@" a link, "?" a paragraph
        std::vector<std::string> everyday;
        std::vector<more_plan> more;
    };
    const std::string classic = _( "Only used while Touch screens is off" );
    const std::vector<tab_plan> plan = {
        {
            _( "Input" ), {
                std::string( "#" ) + _( "Buttons and keys" ), "@controller", "@keys",
                std::string( "#" ) + _( "Controller" ), "MOBILE_PAD", "MOBILE_PAD_HOLD_MS", "MOBILE_PAD_FLICK_MS",
                std::string( "#" ) + _( "Touch on the map" ), "TOUCH_TAP_TO_ACT", "TOUCH_LONG_PRESS_MENU",
                "TOUCH_LONG_PRESS_DELAY", "TOUCH_TWO_FINGER_GESTURES", "TOUCH_ZOOM_NO_WRAP",
                "TOUCH_RECENTER_ON_MOVE", "TOUCH_JOYSTICK_MODE", "ANDROID_SHOW_VIRTUAL_JOYSTICK",
                "ANDROID_VIBRATION",
                std::string( "#" ) + _( "Touch in menus" ), "TOUCH_MOUSE_AS_FINGER", "ANDROID_AUTO_KEYBOARD",
                "TOUCH_MENU_TAP"
            }, {
                { _( "Mouse and controller" ), _( "Edge scrolling, cursor, scroll speed" ), { "*mouse_cont_opts", "MENU_SCROLL", "FAST_SCROLL_OFFSET", "*mobile_touch_opts" } },
                { _( "Keyboard" ), _( "Keyboard mode, capital Y/N, diagonal keys" ), { "SDL_KEYBOARD_MODE", "FORCE_CAPITAL_YN", "DIAG_MOVE_WITH_MODIFIERS_MODE", "QUERY_KEYBIND_REMOVAL", "*android_keyboard_opts" } },
                { _( "Floating joystick" ), _( "Size, dead zone, opacity, repeat delay" ), { "*joystick_android_opts" } },
                { _( "Classic shortcut bar" ), classic, { "*shortcut_android_opts", "*shortcut_android_in_game_opts", "*shortc_screen_opts" } },
                { _( "Classic gestures" ), classic, { "*gestures_android_opts" } }
            }
        },
        {
            _( "Display" ), {
                std::string( "#" ) + _( "Screens" ), "ANDROID_TEXT_SIZE", "@screen_sizes", "MOBILE_MENUS",
                std::string( "#" ) + _( "Map" ), "USE_TILES", "TILES", "MOBILE_HUD", "@edit_hud"
            }, {
                { _( "Animation" ), _( "Animations, rain, projectiles, delay" ), { "*anim_opts" } },
                { _( "Tileset options" ), _( "Distant and overmap tilesets, overlay icons" ), { "*tileset_opts" } },
                { _( "Colour overlays" ), _( "Night vision, lighting, map memory" ), { "*color_ovl_opts" } },
                { _( "Pixel minimap" ), _( "Size, colours, beacons" ), { "*pix_minimap_opts" } },
                { _( "Fonts" ), _( "Font blending and sizes" ), { "*font_params" } },
                { _( "ASCII graphics" ), _( "Title screen and ASCII art" ), { "*ascii_opts", "ENABLE_ASCII_ART" } },
                { _( "Window and renderer" ), _( "Display, fullscreen, renderer, scaling" ), { "*graph_display_opts" } },
                { _( "Terminal size" ), _( "Columns and rows of the text grid" ), { "*term_opts" } },
                { _( "Android display" ), _( "System bars, orientation, insets" ), { "*android_display_opts" } }
            }
        },
        {
            _( "Sound" ), {
                std::string( "#" ) + _( "Sound" ), "SOUND_ENABLED", "SOUNDPACKS",
                std::string( "#" ) + _( "Volume" ), "MUSIC_VOLUME", "SOUND_EFFECT_VOLUME", "AMBIENT_SOUND_VOLUME",
                "*soundpacks_opts"
            }, {}
        },
        {
            _( "Gameplay" ), {
                std::string( "#" ) + _( "Saving" ), "AUTOSAVE", "ANDROID_QUICKSAVE",
                std::string( "#" ) + _( "Safe mode" ), "AUTOSAFEMODE", "@safemode_rules",
                std::string( "#" ) + _( "Picking up" ), "AUTO_PICKUP", "@autopickup_rules"
            }, {
                { _( "Auto pickup" ), _( "Limits, safe mode, adjacent tiles" ), { "*auto_pick_opts" } },
                { _( "Auto features" ), _( "Mining, foraging, pulping, mopping" ), { "*auto_feat_opts" } },
                { _( "Player safety" ), _( "Dangerous terrain, engine controller" ), { "*player_safe_opts" } },
                { _( "Safe mode" ), _( "Distance, turns until it returns" ), { "*safe_mode_opts" } },
                { _( "Autosave" ), _( "Turns and minutes between saves" ), { "*auto_save_opts" } },
                { _( "Auto notes" ), _( "Stairs, map extras, dropped favourites" ), { "*auto_note_opts" } },
                { _( "Other" ), _( "Default name, turn length, death camera and the rest" ), { "*misc_general_opts", "%general" } }
            }
        },
        {
            _( "Interface" ), {
                std::string( "#" ) + _( "Units" ), "*measurement_unit",
                std::string( "#" ) + _( "Language" ), "USE_LANG"
            }, {
                { _( "Messages in the log" ), _( "Items on the ground, monster movement" ), { "*additional_messages_in_log" } },
                { _( "Naming" ), _( "Brand names, ammo in names, containers" ), { "*naming_opts" } },
                { _( "Accessibility" ), _( "Screen reader mode, questions, highlights" ), { "*accessibility" } },
                { _( "Vehicles" ), _( "Driving view, direction indicator, steering" ), { "*veh_intf_opts" } },
                { _( "Quality of life" ), _( "View offset, item symbols, body graph" ), { "*qol_opts", "ITEM_HEALTH" } },
                { _( "Inventory" ), _( "Space shown in the inventory" ), { "*inventory_stats_opts" } },
                { _( "Sidebar and messages" ), _( "The classic sidebar, the message log" ), { "*sidb_opts", "%interface" } }
            }
        },
        {
            ingame ? _( "This world" ) : _( "World" ), {
                ingame ? std::string( "?" ) + _( "Note: Some of these options may produce unexpected results if changed." ) :
                std::string( "?" ) + _( "These are used for new worlds." ),
                "%world_default"
            }, {}
        },
        {
            _( "Advanced" ), {
                std::string( "#" ) + _( "Loading" ), "WARN_ON_MODIFIED", "SKIP_VERIFICATION",
                std::string( "#" ) + _( "Debug" ), "DEBUG_DIFFICULTIES"
            }, {
                { _( "Tall sprites" ), _( "How sprites that hide what is behind them are handled" ), { "*occlusion_opts" } },
                { _( "Everything else" ), _( "Options that have no tab of their own yet" ), { "!" } }
            }
        }
    };

    std::vector<page> ret;
    // the everyday rows first, so that a "More" group only gets what is left
    for( const tab_plan &tab : plan ) {
        page to;
        to.name = tab.name;
        for( const std::string &what : tab.everyday ) {
            if( what[0] == '#' ) {
                // a heading without rows is taken away again
                if( !to.entries.empty() && to.entries.back().kind == entry_kind::group ) {
                    to.entries.pop_back();
                }
                to.entries.push_back( { entry_kind::group, what.substr( 1 ), nullptr } );
            } else if( what[0] == '?' ) {
                to.entries.push_back( { entry_kind::lead, what.substr( 1 ), nullptr } );
            } else if( what[0] == '@' ) {
                entry e = link_entry( what.substr( 1 ) );
                if( e.kind == entry_kind::link ) {
                    to.entries.push_back( std::move( e ) );
                }
            } else {
                for( cOpt *const opt : take( what ) ) {
                    entry e;
                    e.kind = entry_kind::option;
                    e.opt = opt;
                    const auto look = looks.find( opt->getName() );
                    if( look != looks.end() ) {
                        e.tweaked = true;
                        e.look = look->second;
                    }
                    to.entries.push_back( std::move( e ) );
                }
            }
        }
        while( !to.entries.empty() && to.entries.back().kind != entry_kind::option &&
               to.entries.back().kind != entry_kind::link ) {
            to.entries.pop_back();
        }
        ret.push_back( std::move( to ) );
    }
    for( size_t i = 0; i < plan.size(); ++i ) {
        for( const more_plan &more : plan[i].more ) {
            section to;
            to.name = more.name;
            to.sub = more.sub;
            for( const std::string &what : more.what ) {
                for( cOpt *const opt : take( what ) ) {
                    to.entries.push_back( { entry_kind::option, "", opt } );
                }
            }
            if( !to.entries.empty() ) {
                ret[i].more.push_back( std::move( to ) );
            }
        }
    }
    // a tab without anything to change gets no chip
    ret.erase( std::remove_if( ret.begin(), ret.end(), []( const page & p ) {
        return std::none_of( p.entries.begin(), p.entries.end(), []( const entry & e ) {
            return e.kind == entry_kind::option || e.kind == entry_kind::link;
        } ) &&p.more.empty();
    } ), ret.end() );
    return ret;
}

namespace
{

enum class control : int {
    toggle,
    stepper,
    // a value and a chevron; a tap opens a sheet, the keypad or the text card
    choice,
    // the choices side by side
    segment,
    slider,
};

// One line of the list as it is drawn, with its text and height worked out.
struct row {
    entry_kind kind = entry_kind::gap;
    cOpt *opt = nullptr;
    std::string title;
    std::string sub;
    // for a link row: which screen it opens
    std::string link;
    control ctl = control::choice;
    std::string value;
    // false while the option's prerequisite is not met
    bool enabled = true;
    // stepper: whether there is room below and above
    bool can_dec = false;
    bool can_inc = false;
    float control_w = 0.0f;
    float text_w = 0.0f;
    float height = 0.0f;
    // a link that cannot be followed now
    bool off = false;
    // segment: option value, label and width of each choice.  slider: its range.
    tweak look = tweak();
    std::vector<float> seg_widths = {};
};

bool is_number( const cOpt &opt )
{
    return opt.getType() == "int" || opt.getType() == "float";
}

double number_of( const cOpt &opt )
{
    return opt.getType() == "int" ? static_cast<double>( opt.value_as<int>() ) :
           static_cast<double>( opt.value_as<float>() );
}

// The values an int_map option goes through, in upstream's order, starting
// with the current one.  The list itself is private to the option.
std::vector<std::string> int_map_names( const cOpt &opt )
{
    std::vector<std::string> names;
    cOpt copy = opt;
    do {
        names.push_back( copy.getValueName() );
        copy.setNext();
    } while( copy != opt && names.size() < 1000 );
    return names;
}

class options_window : public cataimgui::window
{
    public:
        options_window( std::vector<page> pages, const bool world_options_only, const bool with_tabs )
            : cataimgui::window( "MOBILE_OPTIONS",
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoBackground ),
              pages( std::move( pages ) ), world_options_only( world_options_only ),
              with_tabs( with_tabs ) {}

        // the screen was left; result says how
        bool done = false;
        // a link row was tapped: which screen to open
        std::string open_link;
        std::string result;
        // an option whose row was tapped and that needs a sheet or a popup
        cOpt *asked = nullptr;
        // the list has to be worked out again
        bool dirty = true;

        // One step back: out of the search first.  False if the screen should close.
        bool back();
        void scroll_chips( int direction );
        bool typing() const {
            return search_active;
        }
        // "Edit HUD" closes the screen like Back does
        void leave() {
            done = true;
            result = world_options_only ? "QUIT" : "";
        }

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        std::vector<page> pages;
        bool world_options_only;
        bool with_tabs;
        size_t current = 0;
        // which of the tab's "More" groups is open, or -1 for its everyday rows
        int open_section = -1;
        // the "More" groups have a column of their own (a wide screen)
        bool side = false;
        std::string query;
        std::string applied_query;
        bool search_active = false;
        bool drop_focus = false;
        bool to_top = false;
        float chip_scroll_by = 0.0f;
        std::vector<row> rows;
        float rows_width = 0.0f;
        // the tracks of the sliders drawn this frame: x, y, width, height
        std::vector<ImVec4> slider_rows;

        void rebuild( float width );
        void add_rows( const std::vector<entry> &from, float width );
        void draw_more( const ImVec2 &pos, const ImVec2 &size );
        void draw_search( const ImVec2 &pos, float width );
        void draw_chips( const ImVec2 &pos, float width );
        void draw_list( const ImVec2 &pos, const ImVec2 &size );
        void draw_row( ImDrawList *draw, row &r, const ImVec2 &pos, float width );
        bool step_button( ImDrawList *draw, const std::string &id, const char *icon_name,
                          const ImVec2 &pos, float row_h, bool enabled );
        bool foot_button( const std::string &id, const std::string &label, const ImVec2 &pos,
                          const ImVec2 &size, bool primary );
};

bool options_window::back()
{
    if( !query.empty() || search_active ) {
        query.clear();
        drop_focus = true;
        return true;
    }
    if( open_section >= 0 ) {
        open_section = -1;
        dirty = true;
        to_top = true;
        return true;
    }
    return false;
}

void options_window::scroll_chips( const int direction )
{
    chip_scroll_by += static_cast<float>( direction ) * 220.0f * mobile_ui::unit();
}

void options_window::add_rows( const std::vector<entry> &from, const float width )
{
    const float u = mobile_ui::unit();
    const bool searching = !query.empty();
    for( const entry &e : from ) {
        row r;
        r.kind = e.kind;
        if( e.kind != entry_kind::option ) {
            // search results are a plain list of options
            if( searching ) {
                continue;
            }
            // no gap at the top, and never two in a row
            if( e.kind == entry_kind::gap && ( rows.empty() || rows.back().kind != entry_kind::option ) ) {
                continue;
            }
            r.title = e.text;
            if( e.kind == entry_kind::link ) {
                r.sub = e.sub;
                r.link = e.link;
                r.off = e.off;
                r.text_w = width - 32.0f * u - 30.0f * u;
                r.height = std::max( 60.0f * u, 24.0f * u +
                                     mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w ).y + 2.0f * u +
                                     mobile_ui::text_size( face::body, 12.0f, r.sub, r.text_w ).y );
            } else if( e.kind == entry_kind::group ) {
                r.height = 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, r.title ).y + 6.0f * u;
            } else if( e.kind == entry_kind::lead ) {
                r.text_w = width - 32.0f * u;
                r.height = 8.0f * u + mobile_ui::text_size( face::body, 13.0f, r.title, r.text_w ).y +
                           6.0f * u;
            } else {
                r.height = 14.0f * u;
            }
            rows.push_back( std::move( r ) );
            continue;
        }
        cOpt &opt = *e.opt;
        r.opt = &opt;
        r.title = remove_color_tags( opt.getMenuText() );
        r.sub = remove_color_tags( opt.getTooltip() );
        // upstream's words are searched as well as the row's own
        if( searching && !lcmatch( r.title, query ) && !lcmatch( r.sub, query ) &&
            !( e.tweaked && ( lcmatch( e.look.title, query ) || lcmatch( e.look.sub, query ) ) ) ) {
            continue;
        }
        const std::string type = opt.getType();
        if( e.tweaked && !e.look.title.empty() ) {
            r.title = e.look.title;
            r.sub = e.look.sub;
        } else if( type != "string_select" &&
                   !( type == "string_input" && opt.getDefaultValue().empty() ) ) {
            // the default, and the range of a number; a choice shows its values in the sheet
            if( !r.sub.empty() ) {
                r.sub += "  ";
            }
            r.sub += remove_color_tags( opt.getDefaultText() );
        }
        r.enabled = !opt.hasPrerequisite() || opt.checkPrerequisite();
        r.value = opt.getValueName();
        const float room = width - 32.0f * u;
        if( e.tweaked && e.look.kind == tweak::as::segment ) {
            r.look = e.look;
            if( r.look.choices.empty() && type == "string_select" ) {
                for( const options_manager::id_and_option &item : opt.getItems() ) {
                    r.look.choices.emplace_back( item.first, item.second.translated() );
                }
            }
            r.control_w = 4.0f * u;
            for( const std::pair<std::string, std::string> &choice : r.look.choices ) {
                r.seg_widths.push_back( mobile_ui::text_size( face::bold, 12.5f, choice.second ).x + 20.0f * u );
                r.control_w += r.seg_widths.back();
            }
        }
        if( !r.seg_widths.empty() && r.control_w <= room * 0.62f ) {
            r.ctl = control::segment;
        } else if( e.tweaked && e.look.kind == tweak::as::slider && type == "int" ) {
            r.ctl = control::slider;
            r.look = e.look;
            r.control_w = std::max( 150.0f * u, room * 0.44f );
        } else if( type == "bool" ) {
            r.ctl = control::toggle;
            r.control_w = 50.0f * u;
        } else if( is_number( opt ) ) {
            r.ctl = control::stepper;
            // upstream's steps wrap around at the ends; the buttons stop there
            const double now = number_of( opt );
            cOpt next = opt;
            next.setNext();
            r.can_inc = number_of( next ) > now;
            cOpt prev = opt;
            prev.setPrev();
            r.can_dec = number_of( prev ) < now;
            r.control_w = 88.0f * u + std::max( 52.0f * u,
                                                mobile_ui::text_size( face::mono, 13.0f, r.value ).x + 12.0f * u );
        } else {
            r.ctl = control::choice;
            if( r.value.empty() ) {
                //~ Shown in place of an option's value when it is empty
                r.value = _( "(empty)" );
            }
            r.control_w = std::min( mobile_ui::text_size( face::bold, 13.0f, r.value ).x,
                                    std::max( 90.0f * u, ( width - 32.0f * u ) * 0.36f ) ) + 24.0f * u;
        }
        r.text_w = std::max( 80.0f * u, width - 32.0f * u - 12.0f * u - r.control_w );
        const float title_h = mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w ).y;
        const float sub_h = r.sub.empty() ? 0.0f : 2.0f * u +
                            mobile_ui::text_size( face::body, 12.0f, r.sub, r.text_w ).y;
        r.height = std::max( 60.0f * u, title_h + sub_h + 24.0f * u );
        rows.push_back( std::move( r ) );
    }
}

void options_window::rebuild( const float width )
{
    dirty = false;
    rows_width = width;
    if( query != applied_query ) {
        applied_query = query;
        to_top = true;
    }
    rows.clear();
    if( pages.empty() ) {
        return;
    }
    current = std::min( current, pages.size() - 1 );
    const float u = mobile_ui::unit();
    if( query.empty() ) {
        const page &tab = pages[current];
        if( open_section >= static_cast<int>( tab.more.size() ) ) {
            open_section = -1;
        }
        // a link row is as tall as its two lines
        const auto link_row = [&]( const std::string & title, const std::string & sub,
        const std::string & link ) {
            row r;
            r.kind = entry_kind::link;
            r.title = title;
            r.sub = sub;
            r.link = link;
            r.text_w = width - 32.0f * u - 30.0f * u;
            r.height = std::max( 60.0f * u, 24.0f * u +
                                 mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w ).y + 2.0f * u +
                                 mobile_ui::text_size( face::body, 12.0f, r.sub, r.text_w ).y );
            rows.push_back( std::move( r ) );
        };
        const auto heading = [&]( const std::string & title ) {
            row r;
            r.kind = entry_kind::group;
            r.title = title;
            r.height = 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, r.title ).y + 6.0f * u;
            rows.push_back( std::move( r ) );
        };
        if( open_section >= 0 ) {
            const section &sec = tab.more[static_cast<size_t>( open_section )];
            if( !side ) {
                //~ A row that leads back from a group of options to its tab; %s is the tab
                link_row( string_format( _( "Back to %s" ), tab.name ), sec.name, "more_back" );
            } else {
                heading( sec.name );
            }
            add_rows( sec.entries, width );
            return;
        }
        add_rows( tab.entries, width );
        if( !side && !tab.more.empty() ) {
            //~ Heading over the groups that hold the rest of a tab's options
            heading( _( "More" ) );
            for( size_t i = 0; i < tab.more.size(); ++i ) {
                link_row( tab.more[i].name, tab.more[i].sub, "more:" + std::to_string( i ) );
            }
        }
        return;
    }
    // a search looks through every page
    for( const page &p : pages ) {
        const size_t before = rows.size();
        add_rows( p.entries, width );
        for( const section &sec : p.more ) {
            add_rows( sec.entries, width );
        }
        if( rows.size() > before && pages.size() > 1 ) {
            row head;
            head.kind = entry_kind::page;
            head.title = p.name;
            head.height = 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, head.title ).y + 6.0f * u;
            rows.insert( rows.begin() + static_cast<std::ptrdiff_t>( before ), std::move( head ) );
        }
    }
}

void options_window::draw_search( const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 field = pos;
    const ImVec2 field_max( pos.x + width, pos.y + 44.0f * u );
    mobile_ui::box( draw, field, field_max, col_field, mobile_ui::col_line, 11.0f );
    mobile_ui::icon( draw, "search", ImVec2( field.x + 12.0f * u, field.y + 13.0f * u ), 18.0f,
                     mobile_ui::col_muted );
    const float clear_w = query.empty() ? 0.0f : 44.0f * u;
    const float text_h = 15.0f * u;
    const float pad_y = std::floor( ( 44.0f * u - text_h ) * 0.5f - 2.0f * u );
    if( drop_focus ) {
        ImGui::ClearActiveID();
        drop_focus = false;
    }
    ImGui::SetCursorScreenPos( ImVec2( field.x + 38.0f * u, field.y ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, pad_y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
    ImGui::PushFont( nullptr, text_h / std::max( 0.1f, ImGui::GetStyle().FontScaleMain *
                     ImGui::GetStyle().FontScaleDpi ) );
    ImGui::SetNextItemWidth( field_max.x - field.x - 38.0f * u - clear_w - 6.0f * u );
    ImGui::InputTextWithHint( "##options_search",
                              world_options_only ? _( "Search options" ) : _( "Search every setting" ), &query );
    search_active = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 3 );
    if( search_active ) {
        draw->AddRect( field, field_max, mobile_ui::col_accent, 11.0f * u, 0, 2.0f * u );
    }
    if( !query.empty() ) {
        const ImVec2 clear_pos( field_max.x - 44.0f * u, field.y );
        if( mobile_ui::pressable( "##options_search_clear", clear_pos,
                                  ImVec2( 44.0f * u, 44.0f * u ) ) ) {
            query.clear();
            drop_focus = true;
        }
        mobile_ui::icon( draw, "x", ImVec2( clear_pos.x + 13.0f * u, clear_pos.y + 13.0f * u ), 18.0f,
                         mobile_ui::col_muted );
    }
    if( query != applied_query ) {
        dirty = true;
    }
}

void options_window::draw_chips( const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    const float row_h = 50.0f * u;
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##options_chips", ImVec2( width, row_h ), ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float x = base.x + 12.0f * u;
    for( size_t i = 0; i < pages.size(); ++i ) {
        const std::string &label = pages[i].name;
        // while searching the results come from every page, so none is lit
        const bool on = query.empty() && i == current;
        const ImVec2 label_size = mobile_ui::text_size( face::bold, 13.0f, label );
        const float chip_w = label_size.x + 26.0f * u;
        const ImVec2 chip_pos( x, base.y + 10.0f * u );
        const ImVec2 chip_max( x + chip_w, chip_pos.y + 30.0f * u );
        bool held = false;
        // the whole height of the row takes the tap
        if( mobile_ui::pressable( "##options_chip" + std::to_string( i ), ImVec2( x - 3.0f * u, base.y ),
                                  ImVec2( chip_w + 6.0f * u, row_h ), &held ) ) {
            current = i;
            open_section = -1;
            query.clear();
            drop_focus = true;
            dirty = true;
            to_top = true;
        }
        mobile_ui::box( draw, chip_pos, chip_max,
                        on ? mobile_ui::col_accent : held ? mobile_ui::col_button_down :
                        IM_COL32( 255, 255, 255, 10 ), on ? mobile_ui::col_accent : mobile_ui::col_line, 15.0f );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( chip_pos.x + 13.0f * u,
                         chip_pos.y + ( 30.0f * u - label_size.y ) * 0.5f ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, label );
        x += chip_w + 6.0f * u;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( x + 6.0f * u - base.x, 1.0f ) );
    // sideways swipes arrive as arrow keys; the mouse wheel works on desktop
    if( ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheelH != 0.0f ) {
        chip_scroll_by -= ImGui::GetIO().MouseWheelH * 60.0f * u;
    }
    if( chip_scroll_by != 0.0f ) {
        ImGui::SetScrollX( std::clamp( ImGui::GetScrollX() + chip_scroll_by, 0.0f,
                                       std::max( 0.0f, x + 6.0f * u - base.x - width ) ) );
        chip_scroll_by = 0.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool options_window::step_button( ImDrawList *const draw, const std::string &id,
                                  const char *const icon_name, const ImVec2 &pos, const float row_h, const bool enabled )
{
    const float u = mobile_ui::unit();
    // 36 units to look at, 44 wide and the row's height to touch
    const ImVec2 box_min( pos.x + 4.0f * u, pos.y + ( row_h - 36.0f * u ) * 0.5f );
    const ImVec2 box_max( box_min.x + 36.0f * u, box_min.y + 36.0f * u );
    bool held = false;
    // a finger only reaches ImGui as a tap once it is lifted, so there is no
    // stepping while it rests; the number itself opens the keypad for big changes
    const bool step = enabled && mobile_ui::pressable( id, pos, ImVec2( 44.0f * u, row_h ), &held );
    mobile_ui::box( draw, box_min, box_max,
                    held ? mobile_ui::col_button_down : enabled ? IM_COL32( 255, 255, 255, 15 ) :
                    IM_COL32( 255, 255, 255, 6 ), enabled ? mobile_ui::col_line : IM_COL32( 232, 236, 222, 14 ),
                    10.0f );
    mobile_ui::icon( draw, icon_name, ImVec2( box_min.x + 9.0f * u, box_min.y + 9.0f * u ), 18.0f,
                     enabled ? mobile_ui::col_fg : col_disabled );
    return step;
}

void options_window::draw_row( ImDrawList *const draw, row &r, const ImVec2 &pos,
                               const float width )
{
    const float u = mobile_ui::unit();
    const float left = pos.x;
    if( r.kind == entry_kind::gap ) {
        return;
    }
    if( r.kind == entry_kind::group || r.kind == entry_kind::page ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left + 16.0f * u, pos.y + 16.0f * u ),
                         r.kind == entry_kind::page ? mobile_ui::col_accent : mobile_ui::col_muted,
                         to_upper_case( r.title ) );
        return;
    }
    if( r.kind == entry_kind::lead ) {
        mobile_ui::text( draw, face::body, 13.0f, ImVec2( left + 16.0f * u, pos.y + 8.0f * u ), col_lead,
                         r.title, r.text_w );
        return;
    }
    if( r.kind == entry_kind::link ) {
        bool held = false;
        if( !r.off && mobile_ui::pressable( "##link_" + r.link, pos, ImVec2( width, r.height ), &held ) ) {
            open_link = r.link;
        }
        if( held ) {
            draw->AddRectFilled( pos, ImVec2( pos.x + width, pos.y + r.height ), mobile_ui::col_button_down );
        }
        const float title_h = mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w ).y;
        mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, pos.y + 12.0f * u ),
                         r.off ? col_disabled : mobile_ui::col_fg, r.title, r.text_w );
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( left + 16.0f * u,
                         pos.y + 12.0f * u + title_h + 2.0f * u ), mobile_ui::col_muted, r.sub, r.text_w );
        mobile_ui::icon( draw, r.link == "more_back" ? "back" : "chev",
                         ImVec2( left + width - 36.0f * u, pos.y + ( r.height - 20.0f * u ) * 0.5f ), 20.0f,
                         r.off ? col_disabled : mobile_ui::col_muted );
        draw->AddLine( ImVec2( left, pos.y + r.height ), ImVec2( left + width, pos.y + r.height ),
                       col_row_line, std::max( 1.0f, u ) );
        return;
    }
    cOpt &opt = *r.opt;
    const std::string id = "##opt_" + opt.getName();
    const float right = left + width - 16.0f * u;
    const float control_x = right - r.control_w;

    // the row itself is the target, except under a control with parts of its own
    const bool own_parts = r.ctl == control::stepper || r.ctl == control::segment ||
                           r.ctl == control::slider;
    const float target_w = own_parts && r.enabled ? control_x - left : width;
    bool row_held = false;
    const bool row_tapped = mobile_ui::pressable( id, pos, ImVec2( target_w, r.height ), &row_held );
    if( row_held && r.ctl != control::toggle ) {
        draw->AddRectFilled( pos, ImVec2( pos.x + target_w, pos.y + r.height ),
                             mobile_ui::col_button_down );
    }

    // name and explanation
    const ImVec2 title_px = mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w );
    const float text_h = r.height - 24.0f * u;
    const float used_h = r.sub.empty() ? title_px.y : text_h;
    const float top = pos.y + ( r.height - std::min( text_h, used_h ) ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, top ),
                     r.enabled ? mobile_ui::col_fg : col_disabled, r.title, r.text_w );
    if( !r.sub.empty() ) {
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( left + 16.0f * u, top + title_px.y + 2.0f * u ),
                         mobile_ui::col_muted, r.sub, r.text_w );
    }

    bool changed = false;
    switch( r.ctl ) {
        case control::toggle: {
            const bool on = opt.value_as<bool>();
            const ImVec2 sw( control_x, pos.y + ( r.height - 30.0f * u ) * 0.5f );
            ImU32 fill = on ? mobile_ui::col_accent : col_switch_off;
            if( !r.enabled ) {
                fill = ( fill & ~IM_COL32_A_MASK ) | ( ( ( fill >> IM_COL32_A_SHIFT ) * 2 / 5 ) <<
                                                       IM_COL32_A_SHIFT );
            }
            draw->AddRectFilled( sw, ImVec2( sw.x + 50.0f * u, sw.y + 30.0f * u ), fill, 15.0f * u );
            draw->AddCircleFilled( ImVec2( sw.x + ( on ? 35.0f : 15.0f ) * u, sw.y + 15.0f * u ), 12.0f * u,
                                   r.enabled ? IM_COL32_WHITE : IM_COL32( 255, 255, 255, 110 ), 32 );
            if( row_tapped ) {
                if( r.enabled ) {
                    opt.setNext();
                    changed = true;
                } else {
                    asked = &opt;
                }
            }
            break;
        }
        case control::stepper: {
            const ImVec2 value_px = mobile_ui::text_size( face::mono, 13.0f, r.value );
            const float value_w = r.control_w - 88.0f * u;
            if( step_button( draw, id + "_dec", "minus", ImVec2( control_x, pos.y ), r.height,
                             r.enabled && r.can_dec ) ) {
                opt.setPrev();
                changed = true;
            }
            if( step_button( draw, id + "_inc", "plus", ImVec2( right - 44.0f * u, pos.y ), r.height,
                             r.enabled && r.can_inc ) ) {
                opt.setNext();
                changed = true;
            }
            if( r.enabled ) {
                // the number itself opens the keypad
                bool value_held = false;
                if( mobile_ui::pressable( id + "_value", ImVec2( control_x + 44.0f * u, pos.y ),
                                          ImVec2( value_w, r.height ), &value_held ) ) {
                    asked = &opt;
                }
                if( value_held ) {
                    mobile_ui::box( draw, ImVec2( control_x + 44.0f * u, pos.y + ( r.height - 36.0f * u ) * 0.5f ),
                                    ImVec2( control_x + 44.0f * u + value_w, pos.y + ( r.height + 36.0f * u ) * 0.5f ),
                                    mobile_ui::col_button_down, 0, 10.0f );
                }
            }
            mobile_ui::text( draw, face::mono, 13.0f,
                             ImVec2( control_x + 44.0f * u + ( value_w - value_px.x ) * 0.5f,
                                     pos.y + ( r.height - value_px.y ) * 0.5f ),
                             r.enabled ? mobile_ui::col_fg : col_disabled, r.value );
            if( row_tapped ) {
                asked = &opt;
            }
            break;
        }
        case control::segment: {
            const std::string current_value = opt.getValue( true );
            const float label_h = mobile_ui::text_size( face::bold, 12.5f, "X" ).y;
            const float seg_h = label_h + 18.0f * u + 4.0f * u;
            const ImVec2 seg_pos( control_x, pos.y + ( r.height - seg_h ) * 0.5f );
            mobile_ui::box( draw, seg_pos, ImVec2( seg_pos.x + r.control_w, seg_pos.y + seg_h ),
                            IM_COL32( 255, 255, 255, 15 ), 0, 10.0f );
            float x = seg_pos.x + 2.0f * u;
            for( size_t i = 0; i < r.look.choices.size(); ++i ) {
                const bool on = r.look.choices[i].first == current_value;
                if( on ) {
                    mobile_ui::box( draw, ImVec2( x, seg_pos.y + 2.0f * u ),
                                    ImVec2( x + r.seg_widths[i], seg_pos.y + seg_h - 2.0f * u ),
                                    r.enabled ? mobile_ui::col_accent : col_switch_off, 0, 8.0f );
                }
                mobile_ui::text( draw, face::bold, 12.5f, ImVec2( x + 10.0f * u, seg_pos.y + 11.0f * u ),
                                 !r.enabled ? col_disabled : on ? mobile_ui::col_on_accent : mobile_ui::col_muted,
                                 r.look.choices[i].second );
                // the target is as tall as the row
                if( r.enabled && mobile_ui::pressable( id + "_seg" + std::to_string( i ), ImVec2( x, pos.y ),
                                                       ImVec2( r.seg_widths[i], r.height ) ) && !on ) {
                    opt.setValue( r.look.choices[i].first );
                    changed = true;
                }
                x += r.seg_widths[i];
            }
            if( row_tapped && !r.enabled ) {
                asked = &opt;
            }
            break;
        }
        case control::slider: {
            const int lo = r.look.lo;
            const int hi = r.look.hi;
            const int value = opt.value_as<int>();
            const float mid = pos.y + r.height * 0.5f;
            // the track; a tap or a drag anywhere on it moves the thumb there
            const float knob = 10.0f * u;
            const float track_x0 = control_x + knob;
            const float track_x1 = right - 52.0f * u - 8.0f * u - knob;
            bool held = false;
            const bool tapped = r.enabled &&
                                mobile_ui::pressable( id + "_track", ImVec2( track_x0 - knob, pos.y ),
                                        ImVec2( track_x1 - track_x0 + 2.0f * knob, r.height ), &held );
            int shown = value;
            if( ( held || tapped ) && hi > lo && track_x1 > track_x0 ) {
                const float wanted = std::clamp( ( ImGui::GetIO().MousePos.x - track_x0 ) /
                                                 ( track_x1 - track_x0 ), 0.0f, 1.0f );
                const int steps = static_cast<int>( std::lround( wanted * static_cast<float>( hi - lo ) /
                                                    static_cast<float>( r.look.step ) ) );
                shown = std::clamp( lo + steps * r.look.step, lo, hi );
                if( shown != value ) {
                    opt.setValue( shown );
                    // the row keeps its place, so nothing else has to be worked out again
                    r.value = opt.getValueName();
#if defined(__ANDROID__)
                    request_terminal_relayout();
#endif
                }
            }
            const float fraction = hi > lo ? std::clamp( static_cast<float>( shown - lo ) /
                                   static_cast<float>( hi - lo ), 0.0f, 1.0f ) : 0.0f;
            const float thumb_x = track_x0 + ( track_x1 - track_x0 ) * fraction;
            const ImU32 ink = r.enabled ? mobile_ui::col_accent : col_switch_off;
            draw->AddRectFilled( ImVec2( track_x0, mid - 2.0f * u ), ImVec2( track_x1, mid + 2.0f * u ),
                                 col_switch_off, 2.0f * u );
            draw->AddRectFilled( ImVec2( track_x0, mid - 2.0f * u ), ImVec2( thumb_x, mid + 2.0f * u ), ink,
                                 2.0f * u );
            draw->AddCircleFilled( ImVec2( thumb_x, mid ), held ? knob * 1.15f : knob,
                                   r.enabled ? mobile_ui::col_accent : IM_COL32( 120, 124, 114, 255 ), 32 );
            // the value, right aligned in a column of its own
            const std::string label = r.look.percent ?
                                      //~ a volume as a share of the loudest
                                      string_format( _( "%d%%" ), static_cast<int>( std::lround( fraction * 100.0f ) ) ) :
                                      //~ a length of time in milliseconds
                                      string_format( _( "%d ms" ), shown );
            const ImVec2 label_px = mobile_ui::text_size( face::mono, 11.5f, label );
            mobile_ui::text( draw, face::mono, 11.5f, ImVec2( right - label_px.x, mid - label_px.y * 0.5f ),
                             r.enabled ? mobile_ui::col_fg : col_disabled, label );
            slider_rows.push_back( { track_x0 - knob, pos.y, track_x1 - track_x0 + 2.0f * knob, r.height } );
            if( row_tapped && !r.enabled ) {
                asked = &opt;
            }
            break;
        }
        case control::choice: {
            const ImVec2 value_px = mobile_ui::text_size( face::bold, 13.0f, r.value );
            const float value_w = r.control_w - 24.0f * u;
            const float value_x = control_x + std::max( 0.0f, value_w - value_px.x );
            draw->PushClipRect( ImVec2( control_x, pos.y ), ImVec2( control_x + value_w, pos.y + r.height ),
                                true );
            mobile_ui::text( draw, face::bold, 13.0f, ImVec2( value_x,
                             pos.y + ( r.height - value_px.y ) * 0.5f ),
                             r.enabled ? mobile_ui::col_accent : col_disabled, r.value );
            draw->PopClipRect();
            mobile_ui::icon( draw, "chev", ImVec2( right - 20.0f * u, pos.y + ( r.height - 20.0f * u ) * 0.5f ),
                             20.0f, mobile_ui::col_muted );
            if( row_tapped ) {
                asked = &opt;
            }
            break;
        }
    }
    if( changed ) {
        dirty = true;
#if defined(__ANDROID__)
        // the grid reads live options, so an edit shows at once (as upstream)
        request_terminal_relayout();
#endif
    }
    draw->AddLine( ImVec2( left, pos.y + r.height ), ImVec2( left + width, pos.y + r.height ),
                   col_row_line, std::max( 1.0f, u ) );
}

void options_window::draw_list( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    // the scrollbar gets a strip of its own, clear of the controls
    const float row_w = size.x - ImGui::GetStyle().ScrollbarSize;
    if( dirty || rows_width != row_w ) {
        rebuild( row_w );
    }
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##options_list", size, ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    if( to_top ) {
        ImGui::SetScrollY( 0.0f );
        to_top = false;
    }
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float y = base.y;
    if( rows.empty() ) {
        const std::string none = query.empty() ? _( "Nothing to change here." ) :
                                 string_format( _( "No option matches \"%s\"." ), query );
        mobile_ui::text( draw, face::body, 14.0f, ImVec2( base.x + 16.0f * u, y + 20.0f * u ),
                         mobile_ui::col_muted, none, size.x - 32.0f * u );
        y += 60.0f * u;
    }
    slider_rows.clear();
    for( row &r : rows ) {
        // rows out of view only take their room
        if( y + r.height >= pos.y && y <= pos.y + size.y ) {
            draw_row( draw, r, ImVec2( base.x, y ), row_w );
        }
        y += r.height;
    }
    // A finger on a slider drags its thumb; anywhere else it scrolls the list.
    // There is one such area, so it goes to the longest run of sliders in view.
    ImVec4 drag( 0.0f, 0.0f, 0.0f, 0.0f );
    ImVec4 run( 0.0f, 0.0f, 0.0f, 0.0f );
    for( const ImVec4 &track : slider_rows ) {
        if( run.w > 0.0f && std::abs( track.y - ( run.y + run.w ) ) < 2.0f ) {
            run.w += track.w;
        } else {
            run = track;
        }
        if( run.w > drag.w ) {
            drag = run;
        }
    }
    const float drag_top = std::max( drag.y, pos.y );
    const float drag_bottom = std::min( drag.y + drag.w, pos.y + size.y );
    if( drag_bottom > drag_top ) {
        touch_input::set_raw_drag_rect( drag.x, drag_top, drag.z, drag_bottom - drag_top,
                                        touch_input::view_mode::overmap );
    } else {
        touch_input::clear_raw_drag_rect();
    }
    y += 16.0f * u + ( world_options_only && with_tabs ? 0.0f : safe.w );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void options_window::draw_more( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const page &tab = pages[current];
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##options_more", size, ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float y = base.y;
    //~ Heading over the groups that hold the rest of a tab's options; %s is the tab
    const std::string head = to_upper_case( string_format( _( "More %s" ), tab.name ) );
    mobile_ui::text( draw, face::mono, 10.5f, ImVec2( base.x + 16.0f * u, y + 16.0f * u ),
                     mobile_ui::col_muted, head );
    y += 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, head ).y + 6.0f * u;
    const float text_w = size.x - 32.0f * u - 26.0f * u;
    for( size_t i = 0; i < tab.more.size(); ++i ) {
        const section &sec = tab.more[i];
        const bool open = static_cast<int>( i ) == open_section;
        const float title_h = mobile_ui::text_size( face::bold, 14.0f, sec.name, text_w ).y;
        const float sub_h = mobile_ui::text_size( face::body, 12.0f, sec.sub, text_w ).y;
        const float row_h = std::max( 54.0f * u, title_h + 2.0f * u + sub_h + 20.0f * u );
        const ImVec2 row_pos( base.x, y );
        bool held = false;
        if( mobile_ui::pressable( "##more_" + std::to_string( i ), row_pos, ImVec2( size.x, row_h ),
                                  &held ) ) {
            open_link = "more:" + std::to_string( i );
        }
        if( open || held ) {
            draw->AddRectFilled( row_pos, ImVec2( row_pos.x + size.x, row_pos.y + row_h ),
                                 held ? mobile_ui::col_button_down : mobile_ui::col_accent_soft );
        }
        if( open ) {
            draw->AddRectFilled( row_pos, ImVec2( row_pos.x + 3.0f * u, row_pos.y + row_h ),
                                 mobile_ui::col_accent );
        }
        const float top = y + ( row_h - title_h - 2.0f * u - sub_h ) * 0.5f;
        mobile_ui::text( draw, face::bold, 14.0f, ImVec2( base.x + 16.0f * u, top ), mobile_ui::col_fg,
                         sec.name, text_w );
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( base.x + 16.0f * u, top + title_h + 2.0f * u ),
                         mobile_ui::col_muted, sec.sub, text_w );
        mobile_ui::icon( draw, "chev", ImVec2( base.x + size.x - 34.0f * u,
                                               y + ( row_h - 20.0f * u ) * 0.5f ),
                         20.0f, open ? mobile_ui::col_accent : mobile_ui::col_muted );
        y += row_h;
        draw->AddLine( ImVec2( base.x, y ), ImVec2( base.x + size.x, y ), col_row_line,
                       std::max( 1.0f, u ) );
    }
    y += 16.0f * u + safe.w;
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool options_window::foot_button( const std::string &id, const std::string &label,
                                  const ImVec2 &pos, const ImVec2 &size, const bool primary )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), fill,
                    primary ? 0 : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

void options_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const back_draw = ImGui::GetWindowDrawList();
    back_draw->PushClipRectFullScreen();
    back_draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                              mobile_ui::col_screen );
    back_draw->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    float y = origin.y + safe.y;
    float bar_h = 0.0f;
    const std::string title = world_options_only ? _( "World options" ) : _( "Settings" );
    if( mobile_ui::app_bar( title, ImVec2( left, y ), width, &bar_h ) ) {
        done = true;
        result = world_options_only ? "QUIT" : "";
    }
    if( landscape ) {
        // a wide screen has room for the search field in the bar
        const float field_x = left + 52.0f * u + mobile_ui::text_size( face::display, 21.0f, title ).x +
                              24.0f * u;
        draw_search( ImVec2( field_x, y + 5.0f * u ), left + width - 12.0f * u - field_x );
        y += bar_h;
    } else {
        y += bar_h + 10.0f * u;
        draw_search( ImVec2( left + 12.0f * u, y ), width - 24.0f * u );
        y += 44.0f * u;
    }
    if( pages.size() > 1 ) {
        draw_chips( ImVec2( left, y ), width );
        y += 50.0f * u;
    } else if( !landscape ) {
        y += 8.0f * u;
    }

    // world creation walks through its steps with the buttons at the bottom
    const bool foot = world_options_only && with_tabs;
    const float foot_h = foot ? 68.0f * u + safe.w : 0.0f;
    // on a wide screen the tab's "More" groups stand beside its everyday rows
    current = std::min( current, pages.empty() ? 0 : pages.size() - 1 );
    const bool wants_side = landscape && !world_options_only && query.empty() && !pages.empty() &&
                            !pages[current].more.empty() && width >= 700.0f * u;
    if( wants_side != side ) {
        side = wants_side;
        dirty = true;
    }
    const float list_h = origin.y + size.y - foot_h - y;
    const float side_w = side ? std::min( 380.0f * u, width * 0.36f ) : 0.0f;
    draw_list( ImVec2( left, y ), ImVec2( width - side_w, list_h ) );
    if( side ) {
        back_draw->AddLine( ImVec2( left + width - side_w, y ), ImVec2( left + width - side_w, y + list_h ),
                            mobile_ui::col_line, std::max( 1.0f, u ) );
        draw_more( ImVec2( left + width - side_w, y ), ImVec2( side_w, list_h ) );
    }
    if( open_link == "more_back" ) {
        open_link.clear();
        open_section = -1;
        dirty = true;
        to_top = true;
    } else if( open_link.rfind( "more:", 0 ) == 0 ) {
        const int wanted = std::atoi( open_link.c_str() + 5 );
        open_link.clear();
        // a second tap on the open group goes back to the everyday rows
        open_section = wanted == open_section ? -1 : wanted;
        dirty = true;
        to_top = true;
    }
    if( foot ) {
        const float foot_y = origin.y + size.y - foot_h;
        back_draw->AddLine( ImVec2( left, foot_y ), ImVec2( left + width, foot_y ), mobile_ui::col_line,
                            std::max( 1.0f, u ) );
        const float button_w = ( width - 24.0f * u - 10.0f * u ) * 0.5f;
        const ImVec2 button_size( button_w, 48.0f * u );
        if( foot_button( "##options_prev", _( "Previous" ), ImVec2( left + 12.0f * u, foot_y + 10.0f * u ),
                         button_size, false ) ) {
            done = true;
            result = "PREV_TAB";
        }
        if( foot_button( "##options_next", _( "Next" ),
                         ImVec2( left + 12.0f * u + button_w + 10.0f * u, foot_y + 10.0f * u ), button_size, true ) ) {
            done = true;
            result = "NEXT_TAB";
        }
    }
}

// The row of an option was tapped and a switch or a step is not enough: asks
// for the new value the way upstream's screen does on CONFIRM.
void ask( cOpt &opt )
{
#if defined(LOCALIZE)
    if( opt.getName() == "USE_LANG" ) {
        opt.setValue( select_language() );
        return;
    }
#endif
    if( opt.hasPrerequisite() && !opt.checkPrerequisite() ) {
        popup( _( "Prerequisite for this option not met!\n(%s)" ),
               get_options().get_option( opt.getPrerequisite() ).getMenuText() );
        return;
    }
    const std::string type = opt.getType();
    if( type == "string_select" ) {
        const std::vector<options_manager::id_and_option> items = opt.getItems();
        uilist menu;
        menu.title = opt.getMenuText();
        if( opt.getName() == "SOUNDPACKS" ) {
            menu.title = _( "Sound pack" );
            menu.text = string_format(
                            _( "A new pack is used after the game restarts.  To add a pack, copy its folder into %s" ),
                            PATH_INFO::user_sound().generic_u8string() );
        } else if( opt.getName() == "TILES" ) {
            menu.title = _( "Tileset" );
        }
        const std::string now = opt.getValue( true );
        for( size_t i = 0; i < items.size(); ++i ) {
            menu.addentry( static_cast<int>( i ), true, 0, items[i].second.translated() );
            if( items[i].first == now ) {
                menu.selected = static_cast<int>( i );
            }
        }
        menu.query();
        if( menu.ret >= 0 && static_cast<size_t>( menu.ret ) < items.size() ) {
            opt.setValue( items[static_cast<size_t>( menu.ret )].first );
        }
    } else if( type == "int_map" ) {
        const std::vector<std::string> names = int_map_names( opt );
        uilist menu;
        menu.title = opt.getMenuText();
        for( size_t i = 0; i < names.size(); ++i ) {
            menu.addentry( static_cast<int>( i ), true, 0, names[i] );
        }
        menu.selected = 0;
        menu.query();
        // the list starts at the current value and follows upstream's order
        for( int i = 0; i < menu.ret; ++i ) {
            opt.setNext();
        }
    } else if( type == "int" ) {
        number_input_popup<int> entry( 0, opt.value_as<int>() );
        entry.set_label( opt.getMenuText() );
        opt.setValue( entry.query() );
    } else if( type == "float" ) {
        number_input_popup<float> entry( 0, opt.value_as<float>() );
        entry.set_label( opt.getMenuText() );
        opt.setValue( entry.query() );
    } else {
        // string_input opens its text field; a bool flips
        opt.setNext();
    }
#if defined(__ANDROID__)
    request_terminal_relayout();
#endif
}

} // namespace

std::string show( options_manager &options, const bool ingame, const bool world_options_only,
                  const bool with_tabs )
{
    options_window window( world_options_only ? access::pages( options, ingame, true ) :
                           access::tabs( options, ingame ), world_options_only, with_tabs );
    input_context ctxt( "default", keyboard_mode::keychar );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    // the search field takes the keys
    ctxt.set_timeout( 50 );
    while( !window.done ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            if( !window.back() ) {
                window.leave();
            }
        } else if( ( action == "LEFT" || action == "RIGHT" ) && !window.typing() ) {
            // a swipe to the left brings the chips on the right into view
            window.scroll_chips( action == "LEFT" ? 1 : -1 );
        }
        if( !window.open_link.empty() ) {
            const std::string link = window.open_link;
            window.open_link.clear();
            if( link == "controller" ) {
                mobile_controls::show();
            } else if( link == "keys" ) {
                input_context keys = get_default_mode_input_context();
                keys.display_menu();
            } else if( link == "screen_sizes" ) {
                mobile_settings::show_screen_sizes();
            } else if( link == "edit_hud" ) {
                // the editor works over the map, once this screen has closed and saved
                mobile_settings::request_edit_hud();
                window.leave();
            } else if( link == "safemode_rules" ) {
                get_safemode().show();
            } else if( link == "autopickup_rules" ) {
                get_auto_pickup().show();
            }
            window.dirty = true;
        }
        if( window.asked != nullptr ) {
            cOpt &opt = *window.asked;
            window.asked = nullptr;
            ask( opt );
            window.dirty = true;
        }
    }
    touch_input::clear_raw_drag_rect();
    return window.result;
}

} // namespace mobile_options

#else // !TILES

namespace mobile_options
{

std::string show( options_manager &, bool, bool, bool )
{
    return "";
}

} // namespace mobile_options

#endif // TILES
