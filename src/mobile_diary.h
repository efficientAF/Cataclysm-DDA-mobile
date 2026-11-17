#pragma once
#ifndef CATA_SRC_MOBILE_DIARY_H
#define CATA_SRC_MOBILE_DIARY_H

#include <string>
#include <utility>

// The diary of the mobile UI: a list screen (mobile_list.h) with one row per
// page, and a full-screen card with a field of many lines in place of
// upstream's text editor (string_editor_window).  The screen itself is
// diary::mobile_show, a member, because the pages are private.
namespace mobile_diary
{

// True if these should replace upstream's screens.
bool wanted();

// What the next text card is called, e.g. the date of the diary page.
void set_edit_title( const std::string &title );

// Shows the card and waits.  Returns whether Save was tapped, and the text as
// it was left, as string_editor_window::query_string does.
std::pair<bool, std::string> edit_text( const std::string &text );

// After death: the diary opens on the text card of a new page.
void write_first();
// True once after write_first().
bool take_write_first();

} // namespace mobile_diary

#endif // CATA_SRC_MOBILE_DIARY_H
