#include "mobile_vending.h"

#include <string>
#include <vector>

#include "mobile_list.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"

namespace mobile_vending
{

bool wanted()
{
    return mobile_list::wanted();
}

bool show( const std::vector<entry> &stock, const int money, int &selected )
{
    mobile_list::spec what;
    what.title = _( "Vending machine" );
    what.header = string_format( _( "Money left: %s" ), format_money( money ) );
    what.empty_text = _( "The vending machine is empty." );
    for( const entry &e : stock ) {
        mobile_list::row r;
        r.name = remove_color_tags( e.name );
        r.right = format_money( e.price );
        //~ A row of the vending machine. %d: how many of the item it holds
        r.detail = string_format( _( "%d in stock" ), e.count );
        r.description = e.info;
        r.enabled = e.price <= money;
        r.action = r.enabled ? _( "Buy" ) : _( "Can't afford" );
        what.rows.push_back( r );
    }
    int tab = 0;
    // back leaves no row selected; the caller's row must stay a valid one
    int open_on = selected;
    const int row = mobile_list::show( what, tab, open_on );
    if( row < 0 || row >= static_cast<int>( stock.size() ) ) {
        return false;
    }
    selected = row;
    return true;
}

} // namespace mobile_vending
