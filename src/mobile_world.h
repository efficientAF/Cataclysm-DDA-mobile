#pragma once
#ifndef CATA_SRC_MOBILE_WORLD_H
#define CATA_SRC_MOBILE_WORLD_H

#include <functional>
#include <string>
#include <vector>

#include "type_id.h"

class mod_ui;
struct WORLD;

// World creation and mod selection of the mobile UI (pattern G of the remaining
// screens plan, "settings-style lists").  They stand in for the screens of
// worldfactory.cpp; the world, its options, the mod manager's checks and the
// questions asked on the way are upstream's.
namespace mobile_world
{

// True if these screens should replace upstream's.
bool wanted();

// What the new world screen needs from worldfactory, which keeps these private.
struct hooks {
    // worldfactory::pick_random_name
    std::function<std::string()> random_name;
    // the warning about missing recommended mods; false means "do not create yet"
    std::function<bool()> mods_ok;
    // opens the mod selection for this world
    std::function<void()> edit_mods;
    // opens the world's own options (worldfactory::show_worldgen_tab_options)
    std::function<void()> edit_options;
    // longest name the name field takes
    int max_name_length = 32;
};

// The new world screen: name, the world option sliders, mods, all options,
// "Create world".  Returns what worldfactory::show_worldgen_basic returns:
// 1 to create the world, -999 if the player backed out.
int new_world( WORLD &world, const hooks &upstream );

// The mod selection: tabs Active and Available.  Changes active through the
// mod manager's own try_add, try_rem and try_shift.
void edit_mods( std::vector<mod_id> &active, mod_ui &ui );

// The mods of an existing world, to read only.
void show_mods( const std::vector<mod_id> &active, mod_ui &ui );

} // namespace mobile_world

#endif // CATA_SRC_MOBILE_WORLD_H
