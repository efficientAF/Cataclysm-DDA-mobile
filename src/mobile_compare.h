#pragma once
#ifndef CATA_SRC_MOBILE_COMPARE_H
#define CATA_SRC_MOBILE_COMPARE_H

#include <string>

// The result of comparing two items, as a page of the mobile UI: the two names
// as a switch in portrait, or the two items side by side in landscape, each
// with upstream's info text and its colors for better and worse values.
// Also the "Attach modification?" and "Remove modification?" questions, which
// upstream asks with the same window.
namespace mobile_compare
{

// True if the page should replace upstream's two-column window.
bool wanted();

// Shows the page and waits.  The texts may carry color tags.  With a question,
// the page has Confirm and Cancel buttons; returns true only for Confirm.
bool show( const std::string &first_name, const std::string &first_text,
           const std::string &second_name, const std::string &second_text,
           const std::string &question );

} // namespace mobile_compare

#endif // CATA_SRC_MOBILE_COMPARE_H
