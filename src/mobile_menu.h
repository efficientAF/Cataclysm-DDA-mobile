#pragma once
#ifndef CATA_SRC_MOBILE_MENU_H
#define CATA_SRC_MOBILE_MENU_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Touch-friendly menus of the mobile UI fork, behind the MOBILE_MENUS option.
//
// So far this is the main menu from the UI canvas: large buttons instead of the
// row of text hotkeys.  It only draws and reports taps; main_menu.cpp keeps
// deciding what each entry does.
namespace mobile_menu
{

bool enabled();

// One main menu button.  If it has sub-entries, tapping it opens them as a list.
struct menu_group {
    // the caller's number for this entry, handed back by take_choice
    int id = 0;
    std::string label;
    std::vector<std::string> sub_entries;
};

class main_menu_view
{
    public:
        main_menu_view();
        ~main_menu_view();
        main_menu_view( const main_menu_view & ) = delete;
        main_menu_view &operator=( const main_menu_view & ) = delete;

        // The buttons to show, in main menu order.  Call before each redraw.
        void set_groups( const std::vector<menu_group> &groups, const std::string &version );
        // The entry tapped since the last call: the id of its group and the
        // index of the sub-entry (0 if the group has none).
        std::optional<std::pair<int, int>> take_choice();
    private:
        class impl;
        std::unique_ptr<impl> pimpl;
};

} // namespace mobile_menu

#endif // CATA_SRC_MOBILE_MENU_H
