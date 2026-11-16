#pragma once
#ifndef CATA_SRC_MOBILE_INVENTORY_H
#define CATA_SRC_MOBILE_INVENTORY_H

// The inventory screen of the mobile UI fork, behind the MOBILE_MENUS option.
// It follows the "Inventory" screen of the UI canvas: one screen for what you
// carry, what you wear and what lies around you.
//
// The screen only shows items and asks what to do; every action is performed
// by upstream's own functions (wield, wear, drop, pick up, eat, ...).
#include <set>

#include "coordinates.h"
#include "item_location.h"

namespace mobile_inventory
{

enum class tab : int {
    carried,
    worn,
    nearby,
};

// Opens the screen on the given tab and returns when it is closed.  After an
// action that takes game time the screen closes, and opens again once the
// avatar can act.
void show( tab start = tab::carried );

// The screen as a pick-up menu, in place of upstream's: only what is at the
// given tiles (on the map and in vehicle cargo), or at and around the avatar
// if none are given.  Returns what was chosen; the caller picks it up.
drop_locations pick_up_from( const std::set<tripoint_bub_ms> &targets );

} // namespace mobile_inventory

#endif // CATA_SRC_MOBILE_INVENTORY_H
