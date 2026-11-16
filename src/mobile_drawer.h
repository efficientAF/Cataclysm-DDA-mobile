#pragma once
#ifndef CATA_SRC_MOBILE_DRAWER_H
#define CATA_SRC_MOBILE_DRAWER_H

enum action_id : int;

// The two drawers of the mobile play screen, following the UI canvas: panels
// that come in from a screen edge over the game.
//
// Info has the tabs Body, Status, Gear and Area; Log has Log and Map.
namespace mobile_drawer
{

enum class drawer : int {
    info,
    log,
};

// Opens the drawer over the game and waits until it is closed.  Returns
// ACTION_NULL, or ACTION_MAP when "Open the full map" was tapped.
action_id show( drawer which );

// The Log drawer opens on its Map tab the next time, not on the log.
void open_on_map();

} // namespace mobile_drawer

#endif // CATA_SRC_MOBILE_DRAWER_H
