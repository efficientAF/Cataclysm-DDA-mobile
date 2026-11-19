#pragma once
#ifndef CATA_SRC_MOBILE_LIST_H
#define CATA_SRC_MOBILE_LIST_H

#include <string>
#include <vector>

// Pattern C of the remaining screens plan: a screen with tabs across the top, a
// list, and a panel for the selected row with its description and its one
// action as a button.  Bionics, mutations, spells, fighting styles and missions
// are this shape; each fills a spec from the game and acts on what comes back
// with upstream's own functions.
namespace mobile_list
{

struct row {
    std::string name;
    // a muted second line
    std::string detail;
    // short text at the right edge, e.g. "On"
    std::string right;
    // which tab it is on; an index into spec::tabs, ignored if there are none
    int tab = 0;
    // shown in the panel when the row is selected; may carry color tags
    std::string description;
    // a picture shown in the panel over the description, as a path under
    // data/mobile_ui/ (see mobile_ui::picture); empty for none
    std::string picture;
    // the label of the panel's button, or empty for none
    std::string action;
    // further buttons beside it, for rows with more than one thing to do
    std::vector<std::string> more_actions;
    // a row that is dimmed; it can still be selected and read
    bool enabled = true;
    // marks the one in use, e.g. the current fighting style
    bool current = false;
};

struct spec {
    std::string title;
    // one line under the title, e.g. "Power: 120 / 400 kJ"
    std::string header;
    std::vector<std::string> tabs;
    std::vector<row> rows;
    // what to say on a tab without rows
    std::string empty_text;
    // buttons for the screen as a whole, in a bar at the bottom ("Add zone")
    std::vector<std::string> buttons;
};

// What the player did on the screen.
struct outcome {
    // the row whose button was tapped, or -1
    int row = -1;
    // which of the row's buttons: 0 for action, 1 and up for more_actions
    int action = 0;
    // the screen's button that was tapped, or -1
    int button = -1;
    bool went_back() const {
        return row < 0 && button < 0;
    }
};

// True if these screens should replace upstream's.
bool wanted();

// Shows the screen and waits.  Returns the index of the row whose button was
// tapped, or -1 if the player went back.  tab and selected say where to open
// and receive where the player was, so that a caller can show the screen again
// after acting.
int show( const spec &what, int &tab, int &selected );
// The same for screens with several buttons.
outcome show_all( const spec &what, int &tab, int &selected );

} // namespace mobile_list

#endif // CATA_SRC_MOBILE_LIST_H
