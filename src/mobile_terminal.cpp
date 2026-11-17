#include "mobile_terminal.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "input_enums.h"
#include "mobile_prompt.h"
#include "mobile_richtext.h"
#include "mobile_ui.h"
#include "output.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_terminal
{

bool wanted()
{
    return mobile_menu::enabled();
}

#if defined(TILES)

using mobile_ui::face;

namespace
{

constexpr ImU32 col_term_bg = IM_COL32( 5, 9, 5, 255 );
constexpr ImU32 col_term_text = IM_COL32( 108, 214, 108, 255 );
constexpr ImU32 col_term_line = IM_COL32( 108, 214, 108, 60 );
constexpr ImU32 col_term_fill = IM_COL32( 108, 214, 108, 20 );
constexpr ImU32 col_term_down = IM_COL32( 108, 214, 108, 70 );
constexpr ImU32 col_term_off = IM_COL32( 108, 214, 108, 110 );

struct term_button {
    std::string label;
    bool enabled = true;
};

class terminal_window : public cataimgui::window
{
    public:
        explicit terminal_window( const std::string &name ) :
            cataimgui::window( "MOBILE_TERMINAL",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            name( name ) {}

        std::string name;
        mobile_richtext::block text;
        // the heading over the buttons, e.g. "... - Root Menu"
        std::string heading;
        std::vector<term_button> buttons;
        // the button that was tapped, or -1
        int tapped = -1;
        bool back = false;
        bool to_bottom = false;
        bool refresh = false;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        float text_height = 0.0f;
        // Draws the buttons from pos down; returns the height they take.
        float draw_buttons( const ImVec2 &pos, float width, bool draw_them );
};

float terminal_window::draw_buttons( const ImVec2 &pos, const float width, const bool draw_them )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    float y = pos.y;
    if( !heading.empty() ) {
        if( draw_them ) {
            mobile_ui::text( draw, face::mono, 12.0f, ImVec2( pos.x, y ), col_term_text, heading, width );
        }
        y += mobile_ui::text_size( face::mono, 12.0f, heading, width ).y + 10.0f * u;
    }
    // two short answers share a row; a menu is a column
    const bool row = buttons.size() == 2 && heading.empty();
    const float gap = 8.0f * u;
    const float button_w = row ? ( width - gap ) * 0.5f : width;
    for( size_t i = 0; i < buttons.size(); ++i ) {
        const term_button &b = buttons[i];
        const float wrap = button_w - 28.0f * u;
        const ImVec2 label_size = mobile_ui::text_size( face::mono, 13.5f, b.label, wrap );
        const float button_h = std::max( 46.0f * u, label_size.y + 20.0f * u );
        const ImVec2 button_pos( pos.x + ( row ? static_cast<float>( i ) * ( button_w + gap ) : 0.0f ), y );
        if( draw_them ) {
            bool held = false;
            const bool hit = mobile_ui::pressable( "##term_button" + std::to_string( i ), button_pos,
                                                   ImVec2( button_w, button_h ), &held );
            if( hit && b.enabled ) {
                tapped = static_cast<int>( i );
            }
            mobile_ui::box( draw, button_pos, ImVec2( button_pos.x + button_w, button_pos.y + button_h ),
                            held && b.enabled ? col_term_down : col_term_fill,
                            b.enabled ? col_term_line : col_term_fill, 10.0f );
            mobile_ui::text( draw, face::mono, 13.5f, ImVec2( button_pos.x + 14.0f * u,
                             button_pos.y + ( button_h - label_size.y ) * 0.5f ),
                             b.enabled ? col_term_text : col_term_off, b.label, wrap );
        }
        if( !row || i + 1 == buttons.size() ) {
            y += button_h + gap;
        }
    }
    return y - pos.y;
}

void terminal_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const back_list = ImGui::GetWindowDrawList();
    back_list->PushClipRectFullScreen();
    back_list->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ), col_term_bg );
    back_list->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( name, ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        back = true;
    }
    const float top = origin.y + safe.y + bar_h;
    const float bottom = origin.y + size.y - safe.w;
    const float pad = 16.0f * u;
    const bool wide = size.x > size.y;

    // in landscape the buttons are a column on the right; in portrait they are under the text
    const float buttons_w = wide ? std::min( 340.0f * u, width * 0.42f ) : width - 2.0f * pad;
    const float buttons_h = buttons.empty() ? 0.0f : draw_buttons( ImVec2( 0.0f, 0.0f ), buttons_w,
                            false );
    ImVec2 text_pos( left, top );
    ImVec2 text_size( width, bottom - top );
    ImVec2 buttons_pos( left + pad, top + pad );
    ImVec2 buttons_size( buttons_w, bottom - top - pad );
    if( wide ) {
        text_size.x = width - buttons_w - 2.0f * pad;
        buttons_pos.x = left + text_size.x + pad;
        back_list->AddLine( ImVec2( left + text_size.x, top ), ImVec2( left + text_size.x, bottom ),
                            col_term_line, std::max( 1.0f, u ) );
    } else if( !buttons.empty() ) {
        // the buttons take what they need, up to 60% of the room
        buttons_size.y = std::min( buttons_h + 6.0f * u, ( bottom - top ) * 0.6f );
        text_size.y = bottom - top - buttons_size.y - pad;
        buttons_pos.y = top + text_size.y + pad * 0.5f;
        back_list->AddLine( ImVec2( left, top + text_size.y ), ImVec2( left + width, top + text_size.y ),
                            col_term_line, std::max( 1.0f, u ) );
    }

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::SetCursorScreenPos( text_pos );
    ImGui::BeginChild( "##term_text", text_size, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const float height = text.height( face::mono, 12.5f, text_size.x - 2.0f * pad );
        text.draw( draw, ImVec2( base.x + pad, base.y + pad ) );
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, height + 2.0f * pad ) );
        if( height != text_height ) {
            text_height = height;
            to_bottom = true;
            refresh = true;
        } else if( to_bottom ) {
            // the newest line is the one being asked about
            ImGui::SetScrollY( ImGui::GetScrollMaxY() );
            to_bottom = false;
            refresh = true;
        }
    }
    ImGui::EndChild();

    if( !buttons.empty() ) {
        ImGui::SetCursorScreenPos( buttons_pos );
        ImGui::BeginChild( "##term_buttons", buttons_size, ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoBackground );
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const float height = draw_buttons( base, buttons_w, true );
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, height ) );
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();
}

std::unique_ptr<terminal_window> page;

// Shows the buttons and waits.  Returns the one tapped, or -1 for back.
int wait_for( const std::string &heading, std::vector<term_button> buttons, const bool any_key )
{
    if( !page ) {
        return -1;
    }
    page->heading = heading;
    page->buttons = std::move( buttons );
    page->tapped = -1;
    page->back = false;
    page->to_bottom = true;
    page->refresh = true;
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "ANY_INPUT" );
    int result = -1;
    while( true ) {
        ui_manager::redraw();
        if( page->tapped >= 0 ) {
            result = page->tapped;
            break;
        }
        if( page->back ) {
            break;
        }
        const bool again = page->refresh;
        page->refresh = false;
        const std::string action = again ? ctxt.handle_input( 10 ) : ctxt.handle_input();
        if( action == "QUIT" ) {
            break;
        }
        // a real key, as upstream's "press any key"
        if( any_key && ( action == "CONFIRM" || ( action == "ANY_INPUT" &&
                         ( ctxt.get_raw_input().type == input_event_t::keyboard_char ||
                           ctxt.get_raw_input().type == input_event_t::keyboard_code ) ) ) ) {
            result = 0;
            break;
        }
    }
    page->heading.clear();
    page->buttons.clear();
    page->tapped = -1;
    page->back = false;
    return result;
}

} // namespace

session::session( const std::string &name )
{
    page = std::make_unique<terminal_window>( remove_color_tags( name ) );
}

session::~session()
{
    page.reset();
}

bool active()
{
    return static_cast<bool>( page );
}

void print( const int indent, const std::string &text )
{
    if( page ) {
        page->text.add( text, col_term_text, indent );
        page->to_bottom = true;
    }
}

void newline()
{
    if( page ) {
        page->text.add( std::string(), col_term_text );
    }
}

void clear()
{
    if( page ) {
        page->text.clear();
    }
}

void wait_any()
{
    wait_for( std::string(), { { _( "Continue" ), true } }, true );
}

answer ask_yes_no()
{
    switch( wait_for( std::string(), { { _( "Yes" ), true }, { _( "No" ), true } }, false ) ) {
        case 0:
            return answer::yes;
        case 1:
            return answer::no;
        default:
            return answer::quit;
    }
}

int choose( const std::string &title, const std::vector<std::pair<std::string, bool>> &options )
{
    std::vector<term_button> buttons;
    for( const std::pair<std::string, bool> &option : options ) {
        buttons.push_back( { remove_color_tags( option.first ), option.second } );
    }
    buttons.push_back( { _( "Log off" ), true } );
    const int picked = wait_for( remove_color_tags( title ), buttons, false );
    return picked >= 0 && picked < static_cast<int>( options.size() ) ? picked : -1;
}

#else // !TILES

session::session( const std::string & ) {}
session::~session() = default;

bool active()
{
    return false;
}

void print( int, const std::string & ) {}
void newline() {}
void clear() {}
void wait_any() {}

answer ask_yes_no()
{
    return answer::quit;
}

int choose( const std::string &, const std::vector<std::pair<std::string, bool>> & )
{
    return -1;
}

#endif // TILES

} // namespace mobile_terminal
