#pragma once
#ifndef CATA_SRC_MOBILE_SCREENS_H
#define CATA_SRC_MOBILE_SCREENS_H

class avatar;

// Screens of the mobile UI that are a list with a detail panel (mobile_list.h):
// what the game has is read into the list, and the button calls upstream's own
// function for it.
namespace mobile_screens
{

// Installed bionics: turn them on and off.  In place of avatar::power_bionics.
void bionics( avatar &you );
// Traits and mutations: use the ones that can be used.  In place of
// avatar::power_mutations.
void mutations( avatar &you );
// Missions: read them and choose the one to follow.  In place of
// game::list_missions.
void missions( avatar &you );

} // namespace mobile_screens

#endif // CATA_SRC_MOBILE_SCREENS_H
