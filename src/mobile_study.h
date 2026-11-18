#pragma once
#ifndef CATA_SRC_MOBILE_STUDY_H
#define CATA_SRC_MOBILE_STUDY_H

#include <map>
#include <set>
#include <string>

#include "study_zone_ui.h"
#include "type_id.h"

// The study zone's skill picker of the mobile UI fork: which skills each
// follower studies in the zone.  Upstream's is a table of checkboxes, one
// column per follower; here each follower is a tab of a mobile_list and each
// skill a row that is switched with its button.
namespace mobile_study
{

// Shows the screen and waits.  Changes npc_skill_preferences as upstream's
// query_study_zone_skills does, and returns what it would.
study_zone_ui_result query( std::map<std::string, std::set<skill_id>> &npc_skill_preferences );

} // namespace mobile_study

#endif // CATA_SRC_MOBILE_STUDY_H
