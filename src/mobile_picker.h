#pragma once
#ifndef CATA_SRC_MOBILE_PICKER_H
#define CATA_SRC_MOBILE_PICKER_H

class inventory_multiselector;
class inventory_pick_selector;
class item_location;

// The item picker of the mobile UI: one screen for every "choose an item" menu
// (eat, wear, wield, read, use, drop...).  Upstream builds all of them from one
// class, inventory_selector, with a filter and a title; the picker shows the
// entries of such a selector, already filtered and sorted, in the look of the
// Inventory screen.  What can be chosen, and why something cannot, stays
// upstream's call.
namespace mobile_picker
{

// True if the picker should replace upstream's item menus.
bool wanted();

// Lets the player choose one item.  Returns nowhere if they back out.
item_location pick_one( inventory_pick_selector &selector );

// Lets the player tick items and say how many of each; the choice is put into
// the selector with toggle_entry.  Returns false if they back out.
bool pick_many( inventory_multiselector &selector );

} // namespace mobile_picker

#endif // CATA_SRC_MOBILE_PICKER_H
