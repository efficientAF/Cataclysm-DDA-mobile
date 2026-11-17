#pragma once
#ifndef CATA_SRC_MOBILE_HELP_H
#define CATA_SRC_MOBILE_HELP_H

// The help screen of the mobile UI: the help categories as a list, and each as
// a page to scroll through.  The texts are upstream's (help.h).
namespace mobile_help
{

// True if the screen should replace upstream's.
bool wanted();

// Shows the list and waits until the player leaves it.
void show();

} // namespace mobile_help

#endif // CATA_SRC_MOBILE_HELP_H
