#include "mobile_talk.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <cstddef>

#include "imgui/imgui.h"
#include "mobile_ui.h"
#include "output.h"
#include "translations.h"
#endif

namespace mobile_talk
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

int draw( const std::string &name, const std::string &about, const std::vector<message> &history,
          const std::vector<std::string> &replies )
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

    const float avail = size.x - safe.x - safe.z;
    // side by side when the screen is wide: what was said left, the replies right
    const bool wide = size.x > size.y;
    const float width = std::min( avail, ( wide ? 900.0f : 640.0f ) * u );
    const float left = origin.x + safe.x + ( avail - width ) * 0.5f;
    const float pad = 14.0f * u;
    float y = origin.y + safe.y + 12.0f * u;

    // who it is
    const std::string who = remove_color_tags( name );
    mobile_ui::text( back, face::display, 22.0f, ImVec2( left + pad, y ), mobile_ui::col_title, who );
    y += mobile_ui::text_size( face::display, 22.0f, who ).y + 2.0f * u;
    const std::string line = remove_color_tags( about );
    if( !line.empty() ) {
        const float wrap = width - 2.0f * pad;
        back->PushClipRect( ImVec2( left, y ), ImVec2( left + width, y + 34.0f * u ), true );
        mobile_ui::text( back, face::body, 12.0f, ImVec2( left + pad, y ), mobile_ui::col_muted, line,
                         wrap );
        back->PopClipRect();
        y += std::min( 34.0f * u, mobile_ui::text_size( face::body, 12.0f, line, wrap ).y ) + 4.0f * u;
    }
    y += 6.0f * u;
    back->AddLine( ImVec2( left, y ), ImVec2( left + width, y ), mobile_ui::col_line,
                   std::max( 1.0f, u ) );
    back->PopClipRect();

    // the prompt bar sits under everything
    const float bottom = origin.y + size.y - safe.w - 68.0f * u;
    // the replies take what they need, up to a little over half of the room
    const float history_w = wide ? width * 0.5f : width;
    const float replies_w = wide ? width - history_w : width;
    const float reply_wrap = replies_w - 2.0f * pad - 24.0f * u;
    float replies_h = 0.0f;
    std::vector<float> heights;
    for( const std::string &reply : replies ) {
        const float h = std::max( 48.0f * u,
                                  mobile_ui::text_size( face::bold, 14.0f, remove_color_tags( reply ), reply_wrap ).y + 20.0f * u );
        heights.push_back( h );
        replies_h += h + 6.0f * u;
    }
    const float room = bottom - y;
    const float replies_view = wide ? room : std::min( replies_h + 6.0f * u, room * 0.58f );
    const float history_h = wide ? room : room - replies_view;

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );

    // what was said, the newest at the bottom and in view
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::BeginChild( "##talk_history", ImVec2( history_w, history_h ), ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const float wrap = history_w - 2.0f * pad - 20.0f * u;
        float line_y = base.y + 10.0f * u;
        for( const message &m : history ) {
            const std::string text = remove_color_tags( m.text );
            if( text.empty() ) {
                continue;
            }
            const ImVec2 text_px = mobile_ui::text_size( face::body, 14.0f, text, wrap );
            const ImVec2 min( base.x + pad, line_y );
            const ImVec2 max( min.x + std::min( wrap, text_px.x ) + 20.0f * u, min.y + text_px.y + 14.0f * u );
            mobile_ui::box( draw, min, max, IM_COL32( 255, 255, 255, 13 ), 0, 12.0f );
            // a stripe in the speaker's color
            draw->AddRectFilled( ImVec2( min.x, min.y + 8.0f * u ), ImVec2( min.x + 3.0f * u,
                                 max.y - 8.0f * u ), m.color, 2.0f * u );
            mobile_ui::text( draw, face::body, 14.0f, ImVec2( min.x + 10.0f * u, min.y + 7.0f * u ),
                             mobile_ui::col_text_soft, text, wrap );
            line_y = max.y + 8.0f * u;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, line_y - base.y + 4.0f * u ) );
        // follow the conversation as it grows
        static size_t seen = 0;
        static float seen_height = 0.0f;
        if( history.size() != seen || line_y - base.y != seen_height ) {
            seen = history.size();
            seen_height = line_y - base.y;
            ImGui::SetScrollY( ImGui::GetScrollMaxY() + line_y );
        }
    }
    ImGui::EndChild();

    // the replies
    int tapped = -1;
    ImGui::SetCursorScreenPos( wide ? ImVec2( left + history_w, y ) : ImVec2( left, y + history_h ) );
    ImGui::BeginChild( "##talk_replies", ImVec2( replies_w, replies_view ),
                       ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    {
        // new replies start at the first one, not where the last list was left
        static std::vector<std::string> shown;
        if( shown != replies ) {
            shown = replies;
            ImGui::SetScrollY( 0.0f );
        }
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        float row_y = base.y + 6.0f * u;
        for( size_t i = 0; i < replies.size(); ++i ) {
            const std::string text = remove_color_tags( replies[i] );
            const ImVec2 min( base.x + pad, row_y );
            const ImVec2 max( base.x + replies_w - pad, row_y + heights[i] );
            bool held = false;
            if( mobile_ui::pressable( "##talk_reply" + std::to_string( i ), min,
                                      ImVec2( max.x - min.x, heights[i] ), &held ) ) {
                tapped = static_cast<int>( i );
            }
            mobile_ui::box( draw, min, max, held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255,
                            15 ), mobile_ui::col_line, 12.0f );
            const ImVec2 text_px = mobile_ui::text_size( face::bold, 14.0f, text, reply_wrap );
            mobile_ui::text( draw, face::bold, 14.0f, ImVec2( min.x + 12.0f * u,
                             min.y + ( heights[i] - text_px.y ) * 0.5f ), mobile_ui::col_fg, text, reply_wrap );
            row_y = max.y + 6.0f * u;
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, row_y - base.y ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return tapped;
}

#else // !TILES

int draw( const std::string &, const std::string &, const std::vector<message> &,
          const std::vector<std::string> & )
{
    return -1;
}

#endif // TILES

} // namespace mobile_talk
