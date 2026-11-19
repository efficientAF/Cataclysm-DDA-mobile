#include "mobile_settings.h"

#if defined(TILES)
#include <cmath>
#include <string>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "game.h"
#include "mobile_hud_edit.h"
#include "mobile_list.h"
#include "mobile_ui.h"
#include "options.h"
#include "string_formatter.h"
#include "translations.h"

namespace mobile_settings
{

static bool edit_hud_wanted = false;

void request_edit_hud()
{
    edit_hud_wanted = true;
}

// Each kind of screen with its size; the list itself follows "List screens".
void show_screen_sizes()
{
    int tab = 0;
    int selected = -1;
    // start on the kind of screen that was open last, the likely one to change
    const std::string last = mobile_ui::last_screen_kind();
    while( true ) {
        const std::vector<mobile_ui::screen_kind> kinds = mobile_ui::screen_kinds();
        mobile_list::spec what;
        what.title = _( "Screen sizes" );
        what.header = _( "Steps of 5%.  A kind's size multiplies with \"Everything\"." );
        for( size_t i = 0; i < kinds.size(); ++i ) {
            mobile_list::row r;
            r.name = kinds[i].name;
            r.right = string_format( "%d%%", kinds[i].percent );
            r.description = kinds[i].covers;
            // what such a screen looks like, if there is a picture of one
            r.picture = "previews/" + kinds[i].id + ".png";
            r.action = _( "Smaller" );
            r.more_actions = { _( "Larger" ), _( "100%" ) };
            r.current = kinds[i].percent != 100;
            what.rows.push_back( r );
            if( selected < 0 && kinds[i].id == last ) {
                selected = static_cast<int>( i );
            }
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.row < 0 || static_cast<size_t>( did.row ) >= kinds.size() ) {
            return;
        }
        const mobile_ui::screen_kind &kind = kinds[static_cast<size_t>( did.row )];
        mobile_ui::set_screen_percent( kind.id, did.action == 0 ? kind.percent - 5 :
                                       did.action == 1 ? kind.percent + 5 : 100 );
    }
}

void show()
{
    const bool ingame = g != nullptr && !get_avatar().name.empty();
    const bool was_tiles = use_tiles;
    edit_hud_wanted = false;
    // upstream's options screen: mobile_options draws it, upstream compares,
    // saves and applies what was changed
    get_options().show( ingame );
    if( ingame && use_tiles != was_tiles ) {
        // Tiles and letters differ in size, so the map window is laid out
        // again.  A game started with letters has only checked the tileset,
        // not loaded it.
        if( use_tiles ) {
            g->reload_tileset();
        } else {
            g->reset_zoom();
            g->mark_main_ui_adaptor_resize();
        }
    }
    if( edit_hud_wanted ) {
        edit_hud_wanted = false;
        if( mobile_hud_edit::available() ) {
            // straight into the layout editor, over the map
            mobile_hud_edit::run();
        }
    }
}

} // namespace mobile_settings

#else // !TILES

namespace mobile_settings
{

void show() {}
void show_screen_sizes() {}
void request_edit_hud() {}

} // namespace mobile_settings

#endif // TILES
