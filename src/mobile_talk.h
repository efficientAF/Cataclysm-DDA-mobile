#pragma once
#ifndef CATA_SRC_MOBILE_TALK_H
#define CATA_SRC_MOBILE_TALK_H

#include <string>
#include <vector>

// Conversation on the phone (pattern D of the remaining screens plan): what was
// said scrolls in the upper part, the replies are full-width rows under it,
// within reach of a thumb.  Upstream's dialogue window (dialogue_imgui.cpp)
// keeps the conversation itself and calls in here to be drawn.
namespace mobile_talk
{

struct message {
    // packed RGBA, as ImGui takes it
    unsigned int color = 0;
    std::string text;
};

// True if this layout should replace upstream's.
bool wanted();

// Draws the conversation into the current ImGui window, which fills the screen.
// name is who you are talking to and about a line on them.  Returns the index of
// the reply that was tapped, or -1.  Leaves room at the bottom for the prompt
// bar, which carries the other things one can do (look at, size up, ...).
int draw( const std::string &name, const std::string &about, const std::vector<message> &history,
          const std::vector<std::string> &replies );

} // namespace mobile_talk

#endif // CATA_SRC_MOBILE_TALK_H
