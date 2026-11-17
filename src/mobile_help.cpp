#include "mobile_help.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cata_imgui.h"
#include "character.h"
#include "help.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "npc.h"
#include "output.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_help
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

class help_screen : public cataimgui::window
{
    public:
        help_screen() : cataimgui::window( "MOBILE_HELP",
                                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) {}
        // the back arrow was tapped
        bool back = false;
        // true while a category is open
        bool reading() const {
            return opened;
        }
        void open( int option );
        void close() {
            opened = false;
        }
        // the category before or after the open one, around the ends
        void turn( bool forward );
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        void draw_list( const ImVec2 &pos, const ImVec2 &size );
        void draw_page( const ImVec2 &pos, const ImVec2 &size );
        help_data &data = get_help();
        bool opened = false;
        int option = 0;
        // the open category's paragraphs, translated and with their tags filled in
        std::vector<std::pair<std::string, message_modifier>> paragraphs;
};

void help_screen::open( const int which )
{
    const std::map<const int, const help_category> &cats = data.get_help_categories();
    const auto found = cats.find( which );
    if( found == cats.end() ) {
        return;
    }
    option = which;
    opened = true;
    paragraphs.clear();
    const Character &you = get_player_character();
    for( const auto &[text, modifier] : found->second.paragraphs ) {
        std::string translated = text.translated();
        if( modifier != MM_SEPARATOR ) {
            parse_tags( translated, you, you );
            translated = remove_color_tags( translated );
        }
        paragraphs.emplace_back( translated, modifier );
    }
    data.mark_read( which );
}

void help_screen::turn( const bool forward )
{
    const std::map<const int, const help_category> &cats = data.get_help_categories();
    auto it = cats.find( option );
    if( it == cats.end() ) {
        return;
    }
    if( forward ) {
        ++it;
        if( it == cats.end() ) {
            it = cats.begin();
        }
    } else {
        if( it == cats.begin() ) {
            it = cats.end();
        }
        --it;
    }
    open( it->first );
}

void help_screen::draw_list( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##help_list", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        // two columns where the screen is wide
        const int columns = size.x > size.y ? 2 : 1;
        const float column_w = size.x / static_cast<float>( columns );
        const float row_h = 52.0f * u;
        const std::map<const int, const help_category> &cats = data.get_help_categories();
        const int per_column = ( static_cast<int>( cats.size() ) + columns - 1 ) / columns;
        int index = 0;
        for( const std::pair<const int, const help_category> &cat : cats ) {
            const ImVec2 at( base.x + static_cast<float>( index / per_column ) * column_w,
                             base.y + static_cast<float>( index % per_column ) * row_h );
            bool held = false;
            if( mobile_ui::pressable( "##help_row" + std::to_string( cat.first ), at,
                                      ImVec2( column_w, row_h ), &held ) ) {
                open( cat.first );
            }
            if( held ) {
                draw->AddRectFilled( at, ImVec2( at.x + column_w, at.y + row_h ), mobile_ui::col_button_down );
            }
            const std::string name = remove_color_tags( cat.second.name.translated() );
            const ImVec2 name_px = mobile_ui::text_size( face::bold, 14.5f, name );
            draw->PushClipRect( at, ImVec2( at.x + column_w - 34.0f * u, at.y + row_h ), true );
            // what was read already is dimmed, as upstream has it
            mobile_ui::text( draw, face::bold, 14.5f, ImVec2( at.x + 14.0f * u,
                             at.y + ( row_h - name_px.y ) * 0.5f ),
                             data.is_read( cat.first ) ? mobile_ui::col_muted : mobile_ui::col_fg, name );
            draw->PopClipRect();
            mobile_ui::text( draw, face::bold, 16.0f, ImVec2( at.x + column_w - 26.0f * u,
                             at.y + ( row_h - mobile_ui::text_size( face::bold, 16.0f, "›" ).y ) * 0.5f ),
                             mobile_ui::col_muted, "›" );
            draw->AddLine( ImVec2( at.x, at.y + row_h ), ImVec2( at.x + column_w, at.y + row_h ),
                           IM_COL32( 255, 255, 255, 13 ), std::max( 1.0f, u ) );
            index++;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, static_cast<float>( per_column ) * row_h + 8.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void help_screen::draw_page( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    // a child of its own for each category, so that each opens at its top
    ImGui::BeginChild( ( "##help_page" + std::to_string( option ) ).c_str(), size,
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const float pad = 16.0f * u;
        // lines that run across a wide screen are hard to read
        const float width = std::min( size.x - 2.0f * pad, 560.0f * u );
        const float x = base.x + pad;
        float y = base.y + 14.0f * u;
        for( const std::pair<std::string, message_modifier> &paragraph : paragraphs ) {
            switch( paragraph.second ) {
                case MM_SUBTITLE:
                    y += 6.0f * u;
                    mobile_ui::text( draw, face::display, 18.0f, ImVec2( x, y ), mobile_ui::col_title,
                                     paragraph.first, width );
                    y += mobile_ui::text_size( face::display, 18.0f, paragraph.first, width ).y;
                    break;
                case MM_MONOFONT:
                    // diagrams: never wrapped, and small enough for most to fit
                    mobile_ui::text( draw, face::mono, 12.5f, ImVec2( x, y ), mobile_ui::col_text_soft,
                                     paragraph.first );
                    y += mobile_ui::text_size( face::mono, 12.5f, paragraph.first ).y;
                    break;
                case MM_SEPARATOR:
                    draw->AddLine( ImVec2( x, y ), ImVec2( x + width, y ), mobile_ui::col_line,
                                   std::max( 1.0f, u ) );
                    break;
                case MM_NORMAL:
                default:
                    mobile_ui::text( draw, face::body, 14.5f, ImVec2( x, y ), mobile_ui::col_text_soft,
                                     paragraph.first, width );
                    y += mobile_ui::text_size( face::body, 14.5f, paragraph.first, width ).y;
                    break;
            }
            y += 12.0f * u;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, y - base.y + 16.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void help_screen::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    std::string title = _( "Help" );
    if( opened ) {
        const std::map<const int, const help_category> &cats = data.get_help_categories();
        const auto found = cats.find( option );
        if( found != cats.end() ) {
            title = remove_color_tags( found->second.name.translated() );
        }
    }
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( title, ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        back = true;
    }
    const ImVec2 pos( left, origin.y + safe.y + bar_h );
    const ImVec2 room( width, origin.y + size.y - safe.w - pos.y );
    if( opened ) {
        draw_page( pos, room );
    } else {
        draw_list( pos, room );
    }
}

} // namespace

void show()
{
    help_screen screen;
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    // a swipe sideways arrives as an arrow key: the next or the previous category
    ctxt.register_leftright();
    ctxt.register_action( "ANY_INPUT" );
    while( true ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" || screen.back ) {
            screen.back = false;
            if( !screen.reading() ) {
                return;
            }
            screen.close();
        } else if( screen.reading() && ( action == "LEFT" || action == "RIGHT" ) ) {
            screen.turn( action == "RIGHT" );
        }
    }
}

#else // !TILES

void show()
{
}

#endif // TILES

} // namespace mobile_help
