#pragma once
#ifndef CATA_SRC_MOBILE_PROMPT_H
#define CATA_SRC_MOBILE_PROMPT_H

#include <string>
#include <vector>

class input_context;
struct input_event;

// Prompt buttons of the mobile UI fork: named on-screen buttons for the keys the
// game is waiting for, in the look of the UI canvas.  They take the place of
// upstream's Android shortcut bar, which shows one character per key.
//
// The buttons come from two general sources, not from a list of prompts:
// - Key hints.  A note on screen that names keys ("ESC to go back.  RETURN to
//   move anyway.") is matched against the bindings of the input context that
//   is waiting; each hint becomes a button named after its text ("Go back",
//   "Move anyway") and the keys are taken out of the note.
// - The waiting input context.  On upstream's own screens every registered
//   action that has a key gets a button with the action's name; what does not
//   fit in one row is behind "More".
//
// Nothing shows on the map (unless a note has key hints, as on the death
// camera) or over the mobile screens, which have their own controls.
namespace mobile_prompt
{

// --- Pure helpers (covered by tests/mobile_prompt_test.cpp) ---

struct bound_action {
    std::string action;
    // the keys as a note would name them, see input_context::get_desc
    std::string keys;
    // the name to use if the note says nothing more than the keys
    std::string name;
};

struct key_hint {
    std::string action;
    std::string label;
};

struct hint_result {
    std::vector<key_hint> hints;
    // the message without the lines that had key hints
    std::string remaining;
};

// Turns the text around a key hint into a button label: "to go back." gives
// "Go back".  Empty if there is nothing to use.
std::string label_from_hint( const std::string &text );

// Finds the key hints of a message, line by line.
hint_result parse_key_hints( const std::string &message,
                             const std::vector<bound_action> &actions );

// --- The bar ---

// The MOBILE_MENUS option.
bool enabled();

// Remembers a key list that was handed out for a message, see
// input_context::get_desc.  Only these are looked for in notes.
void note_keys( const std::string &action, const std::string &keys );

// Call while drawing a note that does not take input itself.  The key hints
// in it become buttons for this frame; returns what is left of the message.
std::string take_key_hints( const std::string &message );

// A screen with touch controls of its own keeps one of these next to its input
// context: no bar while that context is the one waiting.
class own_controls
{
    public:
        explicit own_controls( const input_context &ctxt );
        ~own_controls();
        own_controls( const own_controls & ) = delete;
        own_controls &operator=( const own_controls & ) = delete;
    private:
        const input_context *ctxt;
};

#if defined(TILES)

// Draws the bar over the finished UI frame.  Called at the end of every ImGui
// frame (ui_manager.cpp).
void draw();

// Fingers, in ImGui coordinates; routed here by touch_input.cpp.
// Returns true if the bar takes the finger.
bool finger_down( float x, float y );
void finger_motion( float x, float y );
// Returns true if it set out to an input to deliver.
bool finger_up( float x, float y, input_event &out );
void finger_cancel();

#endif // TILES

} // namespace mobile_prompt

#endif // CATA_SRC_MOBILE_PROMPT_H
