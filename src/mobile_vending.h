#pragma once
#ifndef CATA_SRC_MOBILE_VENDING_H
#define CATA_SRC_MOBILE_VENDING_H

#include <string>
#include <vector>

// The vending machine on the mobile UI's list screen (pattern C): a row for
// each kind of item with its price, the item's info in the panel, and a Buy
// button.  iexamine::vending keeps the stock, the money and the purchase.
namespace mobile_vending
{

struct entry {
    std::string name;
    // how many of it the machine holds
    int count = 0;
    // in cents
    int price = 0;
    // upstream's item info; may carry color tags
    std::string info;
};

// True if the list screen should replace upstream's two-pane window.
bool wanted();

// Shows the machine.  selected says which row to open on and receives the row
// the player chose.  Returns true for Buy, false for back.
bool show( const std::vector<entry> &stock, int money, int &selected );

} // namespace mobile_vending

#endif // CATA_SRC_MOBILE_VENDING_H
