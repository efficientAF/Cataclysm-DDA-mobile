#pragma once
#ifndef CATA_SRC_MOBILE_OPTIONS_H
#define CATA_SRC_MOBILE_OPTIONS_H

#include <string>

class options_manager;

// Pattern G of the remaining screens plan: the full options list as a phone
// screen.  A search field, upstream's option pages as chips, and one row per
// option with a control that fits its type: a switch, a stepper, or a tap that
// opens a choice sheet, the keypad or the text card.
//
// It only stands in for the "draw, read a key, change the option" loop of
// options_manager::show.  The options are changed in place with upstream's own
// setters; comparing with the old values, asking to save, saving and every
// side effect stay in options_manager::show.
namespace mobile_options
{

// Shows the screen and waits until it is left.  The arguments are those of
// options_manager::show.  Returns "" for the full list.  For the world options
// of world creation it returns what upstream's loop would: "QUIT", "PREV_TAB"
// or "NEXT_TAB".
std::string show( options_manager &options, bool ingame, bool world_options_only,
                  bool with_tabs );

} // namespace mobile_options

#endif // CATA_SRC_MOBILE_OPTIONS_H
