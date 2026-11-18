#include "mobile_dialog.h"

#include <string>

#include "imgui/imgui.h"

#if defined(TILES)
#include <algorithm>

#include "mobile_pick.h"
#include "mobile_ui.h"
#include "translations.h"
#endif

namespace mobile_dialog
{

int window_flags()
{
    return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
}

static std::string trimmed( const std::string &text )
{
    const size_t first = text.find_first_not_of( ' ' );
    if( first == std::string::npos ) {
        return std::string();
    }
    return text.substr( first, text.find_last_not_of( ' ' ) - first + 1 );
}

std::string split_column( const std::string &text, std::string &rest )
{
    rest.clear();
    // a divider has a space before it, and is not part of a line break
    const size_t bar = text.find( " |" );
    if( bar == std::string::npos || text.find( '\n' ) != std::string::npos ) {
        return text;
    }
    rest = trimmed( text.substr( bar + 2 ) );
    return trimmed( text.substr( 0, bar ) );
}

#if defined(TILES)

using mobile_ui::face;

static constexpr ImU32 col_message = IM_COL32( 201, 207, 191, 255 );
static constexpr ImU32 col_soft_button = IM_COL32( 255, 255, 255, 15 );

void note_rect( const std::string &message, const bool on_top, float &x, float &y, float &w,
                float &h )
{
    // the same sums as in draw_popup, for a dialog without buttons
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const float pad = 20.0f * u;
    const float avail_w = size.x - safe.x - safe.z;
    const float max_w = std::min( 440.0f * u, avail_w - 32.0f * u );
    const ImVec2 message_px = message.empty() ? ImVec2( 0.0f, 0.0f ) :
                              mobile_ui::text_size( face::body, 14.0f, message, max_w - 2.0f * pad );
    const float max_h = size.y - safe.y - safe.w - 24.0f * u;
    w = std::min( max_w, message_px.x + 2.0f * pad );
    h = pad + std::min( message_px.y, std::max( 40.0f * u, max_h - 2.0f * pad ) ) + pad;
    x = viewport->Pos.x + safe.x + ( avail_w - w ) * 0.5f;
    y = on_top ? viewport->Pos.y + safe.y + 12.0f * u :
        viewport->Pos.y + safe.y + ( size.y - safe.y - safe.w - h ) * 0.5f;
    if( on_top ) {
        // under the look-around banner, which is at the top too
        y = std::max( y, mobile_pick::banner_bottom() + 6.0f * u );
    }
    // room for the outline
    x -= 2.0f * u;
    y -= 2.0f * u;
    w += 4.0f * u;
    h += 4.0f * u;
}

int draw_popup( const popup_spec &spec )
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    int result = tap_nothing;

    std::vector<std::string> labels = spec.buttons;
    const bool ok_only = labels.empty() && spec.waits;
    if( ok_only ) {
        labels.emplace_back( _( "OK" ) );
    }
    const int count = static_cast<int>( labels.size() );
    // a note without buttons leaves the screen as it is
    const bool modal = count > 0;
    // a sheet on the bottom edge in portrait, a card otherwise
    const bool sheet = modal && !landscape;

    if( modal ) {
        draw->PushClipRectFullScreen();
        mobile_ui::dim_screen( draw, origin, screen_max );
        draw->PopClipRect();
    }

    const float pad = 20.0f * u;
    const float avail_w = size.x - safe.x - safe.z;
    const float max_w = sheet ? avail_w : std::min( 440.0f * u, avail_w - 32.0f * u );
    const float text_w = max_w - 2.0f * pad;
    const ImVec2 message_px = spec.message.empty() ? ImVec2( 0.0f, 0.0f ) :
                              mobile_ui::text_size( face::body, 14.0f, spec.message, text_w );
    // a note is only as wide as its text
    const float card_w = modal ? max_w : std::min( max_w, message_px.x + 2.0f * pad );

    // buttons side by side while they fit, otherwise one under the other
    const float button_h = 48.0f * u;
    const float gap = 8.0f * u;
    int columns = std::min( count, landscape ? 3 : 2 );
    for( const std::string &label : labels ) {
        const float cell_w = ( text_w - static_cast<float>( columns - 1 ) * gap ) /
                             static_cast<float>( std::max( columns, 1 ) );
        if( columns > 1 && mobile_ui::text_size( face::bold, 15.0f, label ).x > cell_w - 16.0f * u ) {
            columns = 1;
        }
    }
    const int button_rows = columns > 0 ? ( count + columns - 1 ) / columns : 0;
    const float buttons_h = button_rows > 0 ? static_cast<float>( button_rows ) * button_h +
                            static_cast<float>( button_rows - 1 ) * gap + ( spec.message.empty() ? 0.0f : 16.0f * u ) :
                            0.0f;
    const float bottom_pad = pad + ( sheet ? safe.w : 0.0f );
    const float max_h = size.y - safe.y - ( sheet ? 0.0f : safe.w ) - 24.0f * u;
    const float message_h = std::min( message_px.y,
                                      std::max( 40.0f * u, max_h - pad - buttons_h - bottom_pad ) );
    const float card_h = pad + message_h + buttons_h + bottom_pad;

    ImVec2 pos( origin.x + safe.x + ( avail_w - card_w ) * 0.5f,
                origin.y + safe.y + ( size.y - safe.y - safe.w - card_h ) * 0.5f );
    if( sheet ) {
        pos.y = screen_max.y - card_h;
    } else if( spec.on_top ) {
        pos.y = std::max( origin.y + safe.y + 12.0f * u, mobile_pick::banner_bottom() + 6.0f * u );
    }
    const ImVec2 card_max( pos.x + card_w, pos.y + card_h );
    if( sheet ) {
        mobile_ui::sheet_area( pos, card_max );
        draw->AddRectFilled( pos, card_max, mobile_ui::col_sheet, 22.0f * u, ImDrawFlags_RoundCornersTop );
        draw->AddRect( pos, ImVec2( card_max.x, card_max.y + 30.0f * u ), mobile_ui::col_line, 22.0f * u,
                       ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    } else {
        if( modal ) {
            mobile_ui::sheet_area( pos, card_max );
        }
        mobile_ui::box( draw, pos, card_max, mobile_ui::col_sheet, mobile_ui::col_line, 18.0f );
    }

    // the message; it scrolls when it is taller than the screen allows
    if( !spec.message.empty() ) {
        ImGui::SetCursorScreenPos( ImVec2( pos.x + pad, pos.y + pad ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ImGui::BeginChild( "##dialog_message", ImVec2( text_w, message_h ), ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoBackground | ( message_px.y > message_h ? 0 :
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs ) );
        const ImVec2 base = ImGui::GetCursorScreenPos();
        mobile_ui::text( ImGui::GetWindowDrawList(), face::body, 14.0f, base, col_message, spec.message,
                         text_w );
        ImGui::Dummy( ImVec2( 1.0f, message_px.y ) );
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // the choices
    if( count > 0 ) {
        const float top = card_max.y - bottom_pad - static_cast<float>( button_rows ) * button_h -
                          static_cast<float>( button_rows - 1 ) * gap;
        const float cell_w = ( text_w - static_cast<float>( columns - 1 ) * gap ) /
                             static_cast<float>( columns );
        for( int i = 0; i < count; ++i ) {
            const ImVec2 cell( pos.x + pad + static_cast<float>( i % columns ) * ( cell_w + gap ),
                               top + static_cast<float>( i / columns ) * ( button_h + gap ) );
            const ImVec2 cell_max( cell.x + cell_w, cell.y + button_h );
            bool held = false;
            if( mobile_ui::pressable( "##dialog_button" + std::to_string( i ), cell,
                                      ImVec2( cell_w, button_h ), &held ) ) {
                result = ok_only ? tap_ok : i;
            }
            // the first choice is the affirmative one
            const bool primary = i == 0;
            const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                               ( held ? mobile_ui::col_button_down : col_soft_button );
            mobile_ui::box( draw, cell, cell_max, fill, primary ? 0 : mobile_ui::col_line, 12.0f );
            if( i == spec.selected && count > 1 ) {
                // where the keyboard is
                draw->AddRect( ImVec2( cell.x - 2.0f * u, cell.y - 2.0f * u ),
                               ImVec2( cell_max.x + 2.0f * u, cell_max.y + 2.0f * u ), mobile_ui::col_muted, 14.0f * u, 0,
                               std::max( 1.0f, u ) );
            }
            const ImVec2 label_px = mobile_ui::text_size( face::bold, 15.0f, labels[i] );
            draw->PushClipRect( cell, cell_max, true );
            mobile_ui::text( draw, face::bold, 15.0f,
                             ImVec2( cell.x + std::max( 8.0f * u, ( cell_w - label_px.x ) * 0.5f ),
                                     cell.y + ( button_h - label_px.y ) * 0.5f ),
                             primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, labels[i] );
            draw->PopClipRect();
        }
    }

    // last, so that the buttons come first where they overlap
    const bool outside = modal && mobile_ui::pressable( "##dialog_outside", origin, size );
    if( outside && result == tap_nothing ) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if( mouse.x < pos.x || mouse.x > card_max.x || mouse.y < pos.y || mouse.y > card_max.y ) {
            result = tap_outside;
        }
    }
    return result;
}

int draw_list( const list_spec &spec )
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    int result = tap_nothing;

    draw->PushClipRectFullScreen();
    mobile_ui::dim_screen( draw, origin, screen_max );
    draw->PopClipRect();
    const bool outside = mobile_ui::pressable( "##list_outside", origin, size );

    const float sheet_w = landscape ? std::min( 520.0f * u, size.x * 0.76f ) : size.x;
    const float max_h = ( size.y - safe.y ) * ( landscape ? 0.92f : 0.84f );
    const float side = 18.0f * u;
    const float body_w = sheet_w - 2.0f * side;

    // the head: a short title is set as one, a longer text as a paragraph
    const bool has_title = !spec.title.empty();
    const bool short_title = has_title && spec.title.find( '\n' ) == std::string::npos &&
                             mobile_ui::text_size( face::display, 21.0f, spec.title ).x <= body_w;
    const float title_h = !has_title ? 0.0f : short_title ?
                          mobile_ui::text_size( face::display, 21.0f, spec.title ).y :
                          std::min( mobile_ui::text_size( face::body, 14.0f, spec.title, body_w ).y, max_h * 0.4f );
    const float head_h = 22.0f * u + ( has_title ? title_h + 12.0f * u : 0.0f );

    // rows are as tall as their text needs, and at least a comfortable target
    const float min_row_h = 52.0f * u;
    const int count = static_cast<int>( spec.rows.size() );
    std::vector<float> heights;
    heights.reserve( spec.rows.size() );
    float rows_h = 0.0f;
    for( const list_row &row : spec.rows ) {
        const float hotkey_w = row.hotkey.empty() ? 0.0f :
                               mobile_ui::text_size( face::mono, 11.0f, row.hotkey ).x + 12.0f * u;
        const float text_w = body_w - hotkey_w;
        float h = mobile_ui::text_size( face::bold, 15.0f, row.text, text_w ).y;
        if( !row.detail.empty() ) {
            h += 3.0f * u + mobile_ui::text_size( face::body, 12.0f, row.detail, text_w ).y;
        }
        heights.push_back( std::max( min_row_h, h + 20.0f * u ) );
        rows_h += heights.back();
    }
    const float foot_h = safe.w + 6.0f * u;
    const float sheet_h = std::min( max_h, head_h + rows_h + foot_h );
    const ImVec2 pos( origin.x + ( size.x - sheet_w ) * 0.5f, screen_max.y - sheet_h );
    mobile_ui::sheet_area( pos, ImVec2( pos.x + sheet_w, screen_max.y ) );
    draw->AddRectFilled( pos, ImVec2( pos.x + sheet_w, screen_max.y ), mobile_ui::col_sheet, 22.0f * u,
                         ImDrawFlags_RoundCornersTop );
    draw->AddRect( pos, ImVec2( pos.x + sheet_w, screen_max.y + 30.0f * u ), mobile_ui::col_line,
                   22.0f * u, ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    // grab handle
    draw->AddRectFilled( ImVec2( pos.x + sheet_w * 0.5f - 21.0f * u, pos.y + 9.0f * u ),
                         ImVec2( pos.x + sheet_w * 0.5f + 21.0f * u, pos.y + 14.0f * u ),
                         IM_COL32( 255, 255, 255, 71 ), 3.0f * u );
    if( short_title ) {
        mobile_ui::text( draw, face::display, 21.0f, ImVec2( pos.x + side, pos.y + 22.0f * u ),
                         mobile_ui::col_fg, spec.title );
    } else if( has_title ) {
        draw->PushClipRect( ImVec2( pos.x, pos.y + 22.0f * u ),
                            ImVec2( pos.x + sheet_w, pos.y + 22.0f * u + title_h ), true );
        mobile_ui::text( draw, face::body, 14.0f, ImVec2( pos.x + side, pos.y + 22.0f * u ), col_message,
                         spec.title, body_w );
        draw->PopClipRect();
    }

    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + head_h ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##list_scroll", ImVec2( sheet_w, sheet_h - head_h - foot_h ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs );
    ImDrawList *const inner = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float view_top = ImGui::GetWindowPos().y;
    const float view_bottom = view_top + ImGui::GetWindowSize().y;
    float y = base.y;
    for( int i = 0; i < count; ++i ) {
        const list_row &row = spec.rows[i];
        const float row_h = heights[i];
        const ImVec2 row_pos( base.x, y );
        y += row_h;
        if( i == spec.selected && spec.scroll_to_selected ) {
            ImGui::SetScrollFromPosY( row_pos.y + row_h * 0.5f - view_top, 0.5f );
        }
        if( row_pos.y > view_bottom || row_pos.y + row_h < view_top ) {
            continue;
        }
        bool held = false;
        if( row.tappable ) {
            if( mobile_ui::pressable( "##list_row" + std::to_string( i ), row_pos, ImVec2( sheet_w, row_h ),
                                      &held ) ) {
                result = i;
            }
        }
        if( held ) {
            inner->AddRectFilled( row_pos, ImVec2( row_pos.x + sheet_w, row_pos.y + row_h ),
                                  mobile_ui::col_button_down );
        } else if( i == spec.selected ) {
            inner->AddRectFilled( row_pos, ImVec2( row_pos.x + sheet_w, row_pos.y + row_h ),
                                  mobile_ui::col_fill_soft );
        }
        inner->AddLine( row_pos, ImVec2( row_pos.x + sheet_w, row_pos.y ), IM_COL32( 255, 255, 255, 13 ) );
        float hotkey_w = 0.0f;
        if( !row.hotkey.empty() ) {
            const ImVec2 hotkey_px = mobile_ui::text_size( face::mono, 11.0f, row.hotkey );
            hotkey_w = hotkey_px.x + 12.0f * u;
            mobile_ui::text( inner, face::mono, 11.0f,
                             ImVec2( row_pos.x + sheet_w - side - hotkey_px.x, row_pos.y + ( row_h - hotkey_px.y ) * 0.5f ),
                             row.enabled ? mobile_ui::col_muted : IM_COL32( 154, 163, 146, 110 ), row.hotkey );
        }
        const float text_w = body_w - hotkey_w;
        const ImVec2 text_px = mobile_ui::text_size( face::bold, 15.0f, row.text, text_w );
        const float detail_h = row.detail.empty() ? 0.0f : 3.0f * u +
                               mobile_ui::text_size( face::body, 12.0f, row.detail, text_w ).y;
        const float text_y = row_pos.y + ( row_h - text_px.y - detail_h ) * 0.5f;
        mobile_ui::text( inner, face::bold, 15.0f, ImVec2( row_pos.x + side, text_y ),
                         row.enabled ? mobile_ui::col_fg : IM_COL32( 233, 236, 223, 102 ), row.text, text_w );
        if( !row.detail.empty() ) {
            mobile_ui::text( inner, face::body, 12.0f,
                             ImVec2( row_pos.x + side, text_y + text_px.y + 3.0f * u ),
                             row.enabled ? mobile_ui::col_muted : IM_COL32( 154, 163, 146, 110 ), row.detail, text_w );
        }
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if( outside && result == tap_nothing && ImGui::GetIO().MousePos.y < pos.y ) {
        result = tap_outside;
    }
    return result;
}

#else // !TILES

void note_rect( const std::string &, bool, float &, float &, float &, float & ) {}

int draw_popup( const popup_spec & )
{
    return tap_nothing;
}

int draw_list( const list_spec & )
{
    return tap_nothing;
}

#endif // TILES

} // namespace mobile_dialog
