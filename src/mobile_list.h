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
    // the label of the panel's button, or empty for none
    std::string action;
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
};

// True if these screens should replace upstream's.
bool wanted();

// Shows the screen and waits.  Returns the index of the row whose button was
// tapped, or -1 if the player went back.  tab and selected say where to open
// and receive where the player was, so that a caller can show the screen again
// after acting.
int show( const spec &what, int &tab, int &selected );

} // namespace mobile_list

#endif // CATA_SRC_MOBILE_LIST_H
