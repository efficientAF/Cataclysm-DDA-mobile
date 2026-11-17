#pragma once
#ifndef CATA_SRC_MOBILE_ENTRY_H
#define CATA_SRC_MOBILE_ENTRY_H

#include <optional>
#include <string>
#include <vector>

// Entry sheets of the mobile UI: asking the player for a number or a line of
// text, in place of upstream's text field (string_input_popup, input_popup).
//
// A number gets a sheet at the bottom with a keypad of its own, so no system
// keyboard covers half the screen.  Text gets a card at the top with one
// field, above the system keyboard, and what was entered before as chips.
namespace mobile_entry
{

struct spec {
    // the question, e.g. "Wait how long?"
    std::string title;
    // more about it, shown smaller
    std::string description;
    // what the field starts with
    std::string text;
    // digits only: the keypad instead of the keyboard
    bool number = false;
    // with number: a decimal point may be entered
    bool decimal = false;
    // most characters to accept, or -1
    int max_length = -1;
    // earlier answers, oldest first
    std::vector<std::string> history;
};

// True if the sheet should replace upstream's field.
bool wanted();

// Shows the sheet and waits.  Returns what was entered, or nothing if cancelled.
std::optional<std::string> query( const spec &what );

// Appends a keypad key to a number being entered: a digit, '.' for the decimal
// point, '-' to flip the sign or '<' to delete.  fresh says the text is still
// the suggested one, which the first digit replaces.  Kept apart from the
// drawing so that it can be tested.
std::string keypad_press( const std::string &text, char key, bool fresh, bool decimal,
                          int max_length );

} // namespace mobile_entry

#endif // CATA_SRC_MOBILE_ENTRY_H
