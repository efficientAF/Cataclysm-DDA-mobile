#pragma once
#ifndef CATA_SRC_MOBILE_DIALOG_H
#define CATA_SRC_MOBILE_DIALOG_H

#include <string>
#include <vector>

// Upstream's small popups and list menus in the look of the UI canvas, so they
// do not stand out between the mobile screens.  popup.cpp and uilist.cpp keep
// their logic and input handling and only hand the drawing over to this.
namespace mobile_dialog
{

// Flags for the ImGui window the dialogs draw in; it must cover the screen.
int window_flags();

// --- Questions and messages (query_popup) ---

struct popup_spec {
    std::string message;
    // one button per choice; empty for a plain message
    std::vector<std::string> buttons;
    // the choice the keyboard is on, or -1
    int selected = -1;
    // a note at the top of the screen that leaves the middle free
    bool on_top = false;
    // with no buttons: waits for a key or for cancel, so it gets an OK button
    bool waits = false;
    // a tap outside the card counts
    bool can_cancel = false;
};

constexpr int tap_nothing = -1;
constexpr int tap_outside = -2;
constexpr int tap_ok = -3;

// Draws the dialog into the current full-screen window.  Returns the index of
// the button that was tapped, tap_ok, tap_outside or tap_nothing.
int draw_popup( const popup_spec &spec );

// --- List menus (uilist) ---

// Upstream pads the text of some menus into columns with a '|' between them
// ("Store in inventory     | 120").  Returns what is before the divider and
// puts what follows it into rest, both without the padding.  Text without such a
// divider is returned as it is.
std::string split_column( const std::string &text, std::string &rest );

struct list_row {
    std::string text;
    // a muted second line
    std::string detail;
    // shown on the right
    std::string hotkey;
    // disabled rows are dimmed
    bool enabled = true;
    bool tappable = true;
};

struct list_spec {
    std::string title;
    std::vector<list_row> rows;
    // the row the keyboard is on, or -1
    int selected = -1;
    // set when the keyboard moved the selection, to scroll it into view
    bool scroll_to_selected = false;
    bool can_cancel = true;
};

// Draws the menu as a bottom sheet into the current full-screen window.
// Returns the index of the row that was tapped, tap_outside or tap_nothing.
int draw_list( const list_spec &spec );

} // namespace mobile_dialog

#endif // CATA_SRC_MOBILE_DIALOG_H
