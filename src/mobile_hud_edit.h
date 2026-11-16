#pragma once
#ifndef CATA_SRC_MOBILE_HUD_EDIT_H
#define CATA_SRC_MOBILE_HUD_EDIT_H

// Edit HUD mode of the mobile UI fork, following the UI canvas: with the game
// paused, drag the pieces of the HUD to where you want them, tap one to change
// its options, drag a drawer's handle along or to another edge.  The layout
// of the orientation the phone is held in is the one being edited.
namespace mobile_hud_edit
{

// True while a game is being played with the mobile HUD on.
bool available();

// Runs the editor over the map until Done or Cancel.  Done saves the layout
// to the config folder (see mobile_hud_layout.h).
void run();

#if defined(TILES)

// True if a point, in ImGui coordinates, is on the editor's own buttons and
// panels, which are tapped like any other mobile screen.
bool chrome_at( float x, float y );

// Fingers elsewhere on the screen, in window pixels; routed here by
// touch_input.cpp while the editor runs.
void finger_down( float x, float y );
void finger_motion( float x, float y );
void finger_up( float x, float y );
void finger_cancel();

#endif // TILES

} // namespace mobile_hud_edit

#endif // CATA_SRC_MOBILE_HUD_EDIT_H
