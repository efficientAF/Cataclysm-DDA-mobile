#pragma once
#ifndef CATA_SRC_MOBILE_SYSTEM_H
#define CATA_SRC_MOBILE_SYSTEM_H

#include <string>
#include <vector>

struct uilist_entry;

// The small "system" screens of the mobile UI: the distraction manager and
// the auto notes manager as list screens (mobile_list.h), the loading screen
// in the canvas's look, and what the action menus leave out on a phone.  The
// auto notes manager is auto_note_manager_gui::mobile_show, a member, because
// its caches are private.
namespace mobile_system
{

// One line of upstream's distraction manager.
struct distraction {
    std::string name;
    std::string description;
    // the setting itself
    bool *state = nullptr;
    // "Toggle all": flipping it sets every other one to its value
    bool is_toggle = false;
};

// The distraction manager.  Flips the states in place, as upstream's does.
void distractions( const std::vector<distraction> &list );

// Takes out of an action menu what is of no use by touch: swapping inventory
// letters, the sidebar panel manager, the colour manager and the keybindings.
// Does nothing unless the mobile screens are on.
void drop_desktop_actions( std::vector<uilist_entry> &entries );

// The loading screen: title, a bar, the step, a tip.  To be called inside
// loading_ui's ImGui frame.  Counts the steps shown since loading_done() to
// fill the bar, against the number the last load took.
void draw_loading( const std::string &context, const std::string &step, const std::string &tip );
// A step is about to be shown.
void loading_step();
// The loading screen went away.
void loading_done();

} // namespace mobile_system

#endif // CATA_SRC_MOBILE_SYSTEM_H
