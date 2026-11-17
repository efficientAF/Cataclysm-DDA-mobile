#pragma once
#ifndef CATA_SRC_MOBILE_TERMINAL_H
#define CATA_SRC_MOBILE_TERMINAL_H

#include <string>
#include <utility>
#include <vector>

// A computer terminal as a page of the mobile UI: what the computer prints, in
// a monospace face, and under it the buttons for what it asks: its menu,
// yes / no, or Continue where upstream waits for any key.  computer_session
// keeps doing everything else; it tells the page what it prints.
namespace mobile_terminal
{

// True if the page should replace upstream's terminal window.
bool wanted();

// The page shows while one of these lives.
class session
{
    public:
        explicit session( const std::string &name );
        ~session();
        session( const session & ) = delete;
        session &operator=( const session & ) = delete;
};

// True while a session lives.
bool active();

// What computer_session prints.  text may carry color tags and newlines.
void print( int indent, const std::string &text );
void newline();
void clear();

// "Press any key": a Continue button.
void wait_any();

enum class answer : int {
    yes,
    no,
    quit,
};
// Yes and No buttons; back is quit.
answer ask_yes_no();

// The computer's menu: one button for each option; second is whether it can
// be chosen.  Returns the index, or -1 for back (log off).
int choose( const std::string &title, const std::vector<std::pair<std::string, bool>> &options );

} // namespace mobile_terminal

#endif // CATA_SRC_MOBILE_TERMINAL_H
