#pragma once
#ifndef CATA_SRC_MOBILE_RULES_H
#define CATA_SRC_MOBILE_RULES_H

#include <string>
#include <tuple>
#include <vector>

#include "color.h"

namespace auto_pickup
{
class user_interface;
} // namespace auto_pickup

// Rule lists and score lists of the mobile UI, as list screens (mobile_list.h).
// The lists themselves, saving and testing stay upstream's.  The safe mode
// manager is safemode::mobile_show, a member, because its rules are private.
namespace mobile_rules
{

// The auto pickup manager.  Works on the tabs' new_rules and sets
// bStuffChanged, then asks to save, as auto_pickup::user_interface::show does.
void pickup_rules( auto_pickup::user_interface &ui );

// What a rule matches: a list to read.  note is upstream's remark on the list.
void show_matches( const std::string &title, const std::vector<std::string> &names,
                   const std::string &note );

// What upstream's scores window has collected for its four tabs.
struct score_lists {
    // false while achievements are switched off (debug characters)
    bool achievements_enabled = true;
    // one entry per achievement: its name, then lines about it
    std::vector<std::string> achievements;
    std::vector<std::string> conducts;
    std::vector<std::string> scores;
    // kills, symbol, color, name
    std::vector<std::tuple<int, std::string, nc_color, std::string>> monster_kills;
    std::vector<std::string> npc_kills;
};

// Achievements, conducts, scores and kills, in place of upstream's window.
void scores( const score_lists &lists );

} // namespace mobile_rules

#endif // CATA_SRC_MOBILE_RULES_H
