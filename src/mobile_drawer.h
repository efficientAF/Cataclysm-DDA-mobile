#pragma once
#ifndef CATA_SRC_MOBILE_DRAWER_H
#define CATA_SRC_MOBILE_DRAWER_H

enum action_id : int;

// The drawers of the mobile play screen, following the UI canvas: panels that
// come in from a screen edge over the game.
//
// Info has the tabs Body, Status, Gear and Area; Log has Log and Map.  Actions
// has every action of the game as a button, a tab for each kind, so that
// anything is two taps away: the handle, then the action.
namespace mobile_drawer
{

enum class drawer : int {
    info,
    log,
    actions,
};

// Opens the drawer over the game and waits until it is closed.  Returns
// ACTION_NULL, ACTION_MAP when "Open the full map" was tapped, or the action
// chosen in the Actions drawer.
action_id show( drawer which );

// The Log drawer opens on its Map tab the next time, not on the log.
void open_on_map();

// The handle of the Actions drawer was tapped: the next "action menu" input is
// that drawer and not the pause sheet (see handle_action.cpp).
void request_actions();
// True once after request_actions().
bool take_actions_request();

} // namespace mobile_drawer

#endif // CATA_SRC_MOBILE_DRAWER_H
