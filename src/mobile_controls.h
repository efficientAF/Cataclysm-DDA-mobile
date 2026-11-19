#pragma once
#ifndef CATA_SRC_MOBILE_CONTROLS_H
#define CATA_SRC_MOBILE_CONTROLS_H

#include <string>
#include <vector>

// Controller buttons: which button of a controller does what, per input
// context (the play screen, the inventory, the map...).  Reached from the
// Inputs page of the options screen.  The bindings are upstream's (gamepad
// events in the keybindings); this is a touch screen for looking at them and
// changing them, since upstream's keybindings window is hidden on mobile.
namespace mobile_controls
{

// One action of an input context and the controller buttons bound to it.
struct binding {
    // the input context, e.g. "DEFAULTMODE"
    std::string category;
    std::string action;
    // the action's name as the keybindings show it
    std::string name;
    // button codes (JOY_* in input.h) bound now, and by default
    std::vector<int> buttons;
    std::vector<int> defaults;
};

// Every action of every context that has, or by default had, a controller
// button, and every action of the contexts that have any.  Defined in
// input_context.cpp, which can see the input manager's lists.
std::vector<binding> bindings();
// Gives the action exactly these buttons in its context.  A button that
// another action of the same context had is taken from it; returns the names
// of the actions that lost one.
std::vector<std::string> set_buttons( const std::string &category, const std::string &action,
                                      const std::vector<int> &buttons );
// Writes the keybindings file.
void save();

// The name of a button for people: "A", "L1 + X", "Right stick up".
std::string button_name( int code );

// Shows the screen and waits until it is closed.
void show();

} // namespace mobile_controls

#endif // CATA_SRC_MOBILE_CONTROLS_H
