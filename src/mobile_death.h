#pragma once
#ifndef CATA_SRC_MOBILE_DEATH_H
#define CATA_SRC_MOBILE_DEATH_H

#include <optional>
#include <string>

// The death screen of the UI canvas: what killed you, a few numbers, the last
// messages, last words for the memorial file, and what to do next.  It stands
// in for upstream's tombstone, message log and scores screens.
namespace mobile_death
{

// Call at the moment of death, in place of the tombstone screen: remembers
// what killed the avatar, where and when, while that is still known.
void note_death();

// Shows the screen and waits for a choice.  Sends the avatar's death, with the
// last words, to the event bus, as the tombstone screen does.
void show();

// The world to make a new character in, if that was chosen on the death
// screen.  The main menu asks once, when it opens after the game is over.
std::optional<std::string> take_new_character_world();

} // namespace mobile_death

#endif // CATA_SRC_MOBILE_DEATH_H
