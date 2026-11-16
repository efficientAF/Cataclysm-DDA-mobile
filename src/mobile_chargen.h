#pragma once
#ifndef CATA_SRC_MOBILE_CHARGEN_H
#define CATA_SRC_MOBILE_CHARGEN_H

class Character;
class character_creator_ui;
struct character_creator_uistate;

// The character creation screen of the mobile UI fork, behind the MOBILE_MENUS
// option.  It follows the "New character" screen of the UI canvas: an app bar
// with the points left, seven steps, a scrolling body and Back / Next.
//
// The screen only draws and reports taps.  Every change goes through upstream's
// creator in newcharacter.cpp: its state (character_creator_uistate), its
// uilist callback and its handle_action().
namespace mobile_chargen
{

// Runs the screen in place of character_creator_ui::display().  Like it, the
// result is left in the creator's state: finished_character_creator or
// quit_to_main_menu.
void run( character_creator_ui &ccui );

// --- Forwarding functions, defined in newcharacter.cpp ---

// The creator's file-local state.
character_creator_uistate &upstream_state();
// Points of the stat, trait and skill pools that are not spent; negative when
// overspent.
int upstream_points_left( const Character &u );
// Whether starting clothes are the male set.
bool upstream_outfit_male();

} // namespace mobile_chargen

#endif // CATA_SRC_MOBILE_CHARGEN_H
