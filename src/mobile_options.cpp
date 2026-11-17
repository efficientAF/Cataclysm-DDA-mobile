#include "mobile_options.h"

#if defined(TILES)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cata_imgui.h"
#include "cata_utility.h"
#if defined(__ANDROID__)
#include "cursesport.h"
#endif
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "input_popup.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "options.h"
#include "output.h"
#include "popup.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "uilist.h"

namespace mobile_options
{

using mobile_ui::face;
using cOpt = options_manager::cOpt;

namespace
{

constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_switch_off = IM_COL32( 255, 255, 255, 41 );
constexpr ImU32 col_field = IM_COL32( 8, 10, 7, 255 );
constexpr ImU32 col_lead = IM_COL32( 191, 198, 181, 255 );
constexpr ImU32 col_disabled = IM_COL32( 233, 236, 223, 102 );

enum class entry_kind : int {
    // the name of a page, above its search results
    page,
    // the name of an option group
    group,
    // a paragraph of explanation
    lead,
    // upstream's blank line
    gap,
    option,
};

// One line of a page, as upstream lists it.
struct entry {
    entry_kind kind = entry_kind::gap;
    // the header or the paragraph
    std::string text;
    cOpt *opt = nullptr;
};

struct page {
    std::string name;
    std::vector<entry> entries;
};

} // namespace

// Reads the pages and groups of the options manager, which it keeps private.
struct access {
    static std::vector<page> pages( options_manager &options, bool ingame, bool world_options_only );
};

std::vector<page> access::pages( options_manager &options, const bool ingame,
                                 const bool world_options_only )
{
    options_manager::options_container &world = options.world_options.has_value() ?
            *options.world_options.value() : options.options;
    std::vector<page> ret;
    for( const options_manager::Page &from : options.pages_ ) {
        const bool world_page = from.id_ == "world_default";
        if( world_options_only && !world_page ) {
            continue;
        }
        // the same choice of container as upstream's screen
        options_manager::options_container &container = ( ingame || world_options_only ) &&
                world_page ? world : options.options;
        page to;
        to.name = ingame && world_page ? _( "Current world" ) : from.name_.translated();
        if( ingame && world_page ) {
            to.entries.push_back( { entry_kind::lead,
                                    _( "Note: Some of these options may produce unexpected results if changed." ),
                                    nullptr } );
        }
        bool any = false;
        std::string group;
        for( const options_manager::PageItem &item : from.items_ ) {
            switch( item.type ) {
                case options_manager::ItemType::BlankLine:
                    if( !to.entries.empty() && to.entries.back().kind == entry_kind::option ) {
                        to.entries.push_back( {} );
                    }
                    break;
                case options_manager::ItemType::GroupHeader: {
                    const options_manager::Group &g = options.find_group( item.group );
                    to.entries.push_back( { entry_kind::group, g.name_.translated(), nullptr } );
                    const std::string tip = g.tooltip_.translated();
                    if( !tip.empty() ) {
                        to.entries.push_back( { entry_kind::lead, tip, nullptr } );
                    }
                    group = item.group;
                    break;
                }
                case options_manager::ItemType::Option: {
                    const auto found = container.find( item.data );
                    // what upstream hides stays hidden
                    if( found == container.end() || options.get_option( item.data ).is_hidden() ) {
                        break;
                    }
                    if( item.group != group ) {
                        // back out of a group
                        if( to.entries.empty() || to.entries.back().kind != entry_kind::gap ) {
                            to.entries.push_back( {} );
                        }
                        group = item.group;
                    }
                    to.entries.push_back( { entry_kind::option, "", &found->second } );
                    any = true;
                    break;
                }
            }
        }
        // a page without anything to change gets no chip
        if( any ) {
            ret.push_back( std::move( to ) );
        }
    }
    return ret;
}

namespace
{

enum class control : int {
    toggle,
    stepper,
    // a value and a chevron; a tap opens a sheet, the keypad or the text card
    choice,
};

// One line of the list as it is drawn, with its text and height worked out.
struct row {
    entry_kind kind = entry_kind::gap;
    cOpt *opt = nullptr;
    std::string title;
    std::string sub;
    control ctl = control::choice;
    std::string value;
    // false while the option's prerequisite is not met
    bool enabled = true;
    // stepper: whether there is room below and above
    bool can_dec = false;
    bool can_inc = false;
    float control_w = 0.0f;
    float text_w = 0.0f;
    float height = 0.0f;
};

bool is_number( const cOpt &opt )
{
    return opt.getType() == "int" || opt.getType() == "float";
}

double number_of( const cOpt &opt )
{
    return opt.getType() == "int" ? static_cast<double>( opt.value_as<int>() ) :
           static_cast<double>( opt.value_as<float>() );
}

// The values an int_map option goes through, in upstream's order, starting
// with the current one.  The list itself is private to the option.
std::vector<std::string> int_map_names( const cOpt &opt )
{
    std::vector<std::string> names;
    cOpt copy = opt;
    do {
        names.push_back( copy.getValueName() );
        copy.setNext();
    } while( copy != opt && names.size() < 1000 );
    return names;
}

class options_window : public cataimgui::window
{
    public:
        options_window( std::vector<page> pages, const bool world_options_only, const bool with_tabs )
            : cataimgui::window( "MOBILE_OPTIONS",
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoBackground ),
              pages( std::move( pages ) ), world_options_only( world_options_only ),
              with_tabs( with_tabs ) {}

        // the screen was left; result says how
        bool done = false;
        std::string result;
        // an option whose row was tapped and that needs a sheet or a popup
        cOpt *asked = nullptr;
        // the list has to be worked out again
        bool dirty = true;

        // One step back: out of the search first.  False if the screen should close.
        bool back();
        void scroll_chips( int direction );
        bool typing() const {
            return search_active;
        }

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        std::vector<page> pages;
        bool world_options_only;
        bool with_tabs;
        size_t current = 0;
        std::string query;
        std::string applied_query;
        bool search_active = false;
        bool drop_focus = false;
        bool to_top = false;
        float chip_scroll_by = 0.0f;
        std::vector<row> rows;
        float rows_width = 0.0f;

        void rebuild( float width );
        void add_rows( const page &from, float width );
        void draw_search( const ImVec2 &pos, float width );
        void draw_chips( const ImVec2 &pos, float width );
        void draw_list( const ImVec2 &pos, const ImVec2 &size );
        void draw_row( ImDrawList *draw, row &r, const ImVec2 &pos, float width );
        bool step_button( ImDrawList *draw, const std::string &id, const char *icon_name,
                          const ImVec2 &pos, float row_h, bool enabled );
        bool foot_button( const std::string &id, const std::string &label, const ImVec2 &pos,
                          const ImVec2 &size, bool primary );
};

bool options_window::back()
{
    if( !query.empty() || search_active ) {
        query.clear();
        drop_focus = true;
        return true;
    }
    return false;
}

void options_window::scroll_chips( const int direction )
{
    chip_scroll_by += static_cast<float>( direction ) * 220.0f * mobile_ui::unit();
}

void options_window::add_rows( const page &from, const float width )
{
    const float u = mobile_ui::unit();
    const bool searching = !query.empty();
    for( const entry &e : from.entries ) {
        row r;
        r.kind = e.kind;
        if( e.kind != entry_kind::option ) {
            // search results are a plain list of options
            if( searching ) {
                continue;
            }
            // no gap at the top, and never two in a row
            if( e.kind == entry_kind::gap && ( rows.empty() || rows.back().kind != entry_kind::option ) ) {
                continue;
            }
            r.title = e.text;
            if( e.kind == entry_kind::group ) {
                r.height = 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, r.title ).y + 6.0f * u;
            } else if( e.kind == entry_kind::lead ) {
                r.text_w = width - 32.0f * u;
                r.height = 8.0f * u + mobile_ui::text_size( face::body, 13.0f, r.title, r.text_w ).y +
                           6.0f * u;
            } else {
                r.height = 14.0f * u;
            }
            rows.push_back( std::move( r ) );
            continue;
        }
        cOpt &opt = *e.opt;
        r.opt = &opt;
        r.title = remove_color_tags( opt.getMenuText() );
        r.sub = remove_color_tags( opt.getTooltip() );
        if( searching && !lcmatch( r.title, query ) && !lcmatch( r.sub, query ) ) {
            continue;
        }
        const std::string type = opt.getType();
        // the default, and the range of a number; a choice shows its values in the sheet
        if( type != "string_select" && !( type == "string_input" && opt.getDefaultValue().empty() ) ) {
            if( !r.sub.empty() ) {
                r.sub += "  ";
            }
            r.sub += remove_color_tags( opt.getDefaultText() );
        }
        r.enabled = !opt.hasPrerequisite() || opt.checkPrerequisite();
        r.value = opt.getValueName();
        if( type == "bool" ) {
            r.ctl = control::toggle;
            r.control_w = 50.0f * u;
        } else if( is_number( opt ) ) {
            r.ctl = control::stepper;
            // upstream's steps wrap around at the ends; the buttons stop there
            const double now = number_of( opt );
            cOpt next = opt;
            next.setNext();
            r.can_inc = number_of( next ) > now;
            cOpt prev = opt;
            prev.setPrev();
            r.can_dec = number_of( prev ) < now;
            r.control_w = 88.0f * u + std::max( 52.0f * u,
                                                mobile_ui::text_size( face::mono, 13.0f, r.value ).x + 12.0f * u );
        } else {
            r.ctl = control::choice;
            if( r.value.empty() ) {
                //~ Shown in place of an option's value when it is empty
                r.value = _( "(empty)" );
            }
            r.control_w = std::min( mobile_ui::text_size( face::bold, 13.0f, r.value ).x,
                                    std::max( 90.0f * u, ( width - 32.0f * u ) * 0.36f ) ) + 24.0f * u;
        }
        r.text_w = std::max( 80.0f * u, width - 32.0f * u - 12.0f * u - r.control_w );
        const float title_h = mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w ).y;
        const float sub_h = r.sub.empty() ? 0.0f : 2.0f * u +
                            mobile_ui::text_size( face::body, 12.0f, r.sub, r.text_w ).y;
        r.height = std::max( 60.0f * u, title_h + sub_h + 24.0f * u );
        rows.push_back( std::move( r ) );
    }
}

void options_window::rebuild( const float width )
{
    dirty = false;
    rows_width = width;
    if( query != applied_query ) {
        applied_query = query;
        to_top = true;
    }
    rows.clear();
    if( pages.empty() ) {
        return;
    }
    current = std::min( current, pages.size() - 1 );
    if( query.empty() ) {
        add_rows( pages[current], width );
        return;
    }
    // a search looks through every page
    const float u = mobile_ui::unit();
    for( const page &p : pages ) {
        const size_t before = rows.size();
        add_rows( p, width );
        if( rows.size() > before && pages.size() > 1 ) {
            row head;
            head.kind = entry_kind::page;
            head.title = p.name;
            head.height = 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, head.title ).y + 6.0f * u;
            rows.insert( rows.begin() + static_cast<std::ptrdiff_t>( before ), std::move( head ) );
        }
    }
}

void options_window::draw_search( const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 field = pos;
    const ImVec2 field_max( pos.x + width, pos.y + 44.0f * u );
    mobile_ui::box( draw, field, field_max, col_field, mobile_ui::col_line, 11.0f );
    mobile_ui::icon( draw, "search", ImVec2( field.x + 12.0f * u, field.y + 13.0f * u ), 18.0f,
                     mobile_ui::col_muted );
    const float clear_w = query.empty() ? 0.0f : 44.0f * u;
    const float text_h = 15.0f * u;
    const float pad_y = std::floor( ( 44.0f * u - text_h ) * 0.5f - 2.0f * u );
    if( drop_focus ) {
        ImGui::ClearActiveID();
        drop_focus = false;
    }
    ImGui::SetCursorScreenPos( ImVec2( field.x + 38.0f * u, field.y ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, pad_y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
    ImGui::PushFont( nullptr, text_h / std::max( 0.1f, ImGui::GetStyle().FontScaleMain *
                     ImGui::GetStyle().FontScaleDpi ) );
    ImGui::SetNextItemWidth( field_max.x - field.x - 38.0f * u - clear_w - 6.0f * u );
    ImGui::InputTextWithHint( "##options_search", _( "Search options" ), &query );
    search_active = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 3 );
    if( search_active ) {
        draw->AddRect( field, field_max, mobile_ui::col_accent, 11.0f * u, 0, 2.0f * u );
    }
    if( !query.empty() ) {
        const ImVec2 clear_pos( field_max.x - 44.0f * u, field.y );
        if( mobile_ui::pressable( "##options_search_clear", clear_pos,
                                  ImVec2( 44.0f * u, 44.0f * u ) ) ) {
            query.clear();
            drop_focus = true;
        }
        mobile_ui::icon( draw, "x", ImVec2( clear_pos.x + 13.0f * u, clear_pos.y + 13.0f * u ), 18.0f,
                         mobile_ui::col_muted );
    }
    if( query != applied_query ) {
        dirty = true;
    }
}

void options_window::draw_chips( const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    const float row_h = 50.0f * u;
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##options_chips", ImVec2( width, row_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float x = base.x + 12.0f * u;
    for( size_t i = 0; i < pages.size(); ++i ) {
        const std::string &label = pages[i].name;
        // while searching the results come from every page, so none is lit
        const bool on = query.empty() && i == current;
        const ImVec2 label_size = mobile_ui::text_size( face::bold, 13.0f, label );
        const float chip_w = label_size.x + 26.0f * u;
        const ImVec2 chip_pos( x, base.y + 10.0f * u );
        const ImVec2 chip_max( x + chip_w, chip_pos.y + 30.0f * u );
        bool held = false;
        // the whole height of the row takes the tap
        if( mobile_ui::pressable( "##options_chip" + std::to_string( i ), ImVec2( x - 3.0f * u, base.y ),
                                  ImVec2( chip_w + 6.0f * u, row_h ), &held ) ) {
            current = i;
            query.clear();
            drop_focus = true;
            dirty = true;
            to_top = true;
        }
        mobile_ui::box( draw, chip_pos, chip_max,
                        on ? mobile_ui::col_accent : held ? mobile_ui::col_button_down :
                        IM_COL32( 255, 255, 255, 10 ), on ? mobile_ui::col_accent : mobile_ui::col_line, 15.0f );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( chip_pos.x + 13.0f * u,
                         chip_pos.y + ( 30.0f * u - label_size.y ) * 0.5f ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, label );
        x += chip_w + 6.0f * u;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( x + 6.0f * u - base.x, 1.0f ) );
    // sideways swipes arrive as arrow keys; the mouse wheel works on desktop
    if( ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheelH != 0.0f ) {
        chip_scroll_by -= ImGui::GetIO().MouseWheelH * 60.0f * u;
    }
    if( chip_scroll_by != 0.0f ) {
        ImGui::SetScrollX( std::clamp( ImGui::GetScrollX() + chip_scroll_by, 0.0f,
                                       std::max( 0.0f, x + 6.0f * u - base.x - width ) ) );
        chip_scroll_by = 0.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool options_window::step_button( ImDrawList *const draw, const std::string &id,
                                  const char *const icon_name, const ImVec2 &pos, const float row_h, const bool enabled )
{
    const float u = mobile_ui::unit();
    // 36 units to look at, 44 wide and the row's height to touch
    const ImVec2 box_min( pos.x + 4.0f * u, pos.y + ( row_h - 36.0f * u ) * 0.5f );
    const ImVec2 box_max( box_min.x + 36.0f * u, box_min.y + 36.0f * u );
    bool held = false;
    // a finger only reaches ImGui as a tap once it is lifted, so there is no
    // stepping while it rests; the number itself opens the keypad for big changes
    const bool step = enabled && mobile_ui::pressable( id, pos, ImVec2( 44.0f * u, row_h ), &held );
    mobile_ui::box( draw, box_min, box_max,
                    held ? mobile_ui::col_button_down : enabled ? IM_COL32( 255, 255, 255, 15 ) :
                    IM_COL32( 255, 255, 255, 6 ), enabled ? mobile_ui::col_line : IM_COL32( 232, 236, 222, 14 ),
                    10.0f );
    mobile_ui::icon( draw, icon_name, ImVec2( box_min.x + 9.0f * u, box_min.y + 9.0f * u ), 18.0f,
                     enabled ? mobile_ui::col_fg : col_disabled );
    return step;
}

void options_window::draw_row( ImDrawList *const draw, row &r, const ImVec2 &pos,
                               const float width )
{
    const float u = mobile_ui::unit();
    const float left = pos.x;
    if( r.kind == entry_kind::gap ) {
        return;
    }
    if( r.kind == entry_kind::group || r.kind == entry_kind::page ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left + 16.0f * u, pos.y + 16.0f * u ),
                         r.kind == entry_kind::page ? mobile_ui::col_accent : mobile_ui::col_muted,
                         to_upper_case( r.title ) );
        return;
    }
    if( r.kind == entry_kind::lead ) {
        mobile_ui::text( draw, face::body, 13.0f, ImVec2( left + 16.0f * u, pos.y + 8.0f * u ), col_lead,
                         r.title, r.text_w );
        return;
    }
    cOpt &opt = *r.opt;
    const std::string id = "##opt_" + opt.getName();
    const float right = left + width - 16.0f * u;
    const float control_x = right - r.control_w;

    // the row itself is the target, except under a stepper's buttons
    const float target_w = r.ctl == control::stepper && r.enabled ? control_x - left : width;
    bool row_held = false;
    const bool row_tapped = mobile_ui::pressable( id, pos, ImVec2( target_w, r.height ), &row_held );
    if( row_held && r.ctl != control::toggle ) {
        draw->AddRectFilled( pos, ImVec2( pos.x + target_w, pos.y + r.height ),
                             mobile_ui::col_button_down );
    }

    // name and explanation
    const ImVec2 title_px = mobile_ui::text_size( face::bold, 14.5f, r.title, r.text_w );
    const float text_h = r.height - 24.0f * u;
    const float used_h = r.sub.empty() ? title_px.y : text_h;
    const float top = pos.y + ( r.height - std::min( text_h, used_h ) ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, top ),
                     r.enabled ? mobile_ui::col_fg : col_disabled, r.title, r.text_w );
    if( !r.sub.empty() ) {
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( left + 16.0f * u, top + title_px.y + 2.0f * u ),
                         mobile_ui::col_muted, r.sub, r.text_w );
    }

    bool changed = false;
    switch( r.ctl ) {
        case control::toggle: {
            const bool on = opt.value_as<bool>();
            const ImVec2 sw( control_x, pos.y + ( r.height - 30.0f * u ) * 0.5f );
            ImU32 fill = on ? mobile_ui::col_accent : col_switch_off;
            if( !r.enabled ) {
                fill = ( fill & ~IM_COL32_A_MASK ) | ( ( ( fill >> IM_COL32_A_SHIFT ) * 2 / 5 ) <<
                                                       IM_COL32_A_SHIFT );
            }
            draw->AddRectFilled( sw, ImVec2( sw.x + 50.0f * u, sw.y + 30.0f * u ), fill, 15.0f * u );
            draw->AddCircleFilled( ImVec2( sw.x + ( on ? 35.0f : 15.0f ) * u, sw.y + 15.0f * u ), 12.0f * u,
                                   r.enabled ? IM_COL32_WHITE : IM_COL32( 255, 255, 255, 110 ), 32 );
            if( row_tapped ) {
                if( r.enabled ) {
                    opt.setNext();
                    changed = true;
                } else {
                    asked = &opt;
                }
            }
            break;
        }
        case control::stepper: {
            const ImVec2 value_px = mobile_ui::text_size( face::mono, 13.0f, r.value );
            const float value_w = r.control_w - 88.0f * u;
            if( step_button( draw, id + "_dec", "minus", ImVec2( control_x, pos.y ), r.height,
                             r.enabled && r.can_dec ) ) {
                opt.setPrev();
                changed = true;
            }
            if( step_button( draw, id + "_inc", "plus", ImVec2( right - 44.0f * u, pos.y ), r.height,
                             r.enabled && r.can_inc ) ) {
                opt.setNext();
                changed = true;
            }
            if( r.enabled ) {
                // the number itself opens the keypad
                bool value_held = false;
                if( mobile_ui::pressable( id + "_value", ImVec2( control_x + 44.0f * u, pos.y ),
                                          ImVec2( value_w, r.height ), &value_held ) ) {
                    asked = &opt;
                }
                if( value_held ) {
                    mobile_ui::box( draw, ImVec2( control_x + 44.0f * u, pos.y + ( r.height - 36.0f * u ) * 0.5f ),
                                    ImVec2( control_x + 44.0f * u + value_w, pos.y + ( r.height + 36.0f * u ) * 0.5f ),
                                    mobile_ui::col_button_down, 0, 10.0f );
                }
            }
            mobile_ui::text( draw, face::mono, 13.0f,
                             ImVec2( control_x + 44.0f * u + ( value_w - value_px.x ) * 0.5f,
                                     pos.y + ( r.height - value_px.y ) * 0.5f ),
                             r.enabled ? mobile_ui::col_fg : col_disabled, r.value );
            if( row_tapped ) {
                asked = &opt;
            }
            break;
        }
        case control::choice: {
            const ImVec2 value_px = mobile_ui::text_size( face::bold, 13.0f, r.value );
            const float value_w = r.control_w - 24.0f * u;
            const float value_x = control_x + std::max( 0.0f, value_w - value_px.x );
            draw->PushClipRect( ImVec2( control_x, pos.y ), ImVec2( control_x + value_w, pos.y + r.height ),
                                true );
            mobile_ui::text( draw, face::bold, 13.0f, ImVec2( value_x,
                             pos.y + ( r.height - value_px.y ) * 0.5f ),
                             r.enabled ? mobile_ui::col_accent : col_disabled, r.value );
            draw->PopClipRect();
            mobile_ui::icon( draw, "chev", ImVec2( right - 20.0f * u, pos.y + ( r.height - 20.0f * u ) * 0.5f ),
                             20.0f, mobile_ui::col_muted );
            if( row_tapped ) {
                asked = &opt;
            }
            break;
        }
    }
    if( changed ) {
        dirty = true;
#if defined(__ANDROID__)
        // the grid reads live options, so an edit shows at once (as upstream)
        request_terminal_relayout();
#endif
    }
    draw->AddLine( ImVec2( left, pos.y + r.height ), ImVec2( left + width, pos.y + r.height ),
                   col_row_line, std::max( 1.0f, u ) );
}

void options_window::draw_list( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    if( dirty || rows_width != size.x ) {
        rebuild( size.x );
    }
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##options_list", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    if( to_top ) {
        ImGui::SetScrollY( 0.0f );
        to_top = false;
    }
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float y = base.y;
    if( rows.empty() ) {
        const std::string none = query.empty() ? _( "Nothing to change here." ) :
                                 string_format( _( "No option matches \"%s\"." ), query );
        mobile_ui::text( draw, face::body, 14.0f, ImVec2( base.x + 16.0f * u, y + 20.0f * u ),
                         mobile_ui::col_muted, none, size.x - 32.0f * u );
        y += 60.0f * u;
    }
    for( row &r : rows ) {
        // rows out of view only take their room
        if( y + r.height >= pos.y && y <= pos.y + size.y ) {
            draw_row( draw, r, ImVec2( base.x, y ), size.x );
        }
        y += r.height;
    }
    y += 16.0f * u + ( world_options_only && with_tabs ? 0.0f : safe.w );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool options_window::foot_button( const std::string &id, const std::string &label,
                                  const ImVec2 &pos, const ImVec2 &size, const bool primary )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), fill,
                    primary ? 0 : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

void options_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const back_draw = ImGui::GetWindowDrawList();
    back_draw->PushClipRectFullScreen();
    back_draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                              mobile_ui::col_screen );
    back_draw->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    float y = origin.y + safe.y;
    float bar_h = 0.0f;
    const std::string title = world_options_only ? _( "World options" ) : _( "All options" );
    if( mobile_ui::app_bar( title, ImVec2( left, y ), width, &bar_h ) ) {
        done = true;
        result = world_options_only ? "QUIT" : "";
    }
    if( landscape ) {
        // a wide screen has room for the search field in the bar
        const float field_x = left + 52.0f * u + mobile_ui::text_size( face::display, 21.0f, title ).x +
                              24.0f * u;
        draw_search( ImVec2( field_x, y + 5.0f * u ), left + width - 12.0f * u - field_x );
        y += bar_h;
    } else {
        y += bar_h + 10.0f * u;
        draw_search( ImVec2( left + 12.0f * u, y ), width - 24.0f * u );
        y += 44.0f * u;
    }
    if( pages.size() > 1 ) {
        draw_chips( ImVec2( left, y ), width );
        y += 50.0f * u;
    } else if( !landscape ) {
        y += 8.0f * u;
    }

    // world creation walks through its steps with the buttons at the bottom
    const bool foot = world_options_only && with_tabs;
    const float foot_h = foot ? 68.0f * u + safe.w : 0.0f;
    draw_list( ImVec2( left, y ), ImVec2( width, origin.y + size.y - foot_h - y ) );
    if( foot ) {
        const float foot_y = origin.y + size.y - foot_h;
        back_draw->AddLine( ImVec2( left, foot_y ), ImVec2( left + width, foot_y ), mobile_ui::col_line,
                            std::max( 1.0f, u ) );
        const float button_w = ( width - 24.0f * u - 10.0f * u ) * 0.5f;
        const ImVec2 button_size( button_w, 48.0f * u );
        if( foot_button( "##options_prev", _( "Previous" ), ImVec2( left + 12.0f * u, foot_y + 10.0f * u ),
                         button_size, false ) ) {
            done = true;
            result = "PREV_TAB";
        }
        if( foot_button( "##options_next", _( "Next" ),
                         ImVec2( left + 12.0f * u + button_w + 10.0f * u, foot_y + 10.0f * u ), button_size, true ) ) {
            done = true;
            result = "NEXT_TAB";
        }
    }
}

// The row of an option was tapped and a switch or a step is not enough: asks
// for the new value the way upstream's screen does on CONFIRM.
void ask( cOpt &opt )
{
#if defined(LOCALIZE)
    if( opt.getName() == "USE_LANG" ) {
        opt.setValue( select_language() );
        return;
    }
#endif
    if( opt.hasPrerequisite() && !opt.checkPrerequisite() ) {
        popup( _( "Prerequisite for this option not met!\n(%s)" ),
               get_options().get_option( opt.getPrerequisite() ).getMenuText() );
        return;
    }
    const std::string type = opt.getType();
    if( type == "string_select" ) {
        const std::vector<options_manager::id_and_option> items = opt.getItems();
        uilist menu;
        menu.title = opt.getMenuText();
        const std::string now = opt.getValue( true );
        for( size_t i = 0; i < items.size(); ++i ) {
            menu.addentry( static_cast<int>( i ), true, 0, items[i].second.translated() );
            if( items[i].first == now ) {
                menu.selected = static_cast<int>( i );
            }
        }
        menu.query();
        if( menu.ret >= 0 && static_cast<size_t>( menu.ret ) < items.size() ) {
            opt.setValue( items[static_cast<size_t>( menu.ret )].first );
        }
    } else if( type == "int_map" ) {
        const std::vector<std::string> names = int_map_names( opt );
        uilist menu;
        menu.title = opt.getMenuText();
        for( size_t i = 0; i < names.size(); ++i ) {
            menu.addentry( static_cast<int>( i ), true, 0, names[i] );
        }
        menu.selected = 0;
        menu.query();
        // the list starts at the current value and follows upstream's order
        for( int i = 0; i < menu.ret; ++i ) {
            opt.setNext();
        }
    } else if( type == "int" ) {
        number_input_popup<int> entry( 0, opt.value_as<int>() );
        entry.set_label( opt.getMenuText() );
        opt.setValue( entry.query() );
    } else if( type == "float" ) {
        number_input_popup<float> entry( 0, opt.value_as<float>() );
        entry.set_label( opt.getMenuText() );
        opt.setValue( entry.query() );
    } else {
        // string_input opens its text field; a bool flips
        opt.setNext();
    }
#if defined(__ANDROID__)
    request_terminal_relayout();
#endif
}

} // namespace

std::string show( options_manager &options, const bool ingame, const bool world_options_only,
                  const bool with_tabs )
{
    options_window window( access::pages( options, ingame, world_options_only ), world_options_only,
                           with_tabs );
    input_context ctxt( "default", keyboard_mode::keychar );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    // the search field takes the keys
    ctxt.set_timeout( 50 );
    while( !window.done ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            if( !window.back() ) {
                return world_options_only ? "QUIT" : "";
            }
        } else if( ( action == "LEFT" || action == "RIGHT" ) && !window.typing() ) {
            // a swipe to the left brings the chips on the right into view
            window.scroll_chips( action == "LEFT" ? 1 : -1 );
        }
        if( window.asked != nullptr ) {
            cOpt &opt = *window.asked;
            window.asked = nullptr;
            ask( opt );
            window.dirty = true;
        }
    }
    return window.result;
}

} // namespace mobile_options

#else // !TILES

namespace mobile_options
{

std::string show( options_manager &, bool, bool, bool )
{
    return "";
}

} // namespace mobile_options

#endif // TILES
