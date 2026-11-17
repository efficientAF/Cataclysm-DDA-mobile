#include "mobile_entry.h"

#include <cstddef>

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <cstring>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "output.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_entry
{

bool wanted()
{
#if defined(TILES)
    return mobile_menu::enabled();
#else
    return false;
#endif
}

std::string keypad_press( const std::string &text, const char key, const bool fresh,
                          const bool decimal, const int max_length )
{
    std::string out = text;
    const bool negative = !out.empty() && out.front() == '-';
    if( key == '<' ) {
        if( fresh ) {
            return std::string();
        }
        if( !out.empty() ) {
            out.pop_back();
        }
        return out == "-" ? std::string() : out;
    }
    if( key == '-' ) {
        return negative ? out.substr( 1 ) : "-" + out;
    }
    if( fresh ) {
        // the first key replaces the suggestion
        out.clear();
    }
    if( key == '.' ) {
        if( !decimal || out.find( '.' ) != std::string::npos ) {
            return out;
        }
        if( out.empty() || out == "-" ) {
            out += '0';
        }
    } else if( key < '0' || key > '9' ) {
        return out;
    } else if( out == "0" ) {
        // no leading zeros
        out.clear();
    } else if( out == "-0" ) {
        out = "-";
    }
    if( max_length >= 0 && out.size() >= static_cast<size_t>( max_length ) ) {
        return out;
    }
    out += key;
    return out;
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

class entry_window : public cataimgui::window
{
    public:
        explicit entry_window( const spec &what ) :
            cataimgui::window( "MOBILE_ENTRY",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            text( what.text ), what( what ) {
            // newest first, without repeats or the suggestion itself
            for( auto it = what.history.rbegin(); it != what.history.rend() && recent.size() < 6; ++it ) {
                if( !it->empty() && *it != what.text &&
                    std::find( recent.begin(), recent.end(), *it ) == recent.end() ) {
                    recent.push_back( *it );
                }
            }
        }
        std::string text;
        // the text is still the suggestion: the first keypad key replaces it
        bool fresh = true;
        bool confirmed = false;
        bool cancelled = false;
        void press( char key );
    protected:
        cataimgui::bounds get_bounds() override {
            // one pixel short of the screen: what is under the card keeps being
            // redrawn and shows dimmed
            const ImVec2 screen = ImGui::GetMainViewport()->Size;
            return { 0.0f, 0.0f, screen.x, std::max( screen.y - 1.0f, 2.0f ) };
        }
        void draw_controls() override;
    private:
        // a button with a label; returns true when tapped
        bool button( ImDrawList *draw, const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                     const std::string &label, bool primary, float text_size = 15.0f );
        float draw_head( ImDrawList *draw, const ImVec2 &pos, float width );
        float draw_recent( ImDrawList *draw, const ImVec2 &pos, float width );
        void draw_number( const ImVec2 &origin, const ImVec2 &size );
        void draw_text( const ImVec2 &origin, const ImVec2 &size );
        const spec &what;
        std::vector<std::string> recent;
        bool focus_field = true;
        bool skip_field = false;
};

void entry_window::press( const char key )
{
    text = keypad_press( text, key, fresh, what.decimal, what.max_length );
    fresh = false;
}

bool entry_window::button( ImDrawList *const draw, const std::string &id, const ImVec2 &pos,
                           const ImVec2 &size, const std::string &label, const bool primary,
                           const float text_size )
{
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ),
                    primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                    ( held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft ),
                    primary ? mobile_ui::col_accent : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, text_size, label );
    mobile_ui::text( draw, face::bold, text_size, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

// The question and what upstream says about it.  Returns the height used;
// without a draw list it only measures.
float entry_window::draw_head( ImDrawList *const draw, const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    float y = pos.y;
    if( !what.title.empty() ) {
        if( draw != nullptr ) {
            mobile_ui::text( draw, face::display, 20.0f, ImVec2( pos.x, y ), mobile_ui::col_title,
                             what.title, width );
        }
        y += mobile_ui::text_size( face::display, 20.0f, what.title, width ).y + 4.0f * u;
    }
    if( !what.description.empty() ) {
        if( draw != nullptr ) {
            mobile_ui::text( draw, face::body, 13.0f, ImVec2( pos.x, y ), mobile_ui::col_muted,
                             what.description, width );
        }
        y += mobile_ui::text_size( face::body, 13.0f, what.description, width ).y + 4.0f * u;
    }
    return y - pos.y + 6.0f * u;
}

// Earlier answers as chips; tapping one takes it.  Returns the height used.
float entry_window::draw_recent( ImDrawList *const draw, const ImVec2 &pos, const float width )
{
    if( recent.empty() ) {
        return 0.0f;
    }
    const float u = mobile_ui::unit();
    const float h = 34.0f * u;
    float x = pos.x;
    draw->PushClipRect( pos, ImVec2( pos.x + width, pos.y + h ), true );
    for( size_t i = 0; i < recent.size(); ++i ) {
        const float w = std::min( mobile_ui::text_size( face::bold, 13.0f, recent[i] ).x + 24.0f * u,
                                  width * 0.6f );
        if( x + w > pos.x + width ) {
            break;
        }
        if( button( draw, "##entry_recent" + std::to_string( i ), ImVec2( x, pos.y ), ImVec2( w, h ),
                    recent[i], false, 13.0f ) ) {
            text = recent[i];
            fresh = false;
        }
        x += w + 6.0f * u;
    }
    draw->PopClipRect();
    return h + 10.0f * u;
}

// A sheet at the bottom: the number, a keypad, Cancel and OK.
void entry_window::draw_number( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float pad = 16.0f * u;
    const float w = std::min( size.x - safe.x - safe.z, 420.0f * u );
    const float inner = w - 2.0f * pad;
    const float key_h = 46.0f * u;
    const float gap = 6.0f * u;
    const float head_h = draw_head( nullptr, ImVec2( 0.0f, 0.0f ), inner );
    const float recent_h = recent.empty() ? 0.0f : 44.0f * u;
    const float h = 14.0f * u + head_h + 52.0f * u + 10.0f * u + recent_h + 4.0f * ( key_h + gap ) +
                    6.0f * u + 48.0f * u + pad + safe.w;
    const ImVec2 min( origin.x + ( size.x - w ) * 0.5f, origin.y + size.y - h );
    const ImVec2 max( min.x + w, origin.y + size.y );
    draw->AddRectFilled( min, max, mobile_ui::col_sheet, 20.0f * u, ImDrawFlags_RoundCornersTop );
    draw->AddRect( min, ImVec2( max.x, max.y + 20.0f * u ), mobile_ui::col_line, 20.0f * u,
                   ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    float y = min.y + 14.0f * u;
    const float x = min.x + pad;
    y += draw_head( draw, ImVec2( x, y ), inner );

    // the number so far; orange while it is still the suggestion
    mobile_ui::box( draw, ImVec2( x, y ), ImVec2( x + inner, y + 52.0f * u ), col_field,
                    mobile_ui::col_accent, 12.0f );
    const std::string shown = text.empty() ? std::string( "0" ) : text;
    const ImVec2 shown_px = mobile_ui::text_size( face::mono, 22.0f, shown );
    mobile_ui::text( draw, face::mono, 22.0f, ImVec2( x + 14.0f * u, y + ( 52.0f * u - shown_px.y ) *
                     0.5f ), text.empty() ? mobile_ui::col_muted : fresh ? mobile_ui::col_accent :
                     mobile_ui::col_fg, shown );
    y += 62.0f * u;
    y += draw_recent( draw, ImVec2( x, y ), inner );

    // four columns: the digits, and delete, sign and point down the right side
    static constexpr std::array<const char *, 16> keys = { {
            "1", "2", "3", "<", "4", "5", "6", "-", "7", "8", "9", ".", "", "0", "", ""
        }
    };
    const float key_w = ( inner - 3.0f * gap ) / 4.0f;
    for( size_t i = 0; i < keys.size(); ++i ) {
        const char key = keys[i][0];
        if( key == '\0' || ( key == '.' && !what.decimal ) ) {
            continue;
        }
        const ImVec2 pos( x + static_cast<float>( i % 4 ) * ( key_w + gap ),
                          y + static_cast<float>( i / 4 ) * ( key_h + gap ) );
        const std::string label = key == '<' ? _( "Del" ) : key == '-' ? "+/−" : std::string( 1, key );
        if( button( draw, "##entry_key" + std::to_string( i ), pos, ImVec2( key_w, key_h ), label,
                    false, key >= '0' && key <= '9' ? 19.0f : 14.0f ) ) {
            press( key );
        }
    }
    y += 4.0f * ( key_h + gap ) + 6.0f * u;
    const float half = ( inner - 8.0f * u ) * 0.5f;
    if( button( draw, "##entry_cancel", ImVec2( x, y ), ImVec2( half, 48.0f * u ), _( "Cancel" ),
                false ) ) {
        cancelled = true;
    }
    if( button( draw, "##entry_ok", ImVec2( x + half + 8.0f * u, y ), ImVec2( half, 48.0f * u ),
                _( "OK" ), true ) ) {
        confirmed = true;
    }
}

// A card at the top, clear of the system keyboard: the field, Cancel and OK.
void entry_window::draw_text( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float pad = 16.0f * u;
    const float w = std::min( size.x - safe.x - safe.z - 24.0f * u, 460.0f * u );
    const float inner = w - 2.0f * pad;
    const float head_h = draw_head( nullptr, ImVec2( 0.0f, 0.0f ), inner );
    const float recent_h = recent.empty() ? 0.0f : 44.0f * u;
    const float h = pad + head_h + 48.0f * u + 10.0f * u + recent_h + 46.0f * u + pad;
    const ImVec2 min( origin.x + ( size.x - w ) * 0.5f, origin.y + safe.y + 12.0f * u );
    const ImVec2 max( min.x + w, min.y + h );
    mobile_ui::box( draw, min, max, mobile_ui::col_sheet, mobile_ui::col_line, 18.0f );
    const float x = min.x + pad;
    float y = min.y + pad;
    y += draw_head( draw, ImVec2( x, y ), inner );

    ImGui::PushFont( body_font(), 16.0f * u );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 12.0f * u,
                         ( 48.0f * u - ImGui::GetFontSize() ) * 0.5f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 12.0f * u );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, 2.0f * u ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, col_field );
    ImGui::PushStyleColor( ImGuiCol_Border, mobile_ui::col_accent );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::SetCursorScreenPos( ImVec2( x, y ) );
    ImGui::SetNextItemWidth( inner );
    if( skip_field ) {
        skip_field = false;
        focus_field = true;
        ImGui::Dummy( ImVec2( inner, 48.0f * u ) );
    } else {
        if( focus_field ) {
            // brings up the system keyboard
            ImGui::SetKeyboardFocusHere();
            focus_field = false;
        }
        if( ImGui::InputText( "##entry_text", &text, ImGuiInputTextFlags_EnterReturnsTrue ) ) {
            confirmed = true;
        }
    }
    if( what.max_length >= 0 && text.size() > static_cast<size_t>( what.max_length ) ) {
        text.resize( static_cast<size_t>( what.max_length ) );
    }
    ImGui::PopStyleColor( 3 );
    ImGui::PopStyleVar( 3 );
    ImGui::PopFont();
    y += 58.0f * u;
    const std::string before = text;
    y += draw_recent( draw, ImVec2( x, y ), inner );
    if( text != before ) {
        // ImGui keeps its own copy of the text while the field is active:
        // skip the field for a frame so that it lets go, then focus it again
        skip_field = true;
    }
    const float half = ( inner - 8.0f * u ) * 0.5f;
    if( button( draw, "##entry_cancel", ImVec2( x, y ), ImVec2( half, 46.0f * u ), _( "Cancel" ),
                false ) ) {
        cancelled = true;
    }
    if( button( draw, "##entry_ok", ImVec2( x + half + 8.0f * u, y ), ImVec2( half, 46.0f * u ),
                _( "OK" ), true ) ) {
        confirmed = true;
    }
}

void entry_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( viewport->Pos, ImVec2( viewport->Pos.x + viewport->Size.x,
                         viewport->Pos.y + viewport->Size.y ), mobile_ui::col_dim );
    if( what.number ) {
        draw_number( viewport->Pos, viewport->Size );
    } else {
        draw_text( viewport->Pos, viewport->Size );
    }
    draw->PopClipRect();
}
} // namespace

std::optional<std::string> query( const spec &what )
{
    spec clean = what;
    clean.title = remove_color_tags( what.title );
    clean.description = remove_color_tags( what.description );
    entry_window window( clean );
    input_context ctxt( "default", keyboard_mode::keychar );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    if( !clean.number ) {
        // the field takes the keys; see cataimgui's text input
        ctxt.set_timeout( 50 );
    }
    while( !window.confirmed && !window.cancelled ) {
        mobile_ui::keep_map_under();
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            return std::nullopt;
        }
        if( clean.number && action == "ANY_INPUT" ) {
            // a hardware keyboard works the keypad too
            const input_event event = ctxt.get_raw_input();
            if( event.type == input_event_t::keyboard_char && !event.sequence.empty() ) {
                const int key = event.get_first_input();
                if( key == '\n' ) {
                    window.confirmed = true;
                } else if( key == KEY_BACKSPACE ) {
                    window.press( '<' );
                } else if( ( key >= '0' && key <= '9' ) || key == '.' || key == '-' ) {
                    window.press( static_cast<char>( key ) );
                }
            }
        }
    }
    if( window.cancelled ) {
        return std::nullopt;
    }
    return window.text;
}

#else // !TILES

std::optional<std::string> query( const spec & )
{
    return std::nullopt;
}

#endif // TILES

} // namespace mobile_entry
