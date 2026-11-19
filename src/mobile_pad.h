#pragma once
#ifndef CATA_SRC_MOBILE_PAD_H
#define CATA_SRC_MOBILE_PAD_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The controller layout of the mobile UI, for the play screen: what each button
// of a controller does when pressed, when pressed long, and which radial menu
// opens on each stick while it is held.  Made for a Steam Deck; it stands in
// for upstream's scheme (a modifier on the left trigger and two fixed rings)
// while the play screen has the input.  Menus and other screens keep upstream's
// buttons.
//
// - A button with no long press and no menus acts the moment it goes down.
// - A button with a long press acts on release if let go before the hold time,
//   and runs the long press at the hold time otherwise.
// - A button with menus acts on release if let go before the hold time.  Once
//   held, pushing a stick that has a menu opens its ring.  The slot the stick
//   points at runs when the stick snaps back to the centre within the flick
//   time, or when the button is let go with the stick still pushed.  Easing the
//   stick back, or B, runs nothing.
// - A button has a long press or menus, never both: the long press is ignored
//   while a menu is set.
//
// The layout is saved as mobile_pad.json in the config folder.  The screens
// that show and change it are in mobile_controls.h.
namespace mobile_pad
{

enum class button : int {
    a = 0,
    b,
    x,
    y,
    l1,
    r1,
    l2,
    r2,
    l3,
    r3,
    up,
    down,
    left,
    right,
    select,
    start,
    count
};

constexpr int num_buttons = static_cast<int>( button::count );
constexpr int num_slots = 8;

// "A", "L2", "UP": the name in the layout file.
std::string button_id( button b );

// The action that steps where the left stick points, or waits a turn with the
// stick centred.  Every other action is one of the play screen's (action.h).
extern const std::string action_step;

struct menu {
    std::string name;
    // up, up-right, right... clockwise; empty for an empty slot
    std::array<std::string, num_slots> slots;
};

struct assignment {
    std::string press;
    std::string long_press;
    // names of menus, or empty
    std::string menu_left;
    std::string menu_right;
};

// What pushing a stick does while no button with menus is held.
enum class stick_mode : int {
    // walk that way
    move = 0,
    // point at a neighbouring tile: "Step" walks there, Interact and others use it
    direction,
    // scroll the view
    pan,
    // up zooms in, down zooms out
    zoom,
    nothing,
    count
};

// "move", "direction", "pan", "zoom", "nothing": the name in the layout file.
std::string stick_mode_id( stick_mode m );

struct layout {
    std::array<assignment, num_buttons> buttons;
    std::vector<menu> menus;
    // left, right
    std::array<stick_mode, 2> sticks = { { stick_mode::move, stick_mode::pan } };
    // the d-pad walks, and its four buttons' assignments rest
    bool dpad_moves = false;

    const assignment &of( button b ) const {
        return buttons[static_cast<size_t>( b )];
    }
    assignment &of( button b ) {
        return buttons[static_cast<size_t>( b )];
    }
    // nullptr if there is no menu of that name
    const menu *find_menu( const std::string &name ) const;
    // the menu on a stick (0 left, 1 right) while the button is held, or nullptr
    const menu *menu_on( button b, int stick ) const;
    bool has_menus( button b ) const;
    // a long press that is in use: set, and no menus
    bool has_long_press( button b ) const;
    // renames a menu, in the buttons that use it too
    void rename_menu( const std::string &from, const std::string &to );
    void remove_menu( const std::string &name );
    // "Menu", "Menu 2"... : a name no menu has
    std::string free_menu_name( const std::string &stem ) const;
};

layout default_layout();
// The player's layout; read from the config folder on first use.
layout &current();
void save();

// The options: on at all, and the two times in milliseconds.
bool enabled();
int hold_ms();
int flick_ms();

// What the controller did, as the layout reads it.
struct outcome {
    // an action to run now, or empty
    std::string action;
    // the action repeats while the button stays down
    bool repeat = false;
    // the ring changed: draw again
    bool changed = false;
};

// Press, long press, hold and the ring, worked out from button and stick
// events.  Knows nothing of SDL, so that it can be tested.
class machine
{
    public:
        int hold_time = 250;
        int flick_time = 150;

        outcome button_event( const layout &l, button b, bool down, uint32_t now );
        // x and y from -1 to 1, y growing downward as SDL has it
        outcome stick_event( const layout &l, int stick, float x, float y, uint32_t now );
        // call often: the long press and the hold happen without an event
        outcome tick( const layout &l, uint32_t now );
        void reset();

        // a button that waits is down: the sticks belong to it
        bool holding() const {
            return holder != button::count;
        }
        button held_button() const {
            return holder;
        }
        // the button is held and has menus: its rings are on screen
        bool showing() const {
            return holder != button::count && state == phase::held;
        }
        // a stick is pushed in its ring
        bool ring_open() const {
            return ring_stick >= 0;
        }
        // 0 left, 1 right, -1 none
        int ring_stick_index() const {
            return ring_stick;
        }
        // the slot the stick points at, or -1
        int ring_slot() const {
            return slot;
        }
        // where the ring's stick is, -1 to 1
        float stick_x( int stick ) const {
            return sticks[stick].x;
        }
        float stick_y( int stick ) const {
            return sticks[stick].y;
        }

    private:
        enum class phase : int {
            waiting,
            held,
            done
        };
        struct stick_state {
            float x = 0.0f;
            float y = 0.0f;
            // was pushed past the high mark since it last was at the centre
            bool was_high = false;
            bool above_high = false;
            bool above_push = false;
            // when it first fell below each mark
            uint32_t left_high = 0;
            uint32_t left_push = 0;
        };
        button holder = button::count;
        phase state = phase::waiting;
        uint32_t down_at = 0;
        int ring_stick = -1;
        int slot = -1;
        std::array<stick_state, 2> sticks;

        // the hold time has passed for the waiting button
        outcome come_of_age( const layout &l );
        bool open_ring_if_pushed( const layout &l );
        void close_ring();
};

// The machine the game feeds (sdl_gamepad.cpp).
machine &state();

// An action for the play screen to run, set by the controller code and taken
// by input_context::handle_input.
void queue_action( const std::string &action );
bool has_queued_action();
// Empty if there is none.
std::string take_queued_action();

// Which of eight slots a stick position points at: 0 up, then clockwise.
int slot_at( float x, float y );

#if defined(TILES)
// A controller for tests, where there is none.  Point the CATA_PAD_SCRIPT
// environment variable at a file path: the game then has a virtual gamepad,
// and whenever that file appears it is read, deleted and played back.  Lines:
//   <milliseconds> button <A|B|X|Y|L1|R1|L2|R2|L3|R3|UP|DOWN|LEFT|RIGHT|SELECT|START> <down|up>
//   <milliseconds> stick <l|r> <x> <y>        x and y from -1 to 1, y downward
// script_wanted: the variable is set.  script_attach: makes the gamepad; call
// before the controllers are looked for.  script_play: call often.
bool script_wanted();
void script_attach();
void script_play();
#endif

} // namespace mobile_pad

#endif // CATA_SRC_MOBILE_PAD_H
