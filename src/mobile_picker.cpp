#include "mobile_picker.h"

#include "item_location.h"
#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "cata_imgui.h"
#include "cata_utility.h"
#include "character.h"
#include "color.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_category.h"
#include "mobile_entry.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_picker
{

bool wanted()
{
#if defined(TILES)
    return mobile_menu::enabled();
#else
    return false;
#endif
}

#if defined(TILES)

using mobile_ui::face;

namespace
{

constexpr ImU32 col_field = IM_COL32( 8, 10, 7, 255 );
constexpr ImU32 col_header = IM_COL32( 255, 255, 255, 8 );
constexpr ImU32 col_denied = IM_COL32( 216, 187, 66, 255 );

ImFont *body_font()
{
    for( ImFont *const font : ImGui::GetIO().Fonts->Fonts ) {
        if( std::strstr( font->GetDebugName(), "Barlow-Medium" ) != nullptr ) {
            return font;
        }
    }
    return ImGui::GetFont();
}

// One row: an entry of the selector, with what the row shows worked out once.
struct row {
    inventory_entry *entry = nullptr;
    std::string name;
    // what upstream's extra columns say, or why it cannot be chosen
    std::string detail;
    // where the item is
    std::string where;
    std::string category;
    std::string symbol;
    ImU32 symbol_color = 0;
    bool selectable = true;
};

std::vector<row> collect_rows( const inventory_selector &selector, const Character &owner )
{
    std::vector<row> rows;
    const inventory_selector_preset &preset = selector.mobile_preset();
    for( const inventory_column *const column : selector.mobile_columns() ) {
        // the column that repeats what is ticked
        if( column == nullptr || dynamic_cast<const selection_column *>( column ) != nullptr ) {
            continue;
        }
        const std::vector<inventory_entry *> entries = column->get_entries(
        []( const inventory_entry & e ) {
            return e.is_item();
        }, true );
        for( inventory_entry *const entry : entries ) {
            const item_location &loc = entry->any_item();
            if( !loc ) {
                continue;
            }
            row r;
            r.entry = entry;
            const size_t stack = entry->get_stack_size();
            r.name = remove_color_tags( loc->tname( stack > 1 ? static_cast<unsigned int>( stack ) : 1 ) );
            if( stack > 1 ) {
                r.name = string_format( "%d %s", static_cast<int>( stack ), r.name );
            }
            r.selectable = entry->is_selectable();
            const std::string denial = remove_color_tags( preset.get_denial( *entry ) );
            if( !denial.empty() ) {
                r.detail = denial;
                r.selectable = false;
            } else {
                // the first cell is the name
                for( size_t i = 1; i < preset.get_cells_count(); ++i ) {
                    if( preset.is_stub_cell( *entry, i ) ) {
                        continue;
                    }
                    const std::string cell = remove_color_tags( preset.get_cell_text( *entry, i ) );
                    // bars drawn with punctuation mean nothing off the text grid
                    if( std::none_of( cell.begin(), cell.end(), []( const char c ) {
                    return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
                               static_cast<unsigned char>( c ) >= 0x80;
                    } ) ) {
                        continue;
                    }
                    const std::string heading = to_lower_case( remove_color_tags(
                                                    preset.mobile_cell_title( i ) ) );
                    r.detail += ( r.detail.empty() ? "" : " · " ) +
                                ( heading.empty() ? cell : heading + " " + cell );
                }
            }
            r.where = remove_color_tags( loc.describe( &owner ) );
            const item_category *const category = entry->get_category_ptr();
            r.category = category != nullptr ? category->name_header() : std::string();
            r.symbol = loc->symbol();
            r.symbol_color = cataimgui::ImU32_from_color( loc->color_in_inventory( &owner ) );
            rows.push_back( r );
        }
    }
    return rows;
}

enum class outcome : int {
    none,
    back,
    confirm,
};

class picker_window : public cataimgui::window
{
    public:
        picker_window( inventory_selector &selector, inventory_multiselector *const multi ) :
            cataimgui::window( "MOBILE_PICKER",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            selector( selector ), multi( multi ) {
            title = remove_color_tags( selector.mobile_title() );
            // upstream's hints for ticking are about keys; the checkboxes need none
            hint = multi != nullptr ? std::string() : remove_color_tags( selector.mobile_hint() );
            if( title == _( "Multidrop" ) ) {
                title = _( "Drop" );
            }
            rows = collect_rows( selector, selector.mobile_owner() );
            for( const row &r : rows ) {
                if( !r.category.empty() &&
                    std::find( categories.begin(), categories.end(), r.category ) == categories.end() ) {
                    categories.push_back( r.category );
                }
            }
        }
        outcome result = outcome::none;
        // pick-one: the chosen row, or -1
        int chosen = -1;
        // pick-many: the row whose count is to be typed in, or -1
        int ask_count = -1;
        std::vector<row> rows;
        inventory_multiselector *multi_selector() const {
            return multi;
        }
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        bool shown( const row &r ) const;
        bool button( ImDrawList *draw, const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                     const std::string &label, bool primary, bool enabled );
        float draw_row( ImDrawList *draw, int index, const ImVec2 &pos, float width );
        inventory_selector &selector;
        inventory_multiselector *multi;
        std::string title;
        std::string hint;
        std::vector<std::string> categories;
        // "" for all
        std::string filter;
        std::string search;
};

bool picker_window::shown( const row &r ) const
{
    return ( filter.empty() || r.category == filter ) && ( search.empty() ||
            lcmatch( r.name, search ) );
}

bool picker_window::button( ImDrawList *const draw, const std::string &id, const ImVec2 &pos,
                            const ImVec2 &size, const std::string &label, const bool primary, const bool enabled )
{
    const float u = mobile_ui::unit();
    bool held = false;
    const bool tapped = enabled && mobile_ui::pressable( id, pos, size, &held );
    const ImU32 fill = !enabled ? ( primary ? IM_COL32( 240, 145, 63, 70 ) :
                                    IM_COL32( 255, 255, 255, 6 ) ) :
                       primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), fill,
                    primary ? 0 : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.5f, label );
    draw->PushClipRect( ImVec2( pos.x + 6.0f * u, pos.y ), ImVec2( pos.x + size.x - 6.0f * u,
                        pos.y + size.y ), true );
    mobile_ui::text( draw, face::bold, 14.5f,
                     ImVec2( pos.x + std::max( 6.0f * u, ( size.x - label_px.x ) * 0.5f ),
                             pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? ( enabled ? mobile_ui::col_on_accent : IM_COL32( 26, 15, 6, 160 ) ) :
                     ( enabled ? mobile_ui::col_fg : mobile_ui::col_muted ), label );
    draw->PopClipRect();
    return tapped;
}

// Draws one row and handles its taps.  Returns its height.
float picker_window::draw_row( ImDrawList *const draw, const int index, const ImVec2 &pos,
                               const float width )
{
    const float u = mobile_ui::unit();
    row &r = rows[static_cast<size_t>( index )];
    const size_t picked = r.entry->chosen_count;
    const size_t available = r.entry->get_available_count();
    const bool is_chosen = multi != nullptr ? picked > 0 : chosen == index;
    const bool stepper = multi != nullptr && is_chosen && available > 1;
    const float stepper_w = stepper ? 132.0f * u : 0.0f;
    const float lead = multi != nullptr ? 40.0f * u : 12.0f * u;
    const float text_x = pos.x + lead + 44.0f * u;
    const float where_w = stepper ? 0.0f : std::min( 96.0f * u,
                          mobile_ui::text_size( face::mono, 10.5f, r.where ).x );
    const float text_w = std::max( 60.0f * u,
                                   pos.x + width - 12.0f * u - stepper_w - where_w - 8.0f * u -
                                   text_x );
    const ImVec2 name_px = mobile_ui::text_size( face::bold, 14.5f, r.name, text_w );
    std::string detail = r.detail;
    if( stepper ) {
        detail = string_format( _( "%1$d of %2$d" ), static_cast<int>( picked ),
                                static_cast<int>( available ) ) + ( detail.empty() ? "" : " · " + detail );
    }
    const ImVec2 detail_px = detail.empty() ? ImVec2( 0.0f, 0.0f ) :
                             mobile_ui::text_size( face::body, 12.0f, detail, text_w );
    const float text_h = name_px.y + ( detail.empty() ? 0.0f : 2.0f * u + detail_px.y );
    const float h = std::max( 56.0f * u, text_h + 18.0f * u );
    const ImVec2 max( pos.x + width, pos.y + h );

    // the whole row is the target, apart from the stepper
    bool held = false;
    const bool tapped = r.selectable &&
                        mobile_ui::pressable( "##picker_row" + std::to_string( index ), pos,
                                ImVec2( width - stepper_w - ( stepper ? 12.0f * u : 0.0f ), h ), &held );
    if( is_chosen || held ) {
        draw->AddRectFilled( pos, max, held ? mobile_ui::col_button_down : mobile_ui::col_accent_soft );
    }
    const ImU32 ink = r.selectable ? mobile_ui::col_fg : mobile_ui::col_muted;
    if( multi != nullptr ) {
        const ImVec2 box_min( pos.x + 12.0f * u, pos.y + ( h - 22.0f * u ) * 0.5f );
        const ImVec2 box_max( box_min.x + 22.0f * u, box_min.y + 22.0f * u );
        if( is_chosen ) {
            mobile_ui::box( draw, box_min, box_max, mobile_ui::col_accent, 0, 6.0f );
            mobile_ui::icon( draw, "check", ImVec2( box_min.x + 3.0f * u, box_min.y + 3.0f * u ), 16.0f,
                             mobile_ui::col_on_accent );
        } else {
            mobile_ui::box( draw, box_min, box_max, 0, r.selectable ? mobile_ui::col_muted :
                            mobile_ui::col_line, 6.0f );
        }
    }
    const ImVec2 icon_min( pos.x + lead, pos.y + ( h - 34.0f * u ) * 0.5f );
    mobile_ui::box( draw, icon_min, ImVec2( icon_min.x + 34.0f * u, icon_min.y + 34.0f * u ), col_field,
                    mobile_ui::col_line, 9.0f );
    const ImVec2 symbol_px = mobile_ui::text_size( face::mono, 16.0f, r.symbol );
    mobile_ui::text( draw, face::mono, 16.0f, ImVec2( icon_min.x + ( 34.0f * u - symbol_px.x ) * 0.5f,
                     icon_min.y + ( 34.0f * u - symbol_px.y ) * 0.5f ),
                     r.selectable ? r.symbol_color : mobile_ui::col_muted, r.symbol );
    const float top = pos.y + ( h - text_h ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( text_x, top ), ink, r.name, text_w );
    if( !detail.empty() ) {
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( text_x, top + name_px.y + 2.0f * u ),
                         r.selectable ? mobile_ui::col_muted : col_denied, detail, text_w );
    }
    if( stepper ) {
        // fewer, the count (tap to type it), more
        const float b = 38.0f * u;
        const float x = max.x - 12.0f * u - stepper_w;
        const float y = pos.y + ( h - b ) * 0.5f;
        if( button( draw, "##picker_less" + std::to_string( index ), ImVec2( x, y ), ImVec2( b, b ), "−",
                    false, true ) ) {
            multi->toggle_entry( *r.entry, picked - 1 );
        }
        if( button( draw, "##picker_count" + std::to_string( index ), ImVec2( x + b + 4.0f * u, y ),
                    ImVec2( stepper_w - 2.0f * b - 8.0f * u, b ), std::to_string( picked ), false, true ) ) {
            ask_count = index;
        }
        if( button( draw, "##picker_more" + std::to_string( index ), ImVec2( max.x - 12.0f * u - b, y ),
                    ImVec2( b, b ), "+", false, picked < available ) ) {
            multi->toggle_entry( *r.entry, picked + 1 );
        }
    } else if( !r.where.empty() ) {
        draw->PushClipRect( ImVec2( max.x - 12.0f * u - where_w, pos.y ), max, true );
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( max.x - 12.0f * u - where_w,
                         pos.y + ( h - mobile_ui::text_size( face::mono, 10.5f, r.where ).y ) * 0.5f ),
                         mobile_ui::col_muted, r.where );
        draw->PopClipRect();
    }
    draw->AddLine( ImVec2( pos.x, max.y ), max, IM_COL32( 255, 255, 255, 13 ), std::max( 1.0f, u ) );

    if( tapped ) {
        if( multi != nullptr ) {
            // all of it, or none; the stepper is for some
            multi->toggle_entry( *r.entry, is_chosen ? 0 : available );
        } else {
            chosen = chosen == index ? -1 : index;
        }
    }
    return h;
}

void picker_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const back = ImGui::GetWindowDrawList();
    back->PushClipRectFullScreen();
    back->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    back->PopClipRect();

    // a column in the middle of a wide screen
    const float avail = size.x - safe.x - safe.z;
    const float width = std::min( avail, 620.0f * u );
    const float left = origin.x + safe.x + ( avail - width ) * 0.5f;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( title.empty() ? _( "Choose an item" ) : title,
                            ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        result = outcome::back;
    }
    float y = origin.y + safe.y + bar_h;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    if( !hint.empty() ) {
        const float wrap = width - 24.0f * u;
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( left + 12.0f * u, y + 8.0f * u ),
                         mobile_ui::col_muted, hint, wrap );
        y += 8.0f * u + mobile_ui::text_size( face::body, 12.5f, hint, wrap ).y;
    }

    // search, when there is enough to search
    if( rows.size() > 8 ) {
        y += 10.0f * u;
        ImGui::PushFont( body_font(), 15.0f * u );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 12.0f * u,
                             ( 42.0f * u - ImGui::GetFontSize() ) * 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 11.0f * u );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, u ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, col_field );
        ImGui::PushStyleColor( ImGuiCol_Border, mobile_ui::col_line );
        ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
        ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
        ImGui::SetCursorScreenPos( ImVec2( left + 12.0f * u, y ) );
        ImGui::SetNextItemWidth( width - 24.0f * u );
        ImGui::InputTextWithHint( "##picker_search", _( "Search" ), &search );
        ImGui::PopStyleColor( 4 );
        ImGui::PopStyleVar( 3 );
        ImGui::PopFont();
        y += 42.0f * u;
    }

    // one chip per category that is present
    if( categories.size() > 1 ) {
        y += 10.0f * u;
        float x = left + 12.0f * u;
        draw->PushClipRect( ImVec2( left, y ), ImVec2( left + width, y + 34.0f * u ), true );
        const auto chip = [&]( const std::string & id, const std::string & label, const bool on ) {
            const float w = mobile_ui::text_size( face::bold, 13.0f, label ).x + 26.0f * u;
            const bool hit = mobile_ui::pressable( id, ImVec2( x, y ), ImVec2( w, 34.0f * u ) );
            mobile_ui::box( draw, ImVec2( x, y ), ImVec2( x + w, y + 34.0f * u ),
                            on ? mobile_ui::col_accent : 0, on ? 0 : mobile_ui::col_line, 17.0f );
            mobile_ui::text( draw, face::bold, 13.0f, ImVec2( x + 13.0f * u, y + 9.0f * u ),
                             on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, label );
            x += w + 6.0f * u;
            return hit;
        };
        if( chip( "##picker_chip_all", _( "All" ), filter.empty() ) ) {
            filter.clear();
        }
        for( size_t i = 0; i < categories.size(); ++i ) {
            if( chip( "##picker_chip" + std::to_string( i ), categories[i], filter == categories[i] ) ) {
                filter = categories[i];
            }
        }
        draw->PopClipRect();
        y += 34.0f * u;
    }
    y += 8.0f * u;

    // the list
    const float bottom_bar = 68.0f * u + safe.w;
    const float list_h = origin.y + size.y - bottom_bar - y;
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##picker_list", ImVec2( width, list_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground );
    ImDrawList *const list = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float row_y = base.y;
    std::string last_category;
    bool any = false;
    for( size_t i = 0; i < rows.size(); ++i ) {
        if( !shown( rows[i] ) ) {
            continue;
        }
        any = true;
        if( rows[i].category != last_category ) {
            last_category = rows[i].category;
            list->AddRectFilled( ImVec2( base.x, row_y ), ImVec2( base.x + width, row_y + 26.0f * u ),
                                 col_header );
            mobile_ui::text( list, face::mono, 10.0f, ImVec2( base.x + 12.0f * u, row_y + 8.0f * u ),
                             mobile_ui::col_muted, to_upper_case( last_category ) );
            row_y += 26.0f * u;
        }
        row_y += draw_row( list, static_cast<int>( i ), ImVec2( base.x, row_y ), width );
    }
    if( !any ) {
        const std::string none = rows.empty() ? _( "Nothing here can be used for this." ) :
                                 _( "Nothing matches." );
        mobile_ui::text( list, face::body, 14.0f, ImVec2( base.x + 12.0f * u, row_y + 24.0f * u ),
                         mobile_ui::col_muted, none, width - 24.0f * u );
        row_y += 70.0f * u;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, row_y - base.y + 8.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // the bottom bar says what the tap will do
    const ImVec2 bar_min( left, origin.y + size.y - bottom_bar );
    draw->AddRectFilled( bar_min, ImVec2( left + width, origin.y + size.y ), mobile_ui::col_panel );
    draw->AddLine( bar_min, ImVec2( left + width, bar_min.y ), mobile_ui::col_line,
                   std::max( 1.0f, u ) );
    const ImVec2 button_pos( left + 12.0f * u, bar_min.y + 10.0f * u );
    const float buttons_w = width - 24.0f * u;
    if( multi != nullptr ) {
        int count = 0;
        for( const row &r : rows ) {
            count += r.entry->chosen_count > 0 ? 1 : 0;
        }
        const float clear_w = 96.0f * u;
        if( button( draw, "##picker_clear", button_pos, ImVec2( clear_w, 48.0f * u ), _( "Clear" ), false,
                    count > 0 ) ) {
            for( row &r : rows ) {
                if( r.entry->chosen_count > 0 ) {
                    multi->toggle_entry( *r.entry, 0 );
                }
            }
        }
        const std::string label = count > 0 ? string_format( _( "Done · %d chosen" ), count ) :
                                  _( "Tick what you want" );
        if( button( draw, "##picker_done", ImVec2( button_pos.x + clear_w + 8.0f * u, button_pos.y ),
                    ImVec2( buttons_w - clear_w - 8.0f * u, 48.0f * u ), label, true, count > 0 ) ) {
            result = outcome::confirm;
        }
    } else {
        const bool ready = chosen >= 0 && static_cast<size_t>( chosen ) < rows.size();
        const std::string label = ready ? string_format( _( "Choose %s" ),
                                  rows[static_cast<size_t>( chosen )].name ) : _( "Tap an item" );
        if( button( draw, "##picker_choose", button_pos, ImVec2( buttons_w, 48.0f * u ), label, true,
                    ready ) ) {
            result = outcome::confirm;
        }
    }
}

// Runs the screen until the player confirms or backs out.
outcome run( picker_window &window )
{
    input_context ctxt( "default", keyboard_mode::keychar );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // the search field takes the keys
    ctxt.set_timeout( 50 );
    while( window.result == outcome::none ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            return outcome::back;
        }
        if( window.ask_count >= 0 && window.multi_selector() != nullptr ) {
            // a count typed in with the keypad
            row &r = window.rows[static_cast<size_t>( window.ask_count )];
            window.ask_count = -1;
            mobile_entry::spec what;
            what.title = string_format( _( "How many: %s" ), r.name );
            what.description = string_format( _( "Up to %d." ),
                                              static_cast<int>( r.entry->get_available_count() ) );
            what.text = std::to_string( r.entry->chosen_count );
            what.number = true;
            if( const std::optional<std::string> entered = mobile_entry::query( what ) ) {
                const long wanted = std::atol( entered->c_str() );
                window.multi_selector()->toggle_entry( *r.entry,
                                                       static_cast<size_t>( std::max( 0L, wanted ) ) );
            }
        }
    }
    return window.result;
}

} // namespace

item_location pick_one( inventory_pick_selector &selector )
{
    picker_window window( selector, nullptr );
    if( run( window ) != outcome::confirm || window.chosen < 0 ) {
        return item_location();
    }
    return window.rows[static_cast<size_t>( window.chosen )].entry->any_item();
}

bool pick_many( inventory_multiselector &selector )
{
    picker_window window( selector, &selector );
    return run( window ) == outcome::confirm;
}

#else // !TILES

item_location pick_one( inventory_pick_selector & )
{
    return item_location();
}

bool pick_many( inventory_multiselector & )
{
    return false;
}

#endif // TILES

} // namespace mobile_picker
