#pragma once
#ifndef CATA_SRC_MOBILE_CRAFTING_H
#define CATA_SRC_MOBILE_CRAFTING_H

// The crafting screen of the mobile UI fork, behind the MOBILE_MENUS option.
// It follows the UI canvas: search, category chips, a recipe list with what
// can be made first, and a detail pane (a bottom sheet in portrait) with the
// requirements and a Craft button.
//
// The recipes, the availability checks and the craft itself are upstream's.
namespace mobile_crafting
{

// Opens the screen for the avatar and starts the craft that was picked, if any.
void show();

} // namespace mobile_crafting

#endif // CATA_SRC_MOBILE_CRAFTING_H
