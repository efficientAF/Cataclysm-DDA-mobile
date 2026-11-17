#pragma once
#ifndef CATA_SRC_MOBILE_DESCRIBE_H
#define CATA_SRC_MOBILE_DESCRIBE_H

#include <string>
#include <vector>

// Look-around's "Describe" for phones: a full-screen page of text with a chip
// for each thing on the tile (creature, furniture, terrain, vehicle).  The
// text is upstream's, colors included; extended_description_window::show hands
// it over (its mobile_show member is defined in mobile_describe.cpp).
namespace mobile_describe
{

struct page {
    // the chip's label
    std::string name;
    // paragraphs as upstream's extended_description gives them: they may carry
    // color tags and line breaks, and "--" stands for a divider
    std::vector<std::string> parts;
};

// True if the page should replace upstream's window.
bool wanted();

// Shows the pages and waits until the player goes back.  first is the page to
// open on.
void show( const std::string &title, const std::vector<page> &pages, int first );

} // namespace mobile_describe

#endif // CATA_SRC_MOBILE_DESCRIBE_H
