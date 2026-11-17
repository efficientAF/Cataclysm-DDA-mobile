#pragma once
#ifndef CATA_SRC_MOBILE_MOVE_H
#define CATA_SRC_MOBILE_MOVE_H

// Moving items between places next to you (pattern H of the remaining screens
// plan), in place of upstream's two-pane advanced inventory: say where from,
// tick what to move, say where to.  Three steps, each a sheet or the item
// picker.  What you carry is not a source here: dropping has its own action.
namespace mobile_move
{

// True if this flow should replace the advanced inventory.
bool wanted();

// Runs the flow once.  The move itself is upstream's activity.
void show();

} // namespace mobile_move

#endif // CATA_SRC_MOBILE_MOVE_H
