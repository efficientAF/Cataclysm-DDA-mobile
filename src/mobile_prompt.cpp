#include "mobile_prompt.h"

#include <algorithm>
#include <cstddef>
#include <list>
#include <utility>

#include "input_context.h"
#include "options.h"

#if defined(TILES)
#include <cmath>
#include <optional>

#include "imgui/imgui.h"
#include "input.h"
#include "input_enums.h"
#include "mobile_ui.h"
#include "sdltiles.h"
#include "translations.h"
#endif

namespace mobile_prompt
{

// --- Pure helpers ---

static bool is_space( const char c )
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_word_char( const char c )
{
    return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
           ( static_cast<unsigned char>( c ) & 0x80 ) != 0;
}

static bool starts_with_at( const std::string &text, const size_t pos, const std::string &part )
{
    return text.compare( pos, part.size(), part ) == 0;
}

static bool ends_with( const std::string &text, const std::string &part )
{
    return text.size() >= part.size() &&
           text.compare( text.size() - part.size(), part.size(), part ) == 0;
}

std::string label_from_hint( const std::string &text )
{
    static const std::string ellipsis = "\xe2\x80\xa6";
    std::string label = text;
    // punctuation and filler at either end
    bool changed = true;
    while( changed && !label.empty() ) {
        changed = false;
        const char last = label.back();
        if( is_space( last ) || last == '.' || last == '!' || last == ':' || last == ',' ||
            last == ';' || last == '(' ) {
            label.pop_back();
            changed = true;
        } else if( ends_with( label, ellipsis ) ) {
            label.erase( label.size() - ellipsis.size() );
            changed = true;
        }
    }
    changed = true;
    while( changed && !label.empty() ) {
        changed = false;
        const char first = label.front();
        if( is_space( first ) || first == '.' || first == ':' || first == ',' || first == ';' ||
            first == '-' || first == ')' || first == '|' || first == '/' || first == '\\' ) {
            label.erase( 0, 1 );
            changed = true;
        } else if( starts_with_at( label, 0, ellipsis ) ) {
            label.erase( 0, ellipsis.size() );
            changed = true;
        }
    }
    // "to go back" names the action "go back"
    if( label.size() > 3 && ( label[0] == 't' || label[0] == 'T' ) && label[1] == 'o' &&
        label[2] == ' ' ) {
        label.erase( 0, 3 );
    }
    if( !label.empty() && label[0] >= 'a' && label[0] <= 'z' ) {
        label[0] = static_cast<char>( label[0] - 'a' + 'A' );
    }
    return label;
}

namespace
{
struct hint_match {
    size_t pos;
    size_t len;
    const bound_action *action;
};
} // namespace

hint_result parse_key_hints( const std::string &message,
                             const std::vector<bound_action> &actions )
{
    hint_result result;
    // longest key lists first, so that "ESC or q" is not taken for "q"
    std::vector<const bound_action *> by_length;
    for( const bound_action &action : actions ) {
        if( !action.keys.empty() ) {
            by_length.push_back( &action );
        }
    }
    std::stable_sort( by_length.begin(), by_length.end(),
    []( const bound_action * a, const bound_action * b ) {
        return a->keys.size() > b->keys.size();
    } );

    size_t line_start = 0;
    while( line_start <= message.size() ) {
        size_t line_end = message.find( '\n', line_start );
        if( line_end == std::string::npos ) {
            line_end = message.size();
        }
        const std::string line = message.substr( line_start, line_end - line_start );
        line_start = line_end + 1;

        std::vector<hint_match> matches;
        for( const bound_action *action : by_length ) {
            size_t pos = line.find( action->keys );
            while( pos != std::string::npos ) {
                const size_t end = pos + action->keys.size();
                // whole words only, and not inside an earlier match
                bool ok = ( pos == 0 || !is_word_char( line[pos - 1] ) ) &&
                          ( end == line.size() || !is_word_char( line[end] ) );
                for( const hint_match &other : matches ) {
                    ok = ok && ( end <= other.pos || pos >= other.pos + other.len );
                }
                if( ok ) {
                    matches.push_back( { pos, action->keys.size(), action } );
                }
                pos = line.find( action->keys, pos + 1 );
            }
        }
        if( matches.empty() ) {
            if( !result.remaining.empty() ) {
                result.remaining += '\n';
            }
            result.remaining += line;
            continue;
        }
        std::sort( matches.begin(), matches.end(), []( const hint_match & a, const hint_match & b ) {
            return a.pos < b.pos;
        } );
        for( size_t i = 0; i < matches.size(); ++i ) {
            const hint_match &match = matches[i];
            const size_t after = match.pos + match.len;
            const size_t next = i + 1 < matches.size() ? matches[i + 1].pos : line.size();
            std::string label = label_from_hint( line.substr( after, next - after ) );
            if( label.empty() && i == 0 ) {
                // "Cancel: ESC"
                label = label_from_hint( line.substr( 0, match.pos ) );
            }
            if( label.empty() ) {
                label = match.action->name;
            }
            result.hints.push_back( { match.action->action, label } );
        }
    }
    // a note of nothing but blank lines is no note
    if( std::all_of( result.remaining.begin(), result.remaining.end(), is_space ) ) {
        result.remaining.clear();
    }
    return result;
}

// --- The bar ---

bool enabled()
{
#if defined(TILES)
    return get_options().has_option( "MOBILE_MENUS" ) && get_option<bool>( "MOBILE_MENUS" );
#else
    return false;
#endif
}

// the key lists handed out lately, newest first
static std::list<bound_action> recent_keys;
static std::vector<const input_context *> own_contexts;

void note_keys( const std::string &action, const std::string &keys )
{
    if( keys.empty() ) {
        return;
    }
    for( auto it = recent_keys.begin(); it != recent_keys.end(); ++it ) {
        if( it->action == action && it->keys == keys ) {
            recent_keys.splice( recent_keys.begin(), recent_keys, it );
            return;
        }
    }
    recent_keys.push_front( { action, keys, std::string() } );
    if( recent_keys.size() > 24 ) {
        recent_keys.pop_back();
    }
}

own_controls::own_controls( const input_context &ctxt ) : ctxt( &ctxt )
{
    own_contexts.push_back( this->ctxt );
}

own_controls::~own_controls()
{
    const auto found = std::find( own_contexts.rbegin(), own_contexts.rend(), ctxt );
    if( found != own_contexts.rend() ) {
        own_contexts.erase( std::next( found ).base() );
    }
}

#if !defined(TILES)

std::string take_key_hints( const std::string &message )
{
    return message;
}

#else

using mobile_ui::face;

namespace
{
enum class button_kind : int {
    action,
    more,
    // shows or hides the on-screen keyboard
    keyboard,
};

struct bar_button {
    button_kind kind = button_kind::action;
    std::string action;
    std::string label;
    input_event event;
    bool primary = false;
    // where it was drawn; empty if it was not
    ImVec2 min;
    ImVec2 max;
};

enum class press_target : int {
    none,
    button,
    // the open panel, off any button: a drag scrolls it
    panel,
    // beside the open panel: closes it
    outside,
};

struct finger_state {
    press_target target = press_target::none;
    size_t button = 0;
    float down_x = 0.0f;
    float down_y = 0.0f;
    float last_y = 0.0f;
    bool dragged = false;
};
} // namespace

// the hints of the notes drawn in this frame
static std::vector<key_hint> frame_hints;
// what the last frame showed
static std::vector<bar_button> shown;
static const input_context *shown_context = nullptr;
static ImVec2 panel_min;
static ImVec2 panel_max;
static bool expanded = false;
static float scroll = 0.0f;
static float scroll_max = 0.0f;
static finger_state finger;

static input_context *waiting_context()
{
    return input_context::input_context_stack.back();
}

static bool is_registered( const input_context &ctxt, const std::string &action )
{
    const std::vector<std::string> &actions = ctxt.mobile_registered_actions();
    return std::find( actions.begin(), actions.end(), action ) != actions.end();
}

static std::optional<input_event> event_for( const input_context &ctxt,
        const std::string &action )
{
    const std::vector<input_event> keys = ctxt.keys_bound_to( action, -1, false );
    if( keys.empty() ) {
        return std::nullopt;
    }
    // a plain key if there is one; modifiers do not survive every input path
    for( const input_event &key : keys ) {
        if( key.modifiers.empty() ) {
            return key;
        }
    }
    return keys.front();
}

std::string take_key_hints( const std::string &message )
{
    if( !enabled() || recent_keys.empty() ) {
        return message;
    }
    const input_context *const ctxt = waiting_context();
    if( ctxt == nullptr ) {
        return message;
    }
    // only the keys that the waiting context answers to
    std::vector<bound_action> candidates;
    for( const bound_action &recent : recent_keys ) {
        if( is_registered( *ctxt, recent.action ) && event_for( *ctxt, recent.action ) ) {
            candidates.push_back( { recent.action, recent.keys, ctxt->get_action_name( recent.action ) } );
        }
    }
    hint_result parsed = parse_key_hints( message, candidates );
    if( parsed.hints.empty() ) {
        return message;
    }
    for( key_hint &hint : parsed.hints ) {
        const auto same = [&hint]( const key_hint & other ) {
            return other.action == hint.action;
        };
        if( std::none_of( frame_hints.begin(), frame_hints.end(), same ) ) {
            frame_hints.push_back( std::move( hint ) );
        }
    }
    return parsed.remaining;
}

// Actions that are not worth a button: moving a cursor or a selection is done
// by swiping or tapping, and the rest is never typed by hand.
static bool skip_action( const std::string &action )
{
    static const std::vector<std::string> skipped = {
        "ANY_INPUT", "COORDINATE", "MOUSE_MOVE", "SELECT", "SEC_SELECT", "CLICK_AND_DRAG",
        "SCROLL_UP", "SCROLL_DOWN", "UP", "DOWN", "LEFT", "RIGHT", "LEFTUP", "LEFTDOWN",
        "RIGHTUP", "RIGHTDOWN", "TIMEOUT", "toggle_language_to_en", "ERROR"
    };
    return std::find( skipped.begin(), skipped.end(), action ) != skipped.end() ||
           action.rfind( "TEXT.", 0 ) == 0;
}

static std::string action_label( const input_context &ctxt, const std::string &action )
{
    // short names for the two every screen has, unless the screen named them itself
    if( !ctxt.mobile_has_action_name( action ) ) {
        if( action == "QUIT" ) {
            return _( "Back" );
        }
        if( action == "CONFIRM" ) {
            return _( "OK" );
        }
        if( action == "HELP_KEYBINDINGS" ) {
            return _( "Keys" );
        }
    }
    return ctxt.get_action_name( action );
}

static std::vector<bar_button> collect_buttons( const input_context &ctxt,
        const std::vector<key_hint> &hints )
{
    std::vector<bar_button> buttons;
    const auto add = [&buttons, &ctxt]( const std::string & action, const std::string & label ) {
        const auto same = [&action]( const bar_button & other ) {
            return other.action == action;
        };
        if( label.empty() || std::any_of( buttons.begin(), buttons.end(), same ) ) {
            return;
        }
        const std::optional<input_event> event = event_for( ctxt, action );
        if( !event ) {
            return;
        }
        bar_button button;
        button.action = action;
        button.label = label;
        button.event = *event;
        buttons.push_back( button );
    };

    for( const key_hint &hint : hints ) {
        if( is_registered( ctxt, hint.action ) ) {
            add( hint.action, hint.label );
        }
    }
    const size_t hint_count = buttons.size();

    const bool on_map = ctxt.mobile_category() == "DEFAULTMODE";
    const bool own = std::find( own_contexts.begin(), own_contexts.end(),
                                &ctxt ) != own_contexts.end();
    if( !on_map && !own ) {
        const std::vector<std::string> &actions = ctxt.mobile_registered_actions();
        // the two answers first, the keybindings screen last
        for( const char *const first : {
                 "CONFIRM", "QUIT"
             } ) {
            if( is_registered( ctxt, first ) ) {
                add( first, action_label( ctxt, first ) );
            }
        }
        for( const std::string &action : actions ) {
            if( !skip_action( action ) && action != "HELP_KEYBINDINGS" ) {
                add( action, action_label( ctxt, action ) );
            }
        }
        if( buttons.size() > hint_count && is_registered( ctxt, "HELP_KEYBINDINGS" ) ) {
            add( "HELP_KEYBINDINGS", action_label( ctxt, "HELP_KEYBINDINGS" ) );
        }
        // a screen that only knows "back" has the system back button for that
        if( hint_count == 0 && buttons.size() == 1 && buttons.front().action == "QUIT" ) {
            buttons.clear();
        }
#if defined(__ANDROID__)
        // keys a screen reads by hand, with the names it gave them
        for( const input_context::manual_key &manual : const_cast<input_context &>
             ( ctxt ).get_registered_manual_keys() ) {
            bar_button button;
            button.action = "manual " + std::to_string( manual.key );
            button.event = input_event( manual.key, input_event_t::keyboard_char );
            button.label = manual.text.empty() ? button.event.long_description() : manual.text;
            buttons.push_back( button );
        }
#endif
    }

    // the answer that goes on is the orange one
    const auto confirm = std::find_if( buttons.begin(), buttons.end(), []( const bar_button & b ) {
        return b.action == "CONFIRM";
    } );
    if( confirm != buttons.end() ) {
        confirm->primary = true;
    } else if( hint_count > 0 ) {
        const auto forward = std::find_if( buttons.begin(), buttons.begin() + hint_count,
        []( const bar_button & b ) {
            return b.action != "QUIT";
        } );
        ( forward != buttons.begin() + hint_count ? *forward : buttons.front() ).primary = true;
    }
    // Upstream's screens may want a key that has no button: the keyboard is one tap away.
    if( !on_map && !own ) {
        bar_button keyboard;
        keyboard.kind = button_kind::keyboard;
        keyboard.action = "mobile keyboard";
        buttons.insert( buttons.begin(), keyboard );
    }
    return buttons;
}

static constexpr ImU32 col_button_fill = IM_COL32( 28, 33, 26, 245 );
static constexpr float button_h = 48.0f;
static constexpr float gap = 8.0f;

static float button_width( const bar_button &button, const float u )
{
    if( button.kind == button_kind::keyboard ) {
        return 52.0f * u;
    }
    return std::max( 64.0f * u,
                     mobile_ui::text_size( face::bold, 15.0f, button.label ).x + 28.0f * u );
}

static void draw_button( ImDrawList *const draw, bar_button &button, const ImVec2 &pos,
                         const ImVec2 &size, const bool held, const float u )
{
    button.min = pos;
    button.max = ImVec2( pos.x + size.x, pos.y + size.y );
    const ImU32 fill = button.primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : col_button_fill );
    mobile_ui::box( draw, button.min, button.max, fill,
                    button.primary ? mobile_ui::col_accent : mobile_ui::col_line, 12.0f );
    if( button.kind == button_kind::keyboard ) {
        mobile_ui::icon( draw, "keyboard", ImVec2( pos.x + ( size.x - 24.0f * u ) * 0.5f,
                         pos.y + ( size.y - 24.0f * u ) * 0.5f ), 24.0f, mobile_ui::col_fg );
        return;
    }
    // long names are cut off at the edge of the button
    const ImVec2 label = mobile_ui::text_size( face::bold, 15.0f, button.label );
    const float inner_w = size.x - 16.0f * u;
    draw->PushClipRect( ImVec2( pos.x + 8.0f * u, pos.y ),
                        ImVec2( pos.x + size.x - 8.0f * u, pos.y + size.y ), true );
    mobile_ui::text( draw, face::bold, 15.0f,
                     ImVec2( pos.x + 8.0f * u + std::max( 0.0f, ( inner_w - label.x ) * 0.5f ),
                             pos.y + ( size.y - label.y ) * 0.5f ),
                     button.primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, button.label );
    draw->PopClipRect();
}

static bool inside( const ImVec2 &min, const ImVec2 &max, const float x, const float y )
{
    return max.x > min.x && x >= min.x && x < max.x && y >= min.y && y < max.y;
}

void draw()
{
    const std::vector<key_hint> hints = std::move( frame_hints );
    frame_hints.clear();
    const input_context *const was_context = shown_context;
    shown.clear();
    shown_context = nullptr;
    panel_min = panel_max = ImVec2( 0.0f, 0.0f );
    if( !enabled() || ImGui::GetCurrentContext() == nullptr ) {
        expanded = false;
        return;
    }
    const input_context *const ctxt = waiting_context();
    if( ctxt == nullptr ) {
        expanded = false;
        return;
    }
    shown = collect_buttons( *ctxt, hints );
    shown_context = ctxt;
    if( shown.empty() || ctxt != was_context ) {
        expanded = false;
        scroll = 0.0f;
    }
    if( shown.empty() ) {
        return;
    }

    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetForegroundDrawList();
    const float left = origin.x + safe.x + 12.0f * u;
    const float right = origin.x + size.x - safe.z - 12.0f * u;
    const float bottom = origin.y + size.y - safe.w - 10.0f * u;
    const float avail = right - left;
    const float h = button_h * u;
    const float g = gap * u;
    const bool pressing = finger.target == press_target::button && !finger.dragged;

    // how many fit in one row, with room for "More" if not all do
    bar_button more;
    more.kind = button_kind::more;
    more.label = expanded ? _( "Less" ) : _( "More" );
    const float more_w = button_width( more, u );
    float total = 0.0f;
    for( const bar_button &button : shown ) {
        total += button_width( button, u ) + g;
    }
    total -= g;
    size_t in_row = shown.size();
    if( total > avail ) {
        float used = more_w;
        in_row = 0;
        while( in_row < shown.size() &&
               used + g + button_width( shown[in_row], u ) <= avail ) {
            used += g + button_width( shown[in_row], u );
            ++in_row;
        }
        total = used;
    } else {
        expanded = false;
    }
    const bool overflow = in_row < shown.size();

    if( expanded ) {
        // every action, as a grid on a panel above the row
        const int columns = size.x > size.y ? 3 : 2;
        const size_t rest = shown.size() - in_row;
        const int rows = static_cast<int>( ( rest + static_cast<size_t>( columns ) - 1 ) /
                                           static_cast<size_t>( columns ) );
        const float pad = 12.0f * u;
        const float content_h = static_cast<float>( rows ) * ( h + g ) - g;
        const float panel_w = std::min( avail, 560.0f * u );
        const float max_h = bottom - h - g - ( origin.y + safe.y + 12.0f * u ) - 2.0f * pad;
        const float view_h = std::min( content_h, std::max( h, max_h ) );
        scroll_max = std::max( 0.0f, content_h - view_h );
        scroll = std::clamp( scroll, 0.0f, scroll_max );
        panel_max = ImVec2( right, bottom - h - g );
        panel_min = ImVec2( right - panel_w, panel_max.y - view_h - 2.0f * pad );
        draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                             mobile_ui::col_dim );
        mobile_ui::box( draw, panel_min, panel_max, mobile_ui::col_sheet, mobile_ui::col_line, 18.0f );
        const float cell_w = ( panel_w - 2.0f * pad - static_cast<float>( columns - 1 ) * g ) /
                             static_cast<float>( columns );
        const ImVec2 clip_min( panel_min.x + pad, panel_min.y + pad );
        const ImVec2 clip_max( panel_max.x - pad, panel_max.y - pad );
        draw->PushClipRect( clip_min, clip_max, true );
        for( size_t i = in_row; i < shown.size(); ++i ) {
            const size_t n = i - in_row;
            const ImVec2 pos( clip_min.x + static_cast<float>( n % static_cast<size_t>( columns ) ) *
                              ( cell_w + g ),
                              clip_min.y - scroll + static_cast<float>( n / static_cast<size_t>( columns ) ) *
                              ( h + g ) );
            draw_button( draw, shown[i], pos, ImVec2( cell_w, h ),
                         pressing && finger.button == i, u );
            // only the part inside the panel can be tapped
            shown[i].min.y = std::max( shown[i].min.y, clip_min.y );
            shown[i].max.y = std::min( shown[i].max.y, clip_max.y );
            if( shown[i].max.y <= shown[i].min.y ) {
                shown[i].min = shown[i].max = ImVec2( 0.0f, 0.0f );
            }
        }
        draw->PopClipRect();
    }

    // the row: centered while everything fits, otherwise filled from the left
    float x = overflow ? left : left + ( avail - total ) * 0.5f;
    for( size_t i = 0; i < in_row; ++i ) {
        const float w = button_width( shown[i], u );
        draw_button( draw, shown[i], ImVec2( x, bottom - h ), ImVec2( w, h ),
                     pressing && finger.button == i, u );
        x += w + g;
    }
    if( overflow ) {
        shown.push_back( more );
        draw_button( draw, shown.back(), ImVec2( right - more_w, bottom - h ), ImVec2( more_w, h ),
                     pressing && finger.button == shown.size() - 1, u );
    }
}

bool finger_down( const float x, const float y )
{
    finger = finger_state();
    if( shown.empty() || shown_context != waiting_context() ) {
        return false;
    }
    finger.down_x = x;
    finger.down_y = y;
    finger.last_y = y;
    for( size_t i = 0; i < shown.size(); ++i ) {
        if( inside( shown[i].min, shown[i].max, x, y ) ) {
            finger.target = press_target::button;
            finger.button = i;
            return true;
        }
    }
    if( expanded ) {
        finger.target = inside( panel_min, panel_max, x, y ) ? press_target::panel :
                        press_target::outside;
        return true;
    }
    return false;
}

void finger_motion( const float x, const float y )
{
    if( finger.target == press_target::none ) {
        return;
    }
    const float slop = 10.0f * mobile_ui::unit();
    if( !finger.dragged && std::hypot( x - finger.down_x, y - finger.down_y ) > slop ) {
        finger.dragged = true;
    }
    if( finger.dragged && expanded && finger.target != press_target::outside ) {
        scroll = std::clamp( scroll - ( y - finger.last_y ), 0.0f, scroll_max );
    }
    finger.last_y = y;
}

bool finger_up( const float x, const float y, input_event &out )
{
    const finger_state was = finger;
    finger = finger_state();
    if( was.target == press_target::outside ) {
        expanded = false;
        return false;
    }
    if( was.target != press_target::button || was.dragged || was.button >= shown.size() ||
        shown_context != waiting_context() ) {
        return false;
    }
    const bar_button &button = shown[was.button];
    if( !inside( button.min, button.max, x, y ) ) {
        return false;
    }
    if( button.kind == button_kind::more ) {
        expanded = !expanded;
        scroll = 0.0f;
        return false;
    }
    if( button.kind == button_kind::keyboard ) {
        toggle_soft_keyboard();
        return false;
    }
    expanded = false;
    out = button.event;
    return true;
}

void finger_cancel()
{
    finger = finger_state();
}

#endif // TILES

} // namespace mobile_prompt
