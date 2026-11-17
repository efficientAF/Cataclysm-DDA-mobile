#pragma once
#ifndef CATA_SRC_MOBILE_CRAFTING_H
#define CATA_SRC_MOBILE_CRAFTING_H

#include <string>
#include <utility>

#include "type_id.h"

class Character;
class recipe;
class temp_crafting_inventory;

// The crafting screen of the mobile UI fork, behind the MOBILE_MENUS option.
// It follows the UI canvas: search, category chips, a recipe list with what
// can be made first, and a detail pane (a bottom sheet in portrait) with the
// requirements and a Craft button.
//
// The recipes, the availability checks and the craft itself are upstream's.
namespace mobile_crafting
{

// True if this screen should replace upstream's crafting menu.
bool wanted();

// Stands in for upstream's select_crafter_and_crafting_recipe, with its
// arguments and its answer: every way into crafting (craft, long craft, a
// workbench, "view recipe", a camp) asks that function.  The crafter is the
// one passed in; choosing another crafter is not offered.
std::pair<Character *, const recipe *> select( int &batch_size_out, const recipe_id &goto_recipe,
        Character *crafter, const std::string &filter, bool camp,
        temp_crafting_inventory *inv_override );

// The next select() is for Character::long_craft: the title says so.
void next_is_long();

// Opens the screen for the avatar and starts the craft that was picked, if any.
void show();

} // namespace mobile_crafting

#endif // CATA_SRC_MOBILE_CRAFTING_H
