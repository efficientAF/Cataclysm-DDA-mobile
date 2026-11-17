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
#include <string>
#include <vector>

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

// --- For the Nearby drawer (mobile_drawer.h) ---

// One row: an item on or next to the avatar's tile, or a stack of them.
struct nearby_item {
    std::string name;
    // weight and where it lies
    std::string detail;
    std::string symbol;
    // packed RGBA, as ImGui takes it
    unsigned int color = 0;
    std::vector<item_location> locs;
};

// What can be picked up from here, in the order of the Nearby tab.
std::vector<nearby_item> nearby_items();

// Picks up what has room, as the screen's own "Pick up" does.  Returns what
// there is to say about things left behind, or an empty string.
std::string pick_up_now( const std::vector<item_location> &locs );

} // namespace mobile_inventory

#endif // CATA_SRC_MOBILE_INVENTORY_H
