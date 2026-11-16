#pragma once
#ifndef CATA_SRC_MOBILE_MENU_H
#define CATA_SRC_MOBILE_MENU_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

enum action_id : int;

// Screens of the mobile UI fork that replace upstream's menus, behind the
// MOBILE_MENUS option.  They follow the UI canvas.
//
// So far: the main menu and the new game screen.  The views only draw and report
// what was tapped; main_menu.cpp keeps deciding what each entry does.
namespace mobile_menu
{

bool enabled();

// The pause sheet of the play screen.  Returns the action to perform:
// ACTION_NULL to resume, ACTION_ACTIONMENU for upstream's full action menu.
action_id pause_sheet();

// The wait sheet: pick how long to wait, and start waiting.
void wait_sheet();

struct world_info {
    std::string name;
    // for example "2 characters"
    std::string detail;
};

struct main_menu_model {
    std::string version;
    std::string motd;
    std::string credits;
    // What Continue would load; the title is empty if there is nothing to continue.
    std::string continue_title;
    std::string continue_detail;
    std::vector<world_info> worlds;
    // names of the settings screens, in upstream's order
    std::vector<std::string> settings;
};

enum class choice_kind : int {
    continue_game,
    // index: 0 custom, 1 preset, 2 random, 3 play now (default scenario), 4 play now
    new_game,
    // index into worlds
    load_world,
    // index into worlds, or -1 to create one
    edit_world,
    tutorial,
    // index into settings
    settings,
    help,
};

struct choice {
    choice_kind kind = choice_kind::help;
    int index = 0;
};

class main_menu_view
{
    public:
        main_menu_view();
        ~main_menu_view();
        main_menu_view( const main_menu_view & ) = delete;
        main_menu_view &operator=( const main_menu_view & ) = delete;

        // Call before each redraw.
        void set_model( const main_menu_model &model );
        // What was tapped since the last call.
        std::optional<choice> take_choice();
        // Go back one step (close a sheet, leave the new game screen).  Returns
        // false if already on the main menu.
        bool back();
    private:
        class impl;
        std::unique_ptr<impl> pimpl;
};

} // namespace mobile_menu

#endif // CATA_SRC_MOBILE_MENU_H
