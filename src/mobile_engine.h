#pragma once
#ifndef CATA_SRC_MOBILE_ENGINE_H
#define CATA_SRC_MOBILE_ENGINE_H

#include <string>

// The smart engine controller's settings for phones: a switch, two steppers
// for the battery marks and the user manual under them.  The values are the
// vehicle's own (smart_controller_ui::control hands over its references), so
// they are kept the way upstream keeps them.
namespace mobile_engine
{

// True if the page should replace upstream's window.
bool wanted();

// Shows the page and waits until the player goes back.  The marks move in
// steps of five and keep five apart, as upstream's sliders do.
void smart_controller( bool &enabled, int &battery_lo, int &battery_hi,
                       const std::string &manual );

} // namespace mobile_engine

#endif // CATA_SRC_MOBILE_ENGINE_H
