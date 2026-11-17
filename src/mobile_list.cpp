#include "mobile_list.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <cstddef>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "output.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_list
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

class list_window : public cataimgui::window
{
    public:
        list_window( const spec &what, const int tab, const int selected ) :
            cataimgui::window( "MOBILE_LIST",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            tab( tab ), selected( selected ), what( what ) {}
        int tab;
        int selected;
        bool back = false;
        bool acted = false;
        // which of the row's buttons was tapped
        int action = 0;
        // the screen's button that was tapped, or -1
        int button = -1;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        void draw_panel( const ImVec2 &min, const ImVec2 &max );
        const spec &what;
};

// The selected row: its name, what it is, and its button.
void list_window::draw_panel( const ImVec2 &min, const ImVec2 &max )
{
    const float u = mobile_ui::unit();
    const row &r = what.rows[static_cast<size_t>( selected )];
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    mobile_ui::box( draw, min, max, mobile_ui::col_sheet, mobile_ui::col_line, 14.0f );
    const float pad = 14.0f * u;
    const float width = max.x - min.x - 2.0f * pad;
    float y = min.y + pad;
    mobile_ui::text( draw, face::display, 19.0f, ImVec2( min.x + pad, y ), mobile_ui::col_title,
                     r.name, width );
    y += mobile_ui::text_size( face::display, 19.0f, r.name, width ).y + 2.0f * u;
    if( !r.detail.empty() ) {
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( min.x + pad, y ), mobile_ui::col_muted,
                         r.detail, width );
        y += mobile_ui::text_size( face::body, 12.5f, r.detail, width ).y;
    }
    y += 8.0f * u;
    const float button_h = r.action.empty() ? 0.0f : 48.0f * u + 10.0f * u;
    const float text_h = max.y - pad - button_h - y;
    ImGui::SetCursorScreenPos( ImVec2( min.x + pad, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##list_description", ImVec2( width, std::max( text_h, 10.0f * u ) ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const inner = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const std::string text = remove_color_tags( r.description );
        mobile_ui::text( inner, face::body, 13.5f, base, mobile_ui::col_text_soft, text, width );
        ImGui::Dummy( ImVec2( 1.0f, mobile_ui::text_size( face::body, 13.5f, text, width ).y + 8.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if( !r.action.empty() ) {
        // the row's buttons side by side, the first one the main one
        std::vector<std::string> labels = { r.action };
        labels.insert( labels.end(), r.more_actions.begin(), r.more_actions.end() );
        const float gap = 8.0f * u;
        const float button_w = ( width - gap * static_cast<float>( labels.size() - 1 ) ) /
                               static_cast<float>( labels.size() );
        for( size_t i = 0; i < labels.size(); ++i ) {
            const ImVec2 pos( min.x + pad + static_cast<float>( i ) * ( button_w + gap ),
                              max.y - pad - 48.0f * u );
            bool held = false;
            if( mobile_ui::pressable( "##list_action" + std::to_string( i ), pos,
                                      ImVec2( button_w, 48.0f * u ), &held ) ) {
                acted = true;
                action = static_cast<int>( i );
            }
            const bool primary = i == 0;
            mobile_ui::box( draw, pos, ImVec2( pos.x + button_w, pos.y + 48.0f * u ),
                            primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                            ( held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft ),
                            primary ? 0 : mobile_ui::col_line, 12.0f );
            const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.0f, labels[i] );
            draw->PushClipRect( pos, ImVec2( pos.x + button_w, pos.y + 48.0f * u ), true );
            mobile_ui::text( draw, face::bold, 14.0f,
                             ImVec2( pos.x + std::max( 4.0f * u, ( button_w - label_px.x ) * 0.5f ),
                                     pos.y + ( 48.0f * u - label_px.y ) * 0.5f ),
                             primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, labels[i] );
            draw->PopClipRect();
        }
    }
}

void list_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( what.title, ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        back = true;
    }
    float y = origin.y + safe.y + bar_h;
    if( !what.header.empty() ) {
        mobile_ui::text( draw, face::mono, 11.5f, ImVec2( left + 14.0f * u, y + 8.0f * u ),
                         mobile_ui::col_muted, what.header );
        y += 8.0f * u + mobile_ui::text_size( face::mono, 11.5f, what.header ).y;
    }
    if( what.tabs.size() > 1 ) {
        y += 10.0f * u;
        float x = left + 12.0f * u;
        for( size_t i = 0; i < what.tabs.size(); ++i ) {
            const int count = static_cast<int>( std::count_if( what.rows.begin(), what.rows.end(),
            [i]( const row & r ) {
                return r.tab == static_cast<int>( i );
            } ) );
            const std::string label = what.tabs[i] + "  " + std::to_string( count );
            const float w = mobile_ui::text_size( face::bold, 13.0f, label ).x + 26.0f * u;
            const bool on = tab == static_cast<int>( i );
            if( x + w > left + width - 12.0f * u && x > left + 12.0f * u ) {
                // many tabs wrap onto further rows
                x = left + 12.0f * u;
                y += 42.0f * u;
            }
            if( mobile_ui::pressable( "##list_tab" + std::to_string( i ), ImVec2( x, y ),
                                      ImVec2( w, 36.0f * u ) ) && !on ) {
                tab = static_cast<int>( i );
                selected = -1;
            }
            mobile_ui::box( draw, ImVec2( x, y ), ImVec2( x + w, y + 36.0f * u ),
                            on ? mobile_ui::col_accent : 0, on ? 0 : mobile_ui::col_line, 18.0f );
            mobile_ui::text( draw, face::bold, 13.0f, ImVec2( x + 13.0f * u, y + 10.0f * u ),
                             on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, label );
            x += w + 6.0f * u;
        }
        y += 36.0f * u;
    }
    y += 8.0f * u;

    // the list, and the selected row's panel beside it or under it
    float bottom = origin.y + size.y - safe.w;
    if( !what.buttons.empty() ) {
        // the screen's own buttons in a bar at the bottom
        const float bar_h = 64.0f * u;
        bottom -= bar_h;
        draw->AddLine( ImVec2( left, bottom ), ImVec2( left + width, bottom ), mobile_ui::col_line,
                       std::max( 1.0f, u ) );
        const float gap = 8.0f * u;
        const float button_w = ( width - 24.0f * u - gap * static_cast<float>( what.buttons.size() - 1 ) ) /
                               static_cast<float>( what.buttons.size() );
        for( size_t i = 0; i < what.buttons.size(); ++i ) {
            const ImVec2 pos( left + 12.0f * u + static_cast<float>( i ) * ( button_w + gap ),
                              bottom + 8.0f * u );
            bool held = false;
            if( mobile_ui::pressable( "##list_button" + std::to_string( i ), pos,
                                      ImVec2( button_w, 48.0f * u ), &held ) ) {
                button = static_cast<int>( i );
            }
            mobile_ui::box( draw, pos, ImVec2( pos.x + button_w, pos.y + 48.0f * u ),
                            held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 12.0f );
            const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.0f, what.buttons[i] );
            mobile_ui::text( draw, face::bold, 14.0f, ImVec2( pos.x + ( button_w - label_px.x ) * 0.5f,
                             pos.y + ( 48.0f * u - label_px.y ) * 0.5f ), mobile_ui::col_fg, what.buttons[i] );
        }
    }
    const bool has_panel = selected >= 0 && static_cast<size_t>( selected ) < what.rows.size();
    const bool landscape = size.x > size.y;
    float list_w = width;
    float list_h = bottom - y;
    if( has_panel && landscape ) {
        list_w = width * 0.5f;
    } else if( has_panel ) {
        list_h = ( bottom - y ) * 0.5f;
    }
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##list_rows", ImVec2( list_w, list_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const list = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        float row_y = base.y;
        bool any = false;
        for( size_t i = 0; i < what.rows.size(); ++i ) {
            const row &r = what.rows[i];
            if( !what.tabs.empty() && r.tab != tab ) {
                continue;
            }
            any = true;
            const float right_w = r.right.empty() ? 0.0f :
                                  mobile_ui::text_size( face::mono, 11.0f, r.right ).x + 10.0f * u;
            const float text_w = list_w - 28.0f * u - right_w;
            const ImVec2 name_px = mobile_ui::text_size( face::bold, 14.5f, r.name, text_w );
            const ImVec2 detail_px = r.detail.empty() ? ImVec2( 0.0f, 0.0f ) :
                                     mobile_ui::text_size( face::body, 12.0f, r.detail, text_w );
            const float text_h = name_px.y + ( r.detail.empty() ? 0.0f : 2.0f * u + detail_px.y );
            const float h = std::max( 54.0f * u, text_h + 18.0f * u );
            const bool on = selected == static_cast<int>( i );
            bool held = false;
            if( mobile_ui::pressable( "##list_row" + std::to_string( i ), ImVec2( base.x, row_y ),
                                      ImVec2( list_w, h ), &held ) ) {
                selected = on ? -1 : static_cast<int>( i );
            }
            if( on || held ) {
                list->AddRectFilled( ImVec2( base.x, row_y ), ImVec2( base.x + list_w, row_y + h ),
                                     held ? mobile_ui::col_button_down : mobile_ui::col_accent_soft );
            }
            if( r.current ) {
                list->AddRectFilled( ImVec2( base.x, row_y + 8.0f * u ), ImVec2( base.x + 4.0f * u,
                                     row_y + h - 8.0f * u ), mobile_ui::col_accent, 2.0f * u );
            }
            const float top = row_y + ( h - text_h ) * 0.5f;
            mobile_ui::text( list, face::bold, 14.5f, ImVec2( base.x + 14.0f * u, top ),
                             r.enabled ? mobile_ui::col_fg : mobile_ui::col_muted, r.name, text_w );
            if( !r.detail.empty() ) {
                mobile_ui::text( list, face::body, 12.0f, ImVec2( base.x + 14.0f * u,
                                 top + name_px.y + 2.0f * u ), mobile_ui::col_muted, r.detail, text_w );
            }
            if( !r.right.empty() ) {
                mobile_ui::text( list, face::mono, 11.0f, ImVec2( base.x + list_w - 14.0f * u - right_w + 10.0f * u,
                                 row_y + ( h - mobile_ui::text_size( face::mono, 11.0f, r.right ).y ) * 0.5f ),
                                 r.current ? mobile_ui::col_accent : mobile_ui::col_muted, r.right );
            }
            row_y += h;
            list->AddLine( ImVec2( base.x, row_y ), ImVec2( base.x + list_w, row_y ),
                           IM_COL32( 255, 255, 255, 13 ), std::max( 1.0f, u ) );
        }
        if( !any ) {
            mobile_ui::text( list, face::body, 14.0f, ImVec2( base.x + 14.0f * u, row_y + 20.0f * u ),
                             mobile_ui::col_muted, what.empty_text.empty() ? _( "Nothing here." ) : what.empty_text,
                             list_w - 28.0f * u );
            row_y += 60.0f * u;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, row_y - base.y + 8.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // a tap on the selected row has just closed its panel
    if( has_panel && selected >= 0 && static_cast<size_t>( selected ) < what.rows.size() ) {
        if( landscape ) {
            draw_panel( ImVec2( left + list_w + 6.0f * u, y ), ImVec2( left + width - 10.0f * u,
                        bottom - 10.0f * u ) );
        } else {
            draw_panel( ImVec2( left + 10.0f * u, y + list_h + 6.0f * u ), ImVec2( left + width - 10.0f * u,
                        bottom - 10.0f * u ) );
        }
    }
}

} // namespace

int show( const spec &what, int &tab, int &selected )
{
    return show_all( what, tab, selected ).row;
}

outcome show_all( const spec &what, int &tab, int &selected )
{
    spec clean = what;
    clean.title = remove_color_tags( what.title );
    for( row &r : clean.rows ) {
        r.name = remove_color_tags( r.name );
        r.detail = remove_color_tags( r.detail );
    }
    list_window window( clean, std::clamp( tab, 0, std::max( 0,
                                           static_cast<int>( clean.tabs.size() ) - 1 ) ), selected );
    // open on a tab that has something
    const auto filled = [&clean]( const int which ) {
        return std::any_of( clean.rows.begin(), clean.rows.end(), [which]( const row & r ) {
            return r.tab == which;
        } );
    };
    if( !clean.tabs.empty() && window.selected < 0 && !filled( window.tab ) ) {
        for( int i = 0; i < static_cast<int>( clean.tabs.size() ); ++i ) {
            if( filled( i ) ) {
                window.tab = i;
                break;
            }
        }
    }
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // the first frame still draws what is under the screen (look-around's banner
    // ends up over it); the screen hides those from the second frame on
    ui_manager::redraw();
    while( !window.back && !window.acted && window.button < 0 ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            window.back = true;
        }
    }
    tab = window.tab;
    selected = window.selected;
    outcome result;
    if( window.acted ) {
        result.row = window.selected;
        result.action = window.action;
    } else if( window.button >= 0 ) {
        result.button = window.button;
    }
    return result;
}

#else // !TILES

int show( const spec &, int &, int & )
{
    return -1;
}

outcome show_all( const spec &, int &, int & )
{
    return outcome();
}

#endif // TILES

} // namespace mobile_list
