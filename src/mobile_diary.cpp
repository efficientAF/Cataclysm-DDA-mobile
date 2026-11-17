#include "mobile_diary.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "cata_path.h"
#include "catacharset.h"
#include "cursesdef.h"
#include "diary.h"
#include "mobile_list.h"
#include "mobile_menu.h"
#include "output.h"
#include "path_info.h"
#include "scores_ui.h"
#include "string_formatter.h"
#include "translations.h"

#if defined(TILES)
#include <cstring>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "ui_manager.h"
#endif

namespace mobile_diary
{

static std::string edit_title;
static bool write_first_wanted = false;

bool wanted()
{
#if defined(TILES)
    return mobile_menu::enabled();
#else
    return false;
#endif
}

void set_edit_title( const std::string &title )
{
    edit_title = title;
}

void write_first()
{
    write_first_wanted = wanted();
}

bool take_write_first()
{
    const bool was = write_first_wanted;
    write_first_wanted = false;
    return was;
}

#if defined(TILES)

using mobile_ui::face;

namespace
{

constexpr ImU32 col_field = IM_COL32( 8, 10, 7, 255 );

ImFont *body_font()
{
    for( ImFont *const font : ImGui::GetIO().Fonts->Fonts ) {
        if( std::strstr( font->GetDebugName(), "Barlow-Medium" ) != nullptr ) {
            return font;
        }
    }
    return ImGui::GetFont();
}

// The share of the screen's height that a system keyboard leaves free.
float keyboard_room()
{
#if defined(__ANDROID__)
    return 0.5f;
#else
    return 1.0f;
#endif
}

class text_window : public cataimgui::window
{
    public:
        text_window( const std::string &title, const std::string &text ) :
            cataimgui::window( "MOBILE_DIARY_TEXT",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            text( text ), kept( text ), title( title ) {}
        std::string text;
        // the text as of the last frame: Escape makes ImGui's field take back
        // what was typed, and the question that follows needs it
        std::string kept;
        bool saved = false;
        bool cancelled = false;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        bool button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                     const std::string &label, bool primary );
        std::string title;
        bool focus_field = true;
};

bool text_window::button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                          const std::string &label, const bool primary )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ),
                    primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                    ( held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft ),
                    primary ? 0 : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.0f, label );
    mobile_ui::text( draw, face::bold, 14.0f, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

void text_window::draw_controls()
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
    const float top = origin.y + safe.y;

    // Cancel and Save sit in the bar at the top, clear of the system keyboard
    const std::string cancel_label = _( "Cancel" );
    const std::string save_label = _( "Save" );
    const float cancel_w = std::max( 76.0f * u,
                                     mobile_ui::text_size( face::bold, 14.0f, cancel_label ).x + 28.0f * u );
    const float save_w = std::max( 76.0f * u,
                                   mobile_ui::text_size( face::bold, 14.0f, save_label ).x + 28.0f * u );
    const float buttons_w = cancel_w + 8.0f * u + save_w + 12.0f * u;
    float bar_h = 0.0f;
    draw->PushClipRect( ImVec2( left, top ), ImVec2( left + width - buttons_w - 6.0f * u,
                        top + 54.0f * u ), true );
    const bool back = mobile_ui::app_bar( title, ImVec2( left, top ), width, &bar_h );
    draw->PopClipRect();
    draw->AddLine( ImVec2( left, top + bar_h ), ImVec2( left + width, top + bar_h ),
                   mobile_ui::col_line, std::max( 1.0f, u ) );
    if( back ) {
        cancelled = true;
    }
    const float button_y = top + ( bar_h - 44.0f * u ) * 0.5f;
    float x = left + width - buttons_w;
    if( button( "##diary_cancel", ImVec2( x, button_y ), ImVec2( cancel_w, 44.0f * u ), cancel_label,
                false ) ) {
        cancelled = true;
    }
    x += cancel_w + 8.0f * u;
    if( button( "##diary_save", ImVec2( x, button_y ), ImVec2( save_w, 44.0f * u ), save_label,
                true ) ) {
        saved = true;
    }

    // the field takes the room that is left
    const float pad = 12.0f * u;
    const float field_y = top + bar_h + pad;
    const float room = ( size.y - safe.w ) * keyboard_room();
    const float field_h = std::max( 3.0f * 22.0f * u, origin.y + room - pad - field_y );
    ImGui::PushFont( body_font(), 16.0f * u );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 12.0f * u, 10.0f * u ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 12.0f * u );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, 2.0f * u ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, 6.0f * u );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, col_field );
    ImGui::PushStyleColor( ImGuiCol_Border, mobile_ui::col_accent );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::PushStyleColor( ImGuiCol_NavCursor, 0 );
    ImGui::SetCursorScreenPos( ImVec2( left + pad, field_y ) );
    if( focus_field ) {
        // brings up the system keyboard
        ImGui::SetKeyboardFocusHere();
        focus_field = false;
    }
    ImGui::InputTextMultiline( "##diary_text", &text, ImVec2( width - 2.0f * pad, field_h ),
                               ImGuiInputTextFlags_WordWrap );
    ImGui::PopStyleColor( 4 );
    ImGui::PopStyleVar( 4 );
    ImGui::PopFont();
    if( ImGui::IsKeyPressed( ImGuiKey_Escape, false ) ) {
        text = kept;
    } else {
        kept = text;
    }
}

} // namespace

std::pair<bool, std::string> edit_text( const std::string &text )
{
    text_window window( edit_title.empty() ? std::string( _( "Edit text" ) ) : edit_title, text );
    input_context ctxt( "default", keyboard_mode::keychar );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // the field takes the keys; see cataimgui's text input
    ctxt.set_timeout( 50 );
    while( !window.saved && !window.cancelled ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            // other keys that mean QUIT elsewhere, like the space bar, are text here
            const input_event event = ctxt.get_raw_input();
            if( event.type != input_event_t::keyboard_char || event.sequence.empty() ||
                event.get_first_input() == KEY_ESCAPE ) {
                window.cancelled = true;
            }
        }
    }
    return { window.saved, window.kept };
}

#else // !TILES

std::pair<bool, std::string> edit_text( const std::string &text )
{
    return { false, text };
}

#endif // TILES

} // namespace mobile_diary

// The first line of a page's text, cut short, for its row.
static std::string first_line( const std::string &text )
{
    const size_t from = text.find_first_not_of( " \n\t" );
    if( from == std::string::npos ) {
        return std::string();
    }
    const utf8_wrapper line( text.substr( from, text.find( '\n', from ) - from ) );
    if( line.size() <= 70 ) {
        return line.str();
    }
    return line.substr( 0, 70 ).str() + "…";
}

void diary::mobile_show( diary *const c_diary )
{
    const auto no_window = []() {
        return catacurses::window();
    };
    const auto count = [c_diary]() {
        return static_cast<int>( c_diary->pages.size() );
    };
    const auto edit = [&]() {
        mobile_diary::set_edit_title( c_diary->get_page_ptr()->entry_name() );
        c_diary->edit_page_ui( no_window );
        mobile_diary::set_edit_title( std::string() );
    };
    int tab = 0;
    int selected = count() - 1;
    if( mobile_diary::take_write_first() ) {
        // the last entry: a new page, and straight to its text
        c_diary->new_page();
        selected = c_diary->set_opened_page( count() - 1 );
        edit();
        if( c_diary->get_page_text().empty() ) {
            c_diary->delete_page();
            selected = count() - 1;
        }
    }
    while( true ) {
        mobile_list::spec what;
        what.title = string_format( _( "%s's Diary" ), c_diary->owner );
        what.header = string_format( _( "pages: %d" ), count() );
        what.empty_text = _( "The diary has no pages." );
        what.buttons = { _( "New page" ), _( "Scores" ), _( "Export" ) };
        for( int i = 0; i < count(); ++i ) {
            c_diary->set_opened_page( i );
            const diary_page &page = *c_diary->get_page_ptr();
            const bool summary = page.is_summary();
            mobile_list::row r;
            r.name = page.entry_name();
            if( summary ) {
                r.detail = c_diary->get_head_text( true );
            } else {
                r.description = c_diary->get_head_text( false ) + "\n";
                r.detail = first_line( page.m_text );
                if( r.detail.empty() ) {
                    r.detail = _( "Nothing written." );
                } else {
                    r.description += "\n" + page.m_text + "\n";
                }
                r.action = _( "Edit" );
                r.more_actions = { _( "Delete" ) };
            }
            // what upstream's side panes show: what changed since the page before
            const std::vector<std::string> &changes = c_diary->get_change_list();
            std::map<int, std::string> &descriptions = c_diary->get_desc_map();
            if( !changes.empty() && !r.description.empty() ) {
                r.description += "\n";
            }
            for( int n = 0; n < static_cast<int>( changes.size() ); ++n ) {
                r.description += changes[n] + "\n";
                // the summary's entries are lore; their text belongs with them
                const auto desc = descriptions.find( n );
                if( summary && desc != descriptions.end() && !desc->second.empty() ) {
                    r.description += desc->second + "\n\n";
                }
            }
            what.rows.push_back( r );
        }
        const mobile_list::outcome did = mobile_list::show_all( what, tab, selected );
        if( did.went_back() ) {
            break;
        }
        if( did.button == 0 ) {
            c_diary->new_page();
            selected = count() - 1;
        } else if( did.button == 1 ) {
            show_scores_ui();
        } else if( did.button == 2 ) {
            if( query_yn( _( "Export Diary as .txt?" ) ) ) {
                c_diary->export_to_txt();
                const cata_path path = PATH_INFO::world_base_save_path() / ( c_diary->owner + "s_diary.txt" );
                popup( _( "The diary was written to %s" ), path.generic_u8string() );
            }
        } else if( did.row >= 0 && did.row < count() ) {
            c_diary->set_opened_page( did.row );
            if( did.action == 0 ) {
                edit();
            } else if( count() > 1 && query_yn( _( "Really delete Page?" ) ) ) {
                c_diary->delete_page();
                selected = std::min( selected, count() - 1 );
            }
        }
    }
}
