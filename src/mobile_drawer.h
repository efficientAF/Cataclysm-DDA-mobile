#pragma once
#ifndef CATA_SRC_MOBILE_DRAWER_H
#define CATA_SRC_MOBILE_DRAWER_H

#include <optional>

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
    // what lies on and around your tile; a tap picks it up
    nearby,
};

// Opens the drawer over the game and waits until it is closed.  Returns
// ACTION_NULL, ACTION_MAP when "Open the full map" was tapped, or the action
// chosen in the Actions drawer.
action_id show( drawer which );

// The Log drawer opens on its Map tab the next time, not on the log.
void open_on_map();

// The handle of the Actions or the Nearby drawer was tapped: the next "action
// menu" input is that drawer and not the pause sheet (see handle_action.cpp).
void request( drawer which );
// The drawer asked for, once.
std::optional<drawer> take_request();

} // namespace mobile_drawer

#endif // CATA_SRC_MOBILE_DRAWER_H
