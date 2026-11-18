#include "mobile_system.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "action.h"
#include "auto_note.h"
#include "color.h"
#include "input_popup.h"
#include "map_extras.h"
#include "mobile_list.h"
#include "options.h"
#include "output.h"
#include "string_formatter.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"

#if defined(TILES)
#include "imgui/imgui.h"
#include "mobile_ui.h"
#endif

namespace mobile_system
{

void distractions( const std::vector<distraction> &list )
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        mobile_list::spec what;
        what.title = _( "Distractions" );
        what.header = _( "What may interrupt what you are doing." );
        // which entry of list each row is
        std::vector<size_t> where;
        size_t toggle = list.size();
        for( size_t i = 0; i < list.size(); ++i ) {
            const distraction &d = list[i];
            if( d.is_toggle ) {
                toggle = i;
                continue;
            }
            mobile_list::row r;
            r.name = d.name;
            r.right = *d.state ? _( "On" ) : _( "Off" );
            r.current = *d.state;
            r.enabled = *d.state;
            r.description = d.description;
            r.action = *d.state ? _( "Turn off" ) : _( "Turn on" );
            what.rows.push_back( r );
            where.push_back( i );
        }
        if( toggle < list.size() ) {
            what.buttons = { std::string( *list[toggle].state ? _( "Turn all off" ) : _( "Turn all on" ) ) };
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            break;
        }
        if( did.button == 0 && toggle < list.size() ) {
            // as upstream: the switch flips, and everything follows it
            *list[toggle].state ^= true;
            for( const distraction &d : list ) {
                *d.state = *list[toggle].state;
            }
        } else if( did.row >= 0 && static_cast<size_t>( did.row ) < where.size() ) {
            *list[where[static_cast<size_t>( did.row )]].state ^= true;
        }
    }
}

void drop_desktop_actions( std::vector<uilist_entry> &entries )
{
    if( !mobile_list::wanted() ) {
        return;
    }
    entries.erase( std::remove_if( entries.begin(), entries.end(), []( const uilist_entry & entry ) {
        // letters are neither shown nor typed on a phone; the sidebar is hidden
        // under the HUD; the colour manager and the keybindings are keyboard screens
        return entry.retval == ACTION_ORGANIZE || entry.retval == ACTION_TOGGLE_PANEL_ADM ||
               entry.retval == ACTION_COLOR || entry.retval == ACTION_KEYBINDINGS;
    } ), entries.end() );
}

#if defined(TILES)

using mobile_ui::face;

// steps shown since the loading screen came up
static int steps_shown = 0;
// how many the last load took; before the first, about what a new game takes
static int steps_expected = 190;

void loading_step()
{
    steps_shown++;
}

void loading_done()
{
    if( steps_shown > 20 ) {
        steps_expected = steps_shown;
    }
    steps_shown = 0;
}

void draw_loading( const std::string &context, const std::string &step, const std::string &tip )
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ), mobile_ui::col_bg );

    const float avail = size.x - safe.x - safe.z - 48.0f * u;
    const float width = std::min( avail, 420.0f * u );
    const float left = origin.x + safe.x + ( size.x - safe.x - safe.z - width ) * 0.5f;

    const std::string name = "CATACLYSM";
    const std::string sub = "DARK DAYS AHEAD";
    const float name_size = std::min( 58.0f, width / u / 5.2f );
    const ImVec2 name_px = mobile_ui::text_size( face::display, name_size, name );
    const ImVec2 sub_px = mobile_ui::text_size( face::display, 21.0f, sub );
    const std::string line = step.empty() ? context : context + "  ·  " + step;
    const float line_h = mobile_ui::text_size( face::body, 13.0f, "X" ).y;
    const float bar_h = 6.0f * u;
    const float block_h = name_px.y + 4.0f * u + sub_px.y + 30.0f * u + bar_h + 12.0f * u + line_h;
    float y = origin.y + safe.y + std::max( 16.0f * u,
                                            ( size.y - safe.y - safe.w - block_h ) * 0.42f );
    mobile_ui::text( draw, face::display, name_size, ImVec2( left, y ), mobile_ui::col_title, name );
    y += name_px.y + 4.0f * u;
    mobile_ui::text( draw, face::display, 21.0f, ImVec2( left, y ), mobile_ui::col_accent, sub );
    y += sub_px.y + 30.0f * u;

    // the bar never fills before the loading is over
    const float fraction = std::clamp( static_cast<float>( steps_shown ) /
                                       static_cast<float>( std::max( steps_expected, 1 ) ), 0.02f, 0.97f );
    draw->AddRectFilled( ImVec2( left, y ), ImVec2( left + width, y + bar_h ),
                         mobile_ui::col_fill_soft, bar_h * 0.5f );
    draw->AddRectFilled( ImVec2( left, y ), ImVec2( left + std::max( bar_h, width * fraction ),
                         y + bar_h ), mobile_ui::col_accent, bar_h * 0.5f );
    y += bar_h + 12.0f * u;

    // one line, cut at the right edge
    draw->PushClipRect( ImVec2( left, y ), ImVec2( left + width, y + line_h + 4.0f * u ), true );
    mobile_ui::text( draw, face::body, 13.0f, ImVec2( left, y ), mobile_ui::col_muted, line );
    draw->PopClipRect();

    // the tip, at the bottom, if there is room under the block
    if( !tip.empty() ) {
        const std::string plain = remove_color_tags( tip );
        const ImVec2 tip_px = mobile_ui::text_size( face::body, 13.0f, plain, width );
        const float tip_y = origin.y + size.y - safe.w - 24.0f * u - tip_px.y;
        if( tip_y > y + line_h + 24.0f * u ) {
            mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left, tip_y - 18.0f * u ),
                             mobile_ui::col_muted, to_upper_case( _( "Tip" ) ) );
            mobile_ui::text( draw, face::body, 13.0f, ImVec2( left, tip_y ), mobile_ui::col_text_soft,
                             plain, width );
        }
    }
    draw->PopClipRect();
}

#else // !TILES

void draw_loading( const std::string &, const std::string &, const std::string & ) {}
void loading_step() {}
void loading_done() {}

#endif // TILES

} // namespace mobile_system

// The auto notes manager: a tab for the character's list and one for the
// global list, a row for each kind of map extra met so far.  The caches it
// edits and what happens to them afterwards (finish()) are upstream's.
void auto_notes::auto_note_manager_gui::mobile_show()
{
    int tab = 0;
    int selected = -1;
    while( true ) {
        const bool enabled = get_option<bool>( "AUTO_NOTES_MAP_EXTRAS" );
        mobile_list::spec what;
        what.title = _( "Auto notes" );
        what.header = enabled ? _( "Map extras you come across are noted on the map." ) :
                      _( "Notes for map extras are off: nothing here is noted." );
        what.tabs = { std::string( _( "Character" ) ), std::string( _( "Global" ) ) };
        what.empty_text = _( "Discover more special encounters to populate this list" );
        what.buttons = { std::string( enabled ? _( "Turn map extra notes off" ) : _( "Turn map extra notes on" ) ) };
        // which list and which map extra each row is
        std::vector<std::pair<bool, map_extra_id>> where;
        for( const bool character : {
                 true, false
             } ) {
            for( const map_extra_id &id : character ? char_displayCache : global_displayCache ) {
                const std::pair<const map_extra, bool> &cached = ( character ? char_mapExtraCache :
                        global_mapExtraCache ).at( id );
                const std::unordered_map<map_extra_id, custom_symbol> &symbols = character ?
                        char_custom_symbol_cache : global_custom_symbol_cache;
                const auto custom = symbols.find( id );
                const bool has_custom = custom != symbols.end();
                const std::string symbol = has_custom ? custom->second.get_symbol_string() :
                                           cached.first.get_symbol();
                mobile_list::row r;
                r.tab = character ? 0 : 1;
                r.name = cached.first.name();
                r.detail = has_custom ? string_format( _( "Symbol: %s (your own)" ), symbol ) :
                           string_format( _( "Symbol: %s" ), symbol );
                r.right = cached.second ? _( "On" ) : _( "Off" );
                r.current = cached.second;
                r.enabled = cached.second && enabled;
                r.description = cached.first.description();
                r.action = cached.second ? _( "Turn off" ) : _( "Turn on" );
                r.more_actions = { std::string( _( "Symbol…" ) ) };
                what.rows.push_back( r );
                where.emplace_back( character, id );
            }
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            break;
        }
        if( did.button == 0 ) {
            get_options().get_option( "AUTO_NOTES_MAP_EXTRAS" ).setNext();
            get_options().save();
            continue;
        }
        if( did.row < 0 || static_cast<size_t>( did.row ) >= where.size() ) {
            continue;
        }
        const bool character = where[static_cast<size_t>( did.row )].first;
        const map_extra_id &id = where[static_cast<size_t>( did.row )].second;
        std::pair<const map_extra, bool> &cached = ( character ? char_mapExtraCache :
                global_mapExtraCache ).at( id );
        bool &changed = character ? charwasChanged : globalwasChanged;
        if( did.action == 0 ) {
            cached.second = !cached.second;
            changed = true;
            continue;
        }
        // the symbol, then its colour: upstream's two questions
        string_input_popup_imgui symbol_popup( 0 );
        symbol_popup.set_label( _( "Enter a map extra custom symbol (empty to unset):" ) );
        symbol_popup.set_max_input_length( 1 );
        const std::string symbol = symbol_popup.query();
        if( symbol_popup.cancelled() ) {
            continue;
        }
        if( symbol.empty() ) {
            ( character ? char_custom_symbol_cache : global_custom_symbol_cache ).erase( id );
            changed = true;
            continue;
        }
        uilist colors;
        colors.text = _( "Pick a color:" );
        const std::unordered_map<std::string, note_color> &names = get_note_color_names();
        int index = 0;
        for( const std::pair<const std::string, note_color> &color : names ) {
            colors.addentry( colorize( color.second.name, color.second.color ) );
            if( cached.first.color == color.second.color ) {
                colors.selected = index;
            }
            index++;
        }
        const translation color_yellow = to_translation( "color", "yellow" );
        colors.addentry( string_format( _( "Default: %s" ), colorize( color_yellow.translated(),
                                        c_yellow ) ) );
        colors.query();
        std::string color_name;
        if( colors.ret >= 0 && static_cast<size_t>( colors.ret ) < names.size() ) {
            auto iter = names.begin();
            std::advance( iter, colors.ret );
            color_name = get_all_colors().get_name( iter->second.color );
        }
        custom_symbol value;
        value.set_symbol( symbol );
        value.set_color( color_name );
        set_cached_custom_symbol( id, value, character );
        changed = true;
    }
    finish();
}
