#pragma once
#ifndef CATA_SRC_MOBILE_PICK_H
#define CATA_SRC_MOBILE_PICK_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "coords_fwd.h"

// Map pick mode of the mobile UI: questions that are answered by pointing at
// the map.  For now the one that is asked most: "which of the tiles next to
// you?" (close, examine, smash, butcher...), see choose_direction in action.cpp.
//
// The eight tiles around the player are shown as a ring of large buttons over
// the map, with the question and Cancel in a banner at the top.  Where the game
// knows which tiles qualify, only those can be tapped, and each says what is there.
namespace mobile_pick
{

// True if the ring should replace upstream's "(Direction button)" popup.
bool wanted();

// The tiles that qualify for the question that is about to be asked, around
// center.  Cleared again when it goes out of scope.
class candidates
{
    public:
        candidates( const tripoint_bub_ms &center, const std::vector<tripoint_bub_ms> &valid );
        ~candidates();
        candidates( const candidates & ) = delete;
        candidates &operator=( const candidates & ) = delete;
};

// Look-around on the phone: upstream's loop keeps running (game::look_around),
// with a banner at the top that says what is on the tile under the cursor in
// place of its text window.  A tap on the map moves the cursor there; a tap on
// the cursor's own tile confirms.  The banner takes no taps itself.
class look_banner
{
    public:
        look_banner();
        ~look_banner();
        look_banner( const look_banner & ) = delete;
        look_banner &operator=( const look_banner & ) = delete;
        // The tile the cursor is on now.
        void show( const tripoint_bub_ms &pos );
    private:
        class impl;
        std::unique_ptr<impl> window;
};

// Asks for a direction.  Returns the step from the player (zero for the
// player's own tile, above or below if allow_vertical), or nothing if cancelled.
// timeout is how often to redraw the map while waiting, in milliseconds; negative for never.
std::optional<tripoint_rel_ms> direction( const std::string &message, bool allow_vertical,
        int timeout );

} // namespace mobile_pick

#endif // CATA_SRC_MOBILE_PICK_H
