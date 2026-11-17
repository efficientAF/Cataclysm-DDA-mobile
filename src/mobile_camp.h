#pragma once
#ifndef CATA_SRC_MOBILE_CAMP_H
#define CATA_SRC_MOBILE_CAMP_H

#include <map>
#include <string>

#include "coords_fwd.h"

class basecamp;
class mission_data;
class npc;
enum class aim_rule : int;
enum class cbm_recharge_rule : int;
enum class cbm_reserve_rule : int;
enum class combat_engagement : int;

// Camps and followers on the mobile UI's list screen (mobile_list.h): the
// mission list of a camp or a companion, who works at a camp and at what, and
// the rules a follower keeps.  Each stands in for one of upstream's screens and
// changes the game through the functions that screen calls.  All of them are
// only called while mobile_list::wanted().
namespace mobile_camp
{

// In place of the list of talk_function::display_and_choose_opts.  Leaves the
// mission that was chosen in mission_key.cur_key, or upstream's "NONE" entry if
// the player went back.
void choose_mission( mission_data &mission_key, const tripoint_abs_omt &omt_pos,
                     const std::string &role_id, const std::string &title );

// In place of basecamp::worker_assignment_ui.
void assign_workers( basecamp &camp );
// In place of basecamp::job_assignment_ui.
void assign_jobs( basecamp &camp );

// In place of follower_rules_ui's window.  The maps are upstream's, from
// npctalk_rules.cpp: each value of a rule and the snippet that describes it.
void follower_rules( npc &guy, const std::map<combat_engagement, std::string> &engagement,
                     const std::map<aim_rule, std::string> &aim,
                     const std::map<cbm_recharge_rule, std::string> &recharge,
                     const std::map<cbm_reserve_rule, std::string> &reserve );

} // namespace mobile_camp

#endif // CATA_SRC_MOBILE_CAMP_H
