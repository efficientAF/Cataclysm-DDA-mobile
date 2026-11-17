#include "mobile_compare.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <string>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_richtext.h"
#include "mobile_ui.h"
#include "output.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_compare
{

bool wanted()
{
    return mobile_menu::enabled();
}

#if defined(TILES)

using mobile_ui::face;

namespace
{

std::string fit( const face f, const float size, std::string text, const float max_width )
{
    if( mobile_ui::text_size( f, size, text ).x <= max_width ) {
        return text;
    }
    const std::string dots = "…";
    while( !text.empty() ) {
        do {
            text.pop_back();
        } while( !text.empty() && ( static_cast<unsigned char>( text.back() ) & 0xC0 ) == 0x80 );
        if( mobile_ui::text_size( f, size, text + dots ).x <= max_width ) {
            break;
        }
    }
    return text + dots;
}

class compare_window : public cataimgui::window
{
    public:
        compare_window( const std::string &first_name, const std::string &first_text,
                        const std::string &second_name, const std::string &second_text,
                        const std::string &question ) :
            cataimgui::window( "MOBILE_COMPARE",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            question( question ) {
            names[0] = first_name;
            names[1] = second_name;
            texts[0].set( first_text, mobile_ui::col_text_soft, true );
            texts[1].set( second_text, mobile_ui::col_text_soft, true );
        }
        bool done = false;
        bool confirmed = false;
        bool refresh = false;
        // which item shows in portrait
        int shown = 0;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        std::string names[2];
        mobile_richtext::block texts[2];
        std::string question;

        void draw_text( int which, const ImVec2 &pos, const ImVec2 &size );
        bool button( const std::string &id, const std::string &label, const ImVec2 &pos,
                     const ImVec2 &size, bool primary );
};

bool compare_window::button( const std::string &id, const std::string &label, const ImVec2 &pos,
                             const ImVec2 &size, const bool primary )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    const ImVec2 max( pos.x + size.x, pos.y + size.y );
    if( primary ) {
        mobile_ui::box( draw, pos, max, held ? mobile_ui::col_accent_down : mobile_ui::col_accent, 0,
                        12.0f );
    } else {
        mobile_ui::box( draw, pos, max, held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft,
                        mobile_ui::col_line, 12.0f );
    }
    const ImVec2 label_size = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + ( size.x - label_size.x ) * 0.5f,
                     pos.y + ( size.y - label_size.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

void compare_window::draw_text( const int which, const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( which == 0 ? "##compare_first" : "##compare_second", size,
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float wrap = size.x - 32.0f * u;
    const float height = texts[which].height( face::body, 14.0f, wrap );
    texts[which].draw( draw, ImVec2( base.x + 16.0f * u, base.y + 12.0f * u ) );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height + 36.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void compare_window::draw_controls()
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
    if( mobile_ui::app_bar( _( "Compare" ), ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        done = true;
    }
    float top = origin.y + safe.y + bar_h;
    float bottom = origin.y + size.y - safe.w;

    // the question and its two buttons, at the bottom
    if( !question.empty() ) {
        const float pad = 16.0f * u;
        const bool wide = size.x > size.y;
        const float buttons_w = wide ? 300.0f * u : width - 2.0f * pad;
        const float text_w = wide ? width - 3.0f * pad - buttons_w : width - 2.0f * pad;
        const ImVec2 q_size = mobile_ui::text_size( face::bold, 15.0f, question, text_w );
        const float foot_h = wide ? std::max( 48.0f * u, q_size.y ) + 20.0f * u :
                             q_size.y + 48.0f * u + 30.0f * u;
        const float foot_y = bottom - foot_h;
        draw->AddLine( ImVec2( left, foot_y ), ImVec2( left + width, foot_y ), mobile_ui::col_line,
                       std::max( 1.0f, u ) );
        const float q_y = wide ? foot_y + ( foot_h - q_size.y ) * 0.5f : foot_y + 10.0f * u;
        mobile_ui::text( draw, face::bold, 15.0f, ImVec2( left + pad, q_y ), mobile_ui::col_fg, question,
                         text_w );
        const float buttons_x = wide ? left + width - pad - buttons_w : left + pad;
        const float buttons_y = wide ? foot_y + ( foot_h - 48.0f * u ) * 0.5f : q_y + q_size.y + 10.0f * u;
        const float half = ( buttons_w - 8.0f * u ) * 0.5f;
        if( button( "##compare_cancel", _( "Cancel" ), ImVec2( buttons_x, buttons_y ),
                    ImVec2( half, 48.0f * u ), false ) ) {
            done = true;
        }
        if( button( "##compare_confirm", _( "Confirm" ), ImVec2( buttons_x + half + 8.0f * u, buttons_y ),
                    ImVec2( half, 48.0f * u ), true ) ) {
            done = true;
            confirmed = true;
        }
        bottom = foot_y;
    }

    if( size.x > size.y ) {
        // side by side, each under its name
        const float col_w = width * 0.5f;
        for( int i = 0; i < 2; ++i ) {
            const float x = left + col_w * static_cast<float>( i );
            const std::string name = fit( face::bold, 16.0f, names[i], col_w - 32.0f * u );
            mobile_ui::text( draw, face::bold, 16.0f, ImVec2( x + 16.0f * u, top + 12.0f * u ),
                             mobile_ui::col_title, name );
            draw_text( i, ImVec2( x, top + 40.0f * u ), ImVec2( col_w, bottom - top - 40.0f * u ) );
        }
        draw->AddLine( ImVec2( left + col_w, top ), ImVec2( left + col_w, bottom ), mobile_ui::col_line,
                       std::max( 1.0f, u ) );
        return;
    }

    // portrait: the names are a two-way switch
    const float pad = 12.0f * u;
    const ImVec2 sw( left + pad, top + 10.0f * u );
    const float sw_w = width - 2.0f * pad;
    const float sw_h = 44.0f * u;
    mobile_ui::box( draw, sw, ImVec2( sw.x + sw_w, sw.y + sw_h ), mobile_ui::col_fill_soft,
                    mobile_ui::col_line, 12.0f );
    const float half = sw_w * 0.5f;
    for( int i = 0; i < 2; ++i ) {
        const ImVec2 pos( sw.x + half * static_cast<float>( i ), sw.y );
        if( mobile_ui::pressable( i == 0 ? "##compare_tab0" : "##compare_tab1", pos,
                                  ImVec2( half, sw_h ) ) && shown != i ) {
            shown = i;
            refresh = true;
        }
        if( shown == i ) {
            mobile_ui::box( draw, ImVec2( pos.x + 3.0f * u, pos.y + 3.0f * u ),
                            ImVec2( pos.x + half - 3.0f * u, pos.y + sw_h - 3.0f * u ),
                            mobile_ui::col_accent, 0, 9.0f );
        }
        const std::string name = fit( face::bold, 13.5f, names[i], half - 20.0f * u );
        const ImVec2 name_size = mobile_ui::text_size( face::bold, 13.5f, name );
        mobile_ui::text( draw, face::bold, 13.5f, ImVec2( pos.x + ( half - name_size.x ) * 0.5f,
                         pos.y + ( sw_h - name_size.y ) * 0.5f ),
                         shown == i ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, name );
    }
    top += sw_h + 16.0f * u;
    draw_text( shown, ImVec2( left, top ), ImVec2( width, bottom - top ) );
}

} // namespace

bool show( const std::string &first_name, const std::string &first_text,
           const std::string &second_name, const std::string &second_text,
           const std::string &question )
{
    compare_window window( remove_color_tags( first_name ), first_text,
                           remove_color_tags( second_name ), second_text,
                           remove_color_tags( question ) );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    while( true ) {
        ui_manager::redraw();
        if( window.done ) {
            break;
        }
        const bool again = window.refresh;
        window.refresh = false;
        const std::string action = again ? ctxt.handle_input( 10 ) : ctxt.handle_input();
        if( action == "QUIT" ) {
            break;
        } else if( action == "LEFT" ) {
            // a swipe to the left brings the second item in
            window.shown = 1;
            window.refresh = true;
        } else if( action == "RIGHT" ) {
            window.shown = 0;
            window.refresh = true;
        }
    }
    return window.confirmed;
}

#else // !TILES

bool show( const std::string &, const std::string &, const std::string &, const std::string &,
           const std::string & )
{
    return false;
}

#endif // TILES

} // namespace mobile_compare
