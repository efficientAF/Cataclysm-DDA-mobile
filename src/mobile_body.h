#pragma once
#ifndef CATA_SRC_MOBILE_BODY_H
#define CATA_SRC_MOBILE_BODY_H

#include <string>
#include <utility>
#include <vector>

class Character;
class body_status_ui;

// Screens about the character's body for the mobile UI, each a list with a
// detail panel (mobile_list.h): body status and medical, morale, sort armor and
// the blood test.  What they show is read from upstream's own classes and the
// buttons call upstream's functions.
namespace mobile_body
{

// Body status: the tabs Body parts, Medical and Stats.  In place of the loop of
// body_status_ui::display; returns what that returns (a wound was treated).
bool body_status( body_status_ui &ui );

// Sort armor: what is worn, innermost first, on a tab for each body part.  In
// place of outfit::sort_armor.
void sort_armor( Character &guy );

// What player_morale::display has worked out.
struct morale_data {
    // name and share (percent) of each source, largest first
    std::vector<std::pair<std::string, int>> positive;
    std::vector<std::pair<std::string, int>> negative;
    int total_positive = 0;
    int total_negative = 0;
    int total = 0;
    // percent that all morale is scaled to
    int modifier = 100;
    // upstream's words for that line
    std::string modifier_label;
    int pain_penalty = 0;
    int sleepiness_penalty = 0;
    int focus_equilibrium = 0;
};
// Morale.  In place of the window of player_morale::display.
void morale( const morale_data &data );

// The blood test's lines, and for each whether it is good news.  In place of
// the window of Character::conduct_blood_analysis.
void blood_test( const std::vector<std::string> &lines, const std::vector<bool> &good );

} // namespace mobile_body

#endif // CATA_SRC_MOBILE_BODY_H
