#pragma once
#ifndef CATA_SRC_MOBILE_CONTROLS_H
#define CATA_SRC_MOBILE_CONTROLS_H

#include <string>
#include <vector>

// The controller screens.  "Controller" (show) is a picture of the controller:
// tap a control to see and change its press action, its long press and the
// radial menus on the sticks while it is held (the layout of mobile_pad.h,
// for the play screen).  From it: the action picker, the radial menu editor,
// and "Other screens", a plain list of upstream's gamepad bindings per input
// context (menus, lists, the map), which that layout does not touch.
// Reached from the Inputs page of the options screen.
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

// The name of one of upstream's buttons for people: "A", "L2 + X", "Right stick up".
std::string button_name( int code );

// Shows the controller page and waits until it is closed.
void show();

// Draws the open radial menu over the play screen, if one is open.  Called at
// the end of every UI frame (ui_manager.cpp).
void draw_ring();

} // namespace mobile_controls

#endif // CATA_SRC_MOBILE_CONTROLS_H
