#pragma once
#ifndef CATA_SRC_MOBILE_PICKER_H
#define CATA_SRC_MOBILE_PICKER_H

#include <string>

class inventory_multiselector;
class inventory_entry;
class inventory_selector;
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

// Lets the player choose one item.  Returns nowhere if they back out.  entry_out,
// if given, receives the selector's entry for the item.
item_location pick_one( inventory_selector &selector, inventory_entry **entry_out = nullptr );

// Lets the player tick items and say how many of each; the choice is put into
// the selector with toggle_entry.  Returns false if they back out.
bool pick_many( inventory_multiselector &selector );

// The item card: a full-screen page of what the game knows about an item, as
// upstream's item info window words it.  text may carry color tags.
void show_details( const std::string &name, const std::string &kind, const std::string &text );

} // namespace mobile_picker

#endif // CATA_SRC_MOBILE_PICKER_H
