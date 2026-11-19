#include "mobile_pad.h"

#include <algorithm>
#include <cmath>
#include <ostream>

#include "cata_utility.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "options.h"
#include "path_info.h"
#include "string_formatter.h"
#include "translations.h"

#if defined(TILES)
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "sdl_wrappers.h"
#endif

namespace mobile_pad
{

const std::string action_step = "pad_step";

namespace
{

// how far a stick is pushed, of 1: a slot is pointed at above push, the stick
// is back below centre, and a flick is timed from high where it got that far
constexpr float mark_centre = 0.25f;
constexpr float mark_push = 0.5f;
constexpr float mark_high = 0.75f;

const std::array<const char *, num_buttons> ids = { {
        "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "L3", "R3",
        "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "START"
    }
};

bool is_dpad( const button b )
{
    return b == button::up || b == button::down || b == button::left || b == button::right;
}

} // namespace

std::string button_id( const button b )
{
    const int i = static_cast<int>( b );
    return i >= 0 && i < num_buttons ? ids[static_cast<size_t>( i )] : "";
}

const menu *layout::find_menu( const std::string &name ) const
{
    if( name.empty() ) {
        return nullptr;
    }
    for( const menu &m : menus ) {
        if( m.name == name ) {
            return &m;
        }
    }
    return nullptr;
}

const menu *layout::menu_on( const button b, const int stick ) const
{
    const assignment &a = of( b );
    return find_menu( stick == 0 ? a.menu_left : a.menu_right );
}

bool layout::has_menus( const button b ) const
{
    return menu_on( b, 0 ) != nullptr || menu_on( b, 1 ) != nullptr;
}

bool layout::has_long_press( const button b ) const
{
    return !of( b ).long_press.empty() && !has_menus( b );
}

void layout::rename_menu( const std::string &from, const std::string &to )
{
    for( menu &m : menus ) {
        if( m.name == from ) {
            m.name = to;
        }
    }
    for( assignment &a : buttons ) {
        if( a.menu_left == from ) {
            a.menu_left = to;
        }
        if( a.menu_right == from ) {
            a.menu_right = to;
        }
    }
}

void layout::remove_menu( const std::string &name )
{
    menus.erase( std::remove_if( menus.begin(), menus.end(), [&name]( const menu & m ) {
        return m.name == name;
    } ), menus.end() );
    for( assignment &a : buttons ) {
        if( a.menu_left == name ) {
            a.menu_left.clear();
        }
        if( a.menu_right == name ) {
            a.menu_right.clear();
        }
    }
}

std::string layout::free_menu_name( const std::string &stem ) const
{
    if( find_menu( stem ) == nullptr ) {
        return stem;
    }
    for( int n = 2; ; ++n ) {
        const std::string name = string_format( "%s %d", stem, n );
        if( find_menu( name ) == nullptr ) {
            return name;
        }
    }
}

layout default_layout()
{
    layout l;
    // the single presses are upstream's
    l.of( button::a ) = { "interact", "action_menu", "", "" };
    l.of( button::b ) = { "grab", "", "", "" };
    l.of( button::x ) = { "smash", "item_action_menu", "", "" };
    l.of( button::y ) = { "throw", "cast_spell", "", "" };
    l.of( button::l1 ) = { "fire", "", "Move and view", "" };
    l.of( button::r1 ) = { "autoattack", "pick_style", "", "" };
    l.of( button::l2 ) = { "reload_weapon", "", "Activities", "Character" };
    l.of( button::r2 ) = { action_step, "", "", "" };
    l.of( button::l3 ) = { "ignore_enemy", "", "", "" };
    l.of( button::r3 ) = { "peek", "", "", "" };
    l.of( button::up ) = { "eat", "", "", "" };
    l.of( button::down ) = { "drop", "", "", "" };
    l.of( button::left ) = { "wear", "take_off", "", "" };
    l.of( button::right ) = { "wield", "apply_wielded", "", "" };
    l.of( button::select ) = { "map", "", "", "" };
    l.of( button::start ) = { "main_menu", "", "", "" };
    // upstream's two rings of the play screen
    l.menus.push_back( { "Activities", { {
                "craft", "read", "chat", "wait", "construct", "sleep", "disassemble", "unload"
            }
        }
    } );
    l.menus.push_back( { "Character", { {
                "player_data", "medical", "factions", "advinv", "inventory", "bodystatus", "missions",
                "morale"
            }
        }
    } );
    // what upstream had on the left trigger with a button
    l.menus.push_back( { "Move and view", { {
                "LEVEL_UP", "zoom_in", "listitems", "open_movement", "LEVEL_DOWN", "zoom_out", "haul",
                "safemode"
            }
        }
    } );
    return l;
}

static cata_path layout_path()
{
    return PATH_INFO::config_dir_path() / "mobile_pad.json";
}

static void load( layout &l )
{
    read_from_file_optional_json( layout_path(), [&l]( const JsonValue & jv ) {
        const JsonObject jo = jv;
        jo.allow_omitted_members();
        layout read;
        if( jo.has_object( "buttons" ) ) {
            const JsonObject jb = jo.get_object( "buttons" );
            jb.allow_omitted_members();
            for( int i = 0; i < num_buttons; ++i ) {
                const std::string id = ids[static_cast<size_t>( i )];
                if( !jb.has_object( id ) ) {
                    // a button the file does not name keeps its default
                    read.buttons[static_cast<size_t>( i )] = l.buttons[static_cast<size_t>( i )];
                    continue;
                }
                const JsonObject ja = jb.get_object( id );
                ja.allow_omitted_members();
                assignment &a = read.buttons[static_cast<size_t>( i )];
                a.press = ja.get_string( "press", "" );
                a.long_press = ja.get_string( "long_press", "" );
                a.menu_left = ja.get_string( "menu_left", "" );
                a.menu_right = ja.get_string( "menu_right", "" );
            }
        } else {
            read.buttons = l.buttons;
        }
        if( jo.has_array( "menus" ) ) {
            for( const JsonObject jm : jo.get_array( "menus" ) ) {
                jm.allow_omitted_members();
                menu m;
                m.name = jm.get_string( "name", "" );
                size_t at = 0;
                if( jm.has_array( "slots" ) ) {
                    for( const std::string slot : jm.get_array( "slots" ) ) {
                        if( at < m.slots.size() ) {
                            m.slots[at++] = slot;
                        }
                    }
                }
                if( !m.name.empty() && read.find_menu( m.name ) == nullptr ) {
                    read.menus.push_back( m );
                }
            }
        } else {
            read.menus = l.menus;
        }
        l = read;
    } );
}

layout &current()
{
    static layout l = default_layout();
    static bool loaded = false;
    if( !loaded ) {
        loaded = true;
        load( l );
    }
    return l;
}

void save()
{
    const layout &l = current();
    write_to_file( layout_path(), [&l]( std::ostream & out ) {
        JsonOut jsout( out, true );
        jsout.start_object();
        jsout.member( "buttons" );
        jsout.start_object();
        for( int i = 0; i < num_buttons; ++i ) {
            const assignment &a = l.buttons[static_cast<size_t>( i )];
            jsout.member( ids[static_cast<size_t>( i )] );
            jsout.start_object();
            jsout.member( "press", a.press );
            jsout.member( "long_press", a.long_press );
            jsout.member( "menu_left", a.menu_left );
            jsout.member( "menu_right", a.menu_right );
            jsout.end_object();
        }
        jsout.end_object();
        jsout.member( "menus" );
        jsout.start_array();
        for( const menu &m : l.menus ) {
            jsout.start_object();
            jsout.member( "name", m.name );
            jsout.member( "slots" );
            jsout.start_array();
            for( const std::string &slot : m.slots ) {
                jsout.write( slot );
            }
            jsout.end_array();
            jsout.end_object();
        }
        jsout.end_array();
        jsout.end_object();
    }, _( "controller layout" ) );
}

bool enabled()
{
    return get_option<bool>( "MOBILE_PAD" );
}

int hold_ms()
{
    return get_option<int>( "MOBILE_PAD_HOLD_MS" );
}

int flick_ms()
{
    return get_option<int>( "MOBILE_PAD_FLICK_MS" );
}

int slot_at( const float x, const float y )
{
    // 0 is up, clockwise; y grows downward
    const float pi = 3.14159265f;
    float turn = std::atan2( x, -y ) / ( 2.0f * pi );
    if( turn < 0.0f ) {
        turn += 1.0f;
    }
    return static_cast<int>( std::floor( turn * num_slots + 0.5f ) ) % num_slots;
}

void machine::reset()
{
    holder = button::count;
    state = phase::waiting;
    close_ring();
}

void machine::close_ring()
{
    ring_stick = -1;
    slot = -1;
}

bool machine::open_ring_if_pushed( const layout &l )
{
    for( int s = 0; s < 2; ++s ) {
        const stick_state &st = sticks[static_cast<size_t>( s )];
        if( st.above_push && l.menu_on( holder, s ) != nullptr ) {
            ring_stick = s;
            slot = slot_at( st.x, st.y );
            return true;
        }
    }
    return false;
}

outcome machine::come_of_age( const layout &l )
{
    outcome out;
    if( l.has_menus( holder ) ) {
        state = phase::held;
        out.changed = open_ring_if_pushed( l );
    } else {
        state = phase::done;
        out.action = l.of( holder ).long_press;
    }
    return out;
}

outcome machine::tick( const layout &l, const uint32_t now )
{
    if( holder != button::count && state == phase::waiting &&
        now - down_at >= static_cast<uint32_t>( hold_time ) ) {
        return come_of_age( l );
    }
    return {};
}

outcome machine::button_event( const layout &l, const button b, const bool down,
                               const uint32_t now )
{
    outcome out;
    if( b == button::count ) {
        return out;
    }
    if( down ) {
        if( b == holder ) {
            return out;
        }
        if( ring_open() && b == button::b ) {
            // B backs out of the ring; the held button has had its turn
            close_ring();
            state = phase::done;
            out.changed = true;
            return out;
        }
        const bool waits = l.has_menus( b ) || l.has_long_press( b );
        if( !waits || holder != button::count ) {
            out.action = l.of( b ).press;
            out.repeat = !waits && ( is_dpad( b ) || out.action == action_step );
            return out;
        }
        holder = b;
        state = phase::waiting;
        down_at = now;
        close_ring();
        return out;
    }
    if( b != holder ) {
        return out;
    }
    if( state == phase::waiting && now - down_at >= static_cast<uint32_t>( hold_time ) ) {
        // let go after the hold time, before a tick saw it
        out = come_of_age( l );
        if( !out.action.empty() ) {
            holder = button::count;
            return out;
        }
    }
    if( state == phase::waiting ) {
        out.action = l.of( b ).press;
    } else if( state == phase::held && ring_open() && slot >= 0 ) {
        if( const menu *const m = l.menu_on( b, ring_stick ) ) {
            out.action = m->slots[static_cast<size_t>( slot )];
        }
    }
    out.changed = out.changed || ring_open();
    holder = button::count;
    close_ring();
    return out;
}

outcome machine::stick_event( const layout &l, const int stick, const float x, const float y,
                              const uint32_t now )
{
    outcome out;
    if( stick < 0 || stick > 1 ) {
        return out;
    }
    stick_state &st = sticks[static_cast<size_t>( stick )];
    st.x = x;
    st.y = y;
    const float push = std::sqrt( x * x + y * y );
    if( push >= mark_high ) {
        st.above_high = true;
        st.was_high = true;
    } else if( st.above_high ) {
        st.above_high = false;
        st.left_high = now;
    }
    const bool was_pushed = st.above_push;
    if( push >= mark_push ) {
        st.above_push = true;
    } else if( st.above_push ) {
        st.above_push = false;
        st.left_push = now;
    }
    const bool centred = push < mark_centre;
    // a flick: from pushed to the centre within the flick time
    const bool flicked = centred && ( was_pushed || st.was_high || slot >= 0 ) &&
                         now - ( st.was_high ? st.left_high : st.left_push ) <=
                         static_cast<uint32_t>( flick_time );
    if( centred ) {
        st.was_high = false;
    }
    if( holder == button::count ) {
        return out;
    }
    if( state == phase::waiting && now - down_at >= static_cast<uint32_t>( hold_time ) ) {
        out = come_of_age( l );
        if( state != phase::held ) {
            return out;
        }
    }
    if( state != phase::held ) {
        return out;
    }
    const menu *const m = l.menu_on( holder, stick );
    if( m == nullptr ) {
        return out;
    }
    if( st.above_push ) {
        const int pointed = slot_at( x, y );
        if( ring_stick != stick || slot != pointed ) {
            ring_stick = stick;
            slot = pointed;
        }
        // the dot follows the stick
        out.changed = true;
    } else if( ring_stick == stick ) {
        out.changed = true;
        if( centred && slot >= 0 ) {
            if( flicked ) {
                out.action = m->slots[static_cast<size_t>( slot )];
                // the button is still held: another push opens the ring again
                close_ring();
            } else {
                // eased back: nothing is pointed at any more
                slot = -1;
            }
        }
    }
    return out;
}

machine &state()
{
    static machine m;
    return m;
}

static std::string queued;

void queue_action( const std::string &action )
{
    queued = action;
}

bool has_queued_action()
{
    return !queued.empty();
}

std::string take_queued_action()
{
    std::string action;
    std::swap( action, queued );
    return action;
}

#if defined(TILES)

namespace
{
struct scripted {
    uint64_t at = 0;
    // 0 a button, 1 an axis
    int kind = 0;
    int index = 0;
    int value = 0;
};
SDL_Joystick *virtual_pad = nullptr;
std::vector<scripted> script;
} // namespace

static const char *script_path()
{
    static const char *const path = std::getenv( "CATA_PAD_SCRIPT" );
    return path;
}

bool script_wanted()
{
    return script_path() != nullptr;
}

void script_attach()
{
    if( !script_wanted() || virtual_pad != nullptr ) {
        return;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE( &desc );
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "CDDA test gamepad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick( &desc );
    if( id == 0 ) {
        return;
    }
    virtual_pad = SDL_OpenJoystick( id );
    if( virtual_pad != nullptr ) {
        // triggers rest at the low end
        SDL_SetJoystickVirtualAxis( virtual_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
        SDL_SetJoystickVirtualAxis( virtual_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
    }
}

static void script_load()
{
    if( !script.empty() ) {
        return;
    }
    std::ifstream in( script_path() );
    if( !in ) {
        return;
    }
    static const std::array<int, num_buttons> sdl_buttons = { {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1, -1, SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_START
        }
    };
    const uint64_t start = SDL_GetTicks();
    uint64_t at = 0;
    std::string kind;
    while( in >> at >> kind ) {
        scripted evt;
        evt.at = start + at;
        if( kind == "button" ) {
            std::string name;
            std::string way;
            in >> name >> way;
            const bool down = way == "down";
            for( int i = 0; i < num_buttons; ++i ) {
                if( name != ids[static_cast<size_t>( i )] ) {
                    continue;
                }
                if( sdl_buttons[static_cast<size_t>( i )] >= 0 ) {
                    evt.kind = 0;
                    evt.index = sdl_buttons[static_cast<size_t>( i )];
                    evt.value = down ? 1 : 0;
                } else {
                    evt.kind = 1;
                    evt.index = name == "L2" ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                    evt.value = down ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN;
                }
                script.push_back( evt );
            }
        } else if( kind == "stick" ) {
            std::string which;
            float x = 0.0f;
            float y = 0.0f;
            in >> which >> x >> y;
            const bool left = which == "l";
            evt.kind = 1;
            evt.index = left ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
            evt.value = static_cast<int>( std::lround( x * 32767.0f ) );
            script.push_back( evt );
            evt.index = left ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
            evt.value = static_cast<int>( std::lround( y * 32767.0f ) );
            script.push_back( evt );
        }
    }
    in.close();
    std::remove( script_path() );
}

void script_play()
{
    if( !script_wanted() || virtual_pad == nullptr ) {
        return;
    }
    script_load();
    const uint64_t now = SDL_GetTicks();
    while( !script.empty() && script.front().at <= now ) {
        const scripted &evt = script.front();
        if( evt.kind == 0 ) {
            SDL_SetJoystickVirtualButton( virtual_pad, static_cast<Uint8>( evt.index ), evt.value != 0 );
        } else {
            SDL_SetJoystickVirtualAxis( virtual_pad, static_cast<Uint8>( evt.index ),
                                        static_cast<Sint16>( evt.value ) );
        }
        script.erase( script.begin() );
    }
}

#endif // TILES

} // namespace mobile_pad
