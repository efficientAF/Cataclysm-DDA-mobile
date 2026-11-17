#include "mobile_hud_edit.h"

#if defined(TILES)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_imgui.h"
#include "game.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_hud.h"
#include "mobile_hud_layout.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "string_formatter.h"
#include "touch_input.h"
#include "translations.h"
#include "ui_manager.h"

namespace mobile_hud_edit
{

using mobile_hud::drawer_id;
using mobile_hud::num_drawers;
using mobile_hud::num_pieces;
using mobile_hud::piece_id;
using mobile_ui::face;

namespace
{
struct drag_state {
    bool active = false;
    float start_x = 0.0f;
    float start_y = 0.0f;
    // the piece's place when the finger went down
    mobile_hud::rect start_rect;
    bool moved = false;
};

struct editor_state {
    bool running = false;
    mobile_hud::preset working;
    // a piece_id, num_pieces plus a drawer_id, or -1
    int selection = -1;
    drag_state drag;
    bool changed = false;
    // a sheet of the editor is open: fingers go to it
    bool in_sheet = false;
    // the editor's own controls as of the last frame, in ImGui coordinates
    std::vector<std::pair<ImVec2, ImVec2>> chrome;
};
} // namespace

static editor_state editor;

static mobile_hud::layout &working_layout( const bool landscape )
{
    return landscape ? editor.working.landscape : editor.working.portrait;
}

static bool is_drawer( const int selection )
{
    return selection >= static_cast<int>( num_pieces );
}

bool available()
{
    return g != nullptr && touch_input::hud_enabled() && !get_avatar().name.empty();
}

bool chrome_at( const float x, const float y )
{
    if( editor.in_sheet ) {
        return true;
    }
    for( const std::pair<ImVec2, ImVec2> &area : editor.chrome ) {
        if( x >= area.first.x && x < area.second.x && y >= area.first.y && y < area.second.y ) {
            return true;
        }
    }
    return false;
}

static bool inside( const mobile_hud::rect &r, const float x, const float y, const float grow )
{
    return r.w > 0.0f && x >= r.x - grow && x < r.x + r.w + grow && y >= r.y - grow &&
           y < r.y + r.h + grow;
}

void finger_down( const float x, const float y )
{
    editor.drag = drag_state();
    if( !editor.running ) {
        return;
    }
    const mobile_hud::edit_geometry geo = mobile_hud::edit_frame();
    const float grow = 6.0f * geo.unit;
    int hit = -1;
    // handles first: they are small and sit on top at the edges
    for( size_t i = 0; i < num_drawers && hit < 0; ++i ) {
        if( inside( geo.handles[i], x, y, grow ) ) {
            hit = static_cast<int>( num_pieces + i );
            editor.drag.start_rect = geo.handles[i];
        }
    }
    // then the pieces, the smallest one under the finger
    if( hit < 0 ) {
        float best_area = 0.0f;
        for( size_t i = 0; i < num_pieces; ++i ) {
            const float area = geo.pieces[i].w * geo.pieces[i].h;
            if( inside( geo.pieces[i], x, y, grow ) && ( hit < 0 || area < best_area ) ) {
                hit = static_cast<int>( i );
                best_area = area;
                editor.drag.start_rect = geo.pieces[i];
            }
        }
    }
    editor.selection = hit;
    if( hit >= 0 ) {
        editor.drag.active = true;
        editor.drag.start_x = x;
        editor.drag.start_y = y;
    }
    mobile_hud::update_edit( editor.working, editor.selection );
}

void finger_motion( const float x, const float y )
{
    if( !editor.running || !editor.drag.active || editor.selection < 0 ) {
        return;
    }
    const mobile_hud::edit_geometry geo = mobile_hud::edit_frame();
    const float dx = x - editor.drag.start_x;
    const float dy = y - editor.drag.start_y;
    if( !editor.drag.moved && std::hypot( dx, dy ) < 8.0f * geo.unit ) {
        return;
    }
    editor.drag.moved = true;
    editor.changed = true;
    mobile_hud::layout &layout = working_layout( geo.landscape );
    if( is_drawer( editor.selection ) ) {
        mobile_hud::drawer &d = layout.drawers[static_cast<size_t>( editor.selection ) - num_pieces];
        mobile_hud::move_handle( d, x, y, geo.screen, geo.handle_safe, geo.unit );
    } else {
        mobile_hud::rect where = editor.drag.start_rect;
        const float left = geo.screen.x + geo.safe.left;
        const float top = geo.screen.y + geo.safe.top;
        where.x = std::clamp( where.x + dx, left, std::max( left,
                              geo.screen.x + geo.screen.w - geo.safe.right - where.w ) );
        where.y = std::clamp( where.y + dy, top, std::max( top,
                              geo.screen.y + geo.screen.h - geo.safe.bottom - where.h ) );
        mobile_hud::pin_to( layout.pieces[static_cast<size_t>( editor.selection )], where, geo.screen,
                            geo.safe, geo.unit, geo.landscape );
    }
    mobile_hud::update_edit( editor.working, editor.selection );
}

void finger_up( const float, const float )
{
    editor.drag = drag_state();
}

void finger_cancel()
{
    editor.drag = drag_state();
}

namespace
{
enum class bar_action : int {
    none,
    cancel,
    presets,
    pieces,
    done,
};

class editor_window : public cataimgui::window
{
    public:
        editor_window() : cataimgui::window( "MOBILE_HUD_EDIT",
                                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) {}
        bar_action tapped = bar_action::none;
        // "Order" of the quick actions was tapped
        bool wants_order = false;
    protected:
        cataimgui::bounds get_bounds() override {
            // One pixel short of the screen, so that ui_manager keeps redrawing
            // the map underneath (see ui_adaptor::invalidate_ui).
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            return { 0.0f, 0.0f, size.x, std::max( size.y - 1.0f, 2.0f ) };
        }
        void draw_controls() override;
    private:
        void draw_bar( const ImVec2 &origin, const ImVec2 &size );
        void draw_options( const ImVec2 &origin, const ImVec2 &size );
        // one row of the options panel; each returns true if the value changed
        bool stepper( const std::string &label, float &value, float low, float high, float step,
                      const std::string &shown );
        bool stepper( const std::string &label, int &value, int low, int high );
        bool choice( const std::string &label, int &value, const std::vector<std::string> &names );
        // a row with one button at the right edge; true when it is tapped
        bool button_row( const std::string &label, const std::string &button );

        ImDrawList *draw = nullptr;
        float row_x = 0.0f;
        float row_y = 0.0f;
        float row_w = 0.0f;
        int row_index = 0;
};

constexpr float row_h = 46.0f;

bool editor_window::stepper( const std::string &label, float &value, const float low,
                             const float high, const float step, const std::string &shown )
{
    const float u = mobile_ui::unit();
    const float h = row_h * u;
    const float b = 38.0f * u;
    const std::string id = "##hudopt" + std::to_string( row_index++ );
    mobile_ui::text( draw, face::body, 14.0f, ImVec2( row_x, row_y + ( h - 17.0f * u ) * 0.5f ),
                     mobile_ui::col_text_soft, label );
    bool changed = false;
    const float right = row_x + row_w;
    const std::array<float, 2> steps = { -step, step };
    const std::array<const char *, 2> icons = { "minus", "plus" };
    const std::array<float, 2> xs = { right - 2.0f * b - 56.0f * u, right - b };
    for( size_t i = 0; i < 2; ++i ) {
        const ImVec2 pos( xs[i], row_y + ( h - b ) * 0.5f );
        const bool can = i == 0 ? value > low + 0.001f : value < high - 0.001f;
        bool held = false;
        if( mobile_ui::pressable( id + icons[i], pos, ImVec2( b, b ), &held ) && can ) {
            value = std::clamp( value + steps[i], low, high );
            changed = true;
        }
        mobile_ui::box( draw, pos, ImVec2( pos.x + b, pos.y + b ),
                        held && can ? mobile_ui::col_button_down : mobile_ui::col_fill_soft,
                        mobile_ui::col_line, 11.0f );
        mobile_ui::icon( draw, icons[i], ImVec2( pos.x + ( b - 18.0f * u ) * 0.5f,
                         pos.y + ( b - 18.0f * u ) * 0.5f ), 18.0f,
                         can ? mobile_ui::col_fg : mobile_ui::col_line );
    }
    const ImVec2 value_px = mobile_ui::text_size( face::mono, 13.0f, shown );
    mobile_ui::text( draw, face::mono, 13.0f,
                     ImVec2( right - b - 28.0f * u - value_px.x * 0.5f, row_y + ( h - value_px.y ) * 0.5f ),
                     mobile_ui::col_fg, shown );
    row_y += h;
    return changed;
}

bool editor_window::stepper( const std::string &label, int &value, const int low, const int high )
{
    float as_float = static_cast<float>( value );
    const bool changed = stepper( label, as_float, static_cast<float>( low ),
                                  static_cast<float>( high ), 1.0f, std::to_string( value ) );
    value = static_cast<int>( std::lround( as_float ) );
    return changed;
}

bool editor_window::choice( const std::string &label, int &value,
                            const std::vector<std::string> &names )
{
    const float u = mobile_ui::unit();
    const float h = row_h * u;
    const std::string id = "##hudopt" + std::to_string( row_index++ );
    mobile_ui::text( draw, face::body, 14.0f, ImVec2( row_x, row_y + ( h - 17.0f * u ) * 0.5f ),
                     mobile_ui::col_text_soft, label );
    bool changed = false;
    // a segmented control at the right edge
    float total = 4.0f * u;
    std::vector<float> widths;
    for( const std::string &name : names ) {
        widths.push_back( mobile_ui::text_size( face::bold, 12.5f, name ).x + 20.0f * u );
        total += widths.back();
    }
    const float seg_h = 36.0f * u;
    const ImVec2 seg_min( row_x + row_w - total, row_y + ( h - seg_h ) * 0.5f );
    mobile_ui::box( draw, seg_min, ImVec2( seg_min.x + total, seg_min.y + seg_h ),
                    IM_COL32( 255, 255, 255, 15 ), 0, 10.0f );
    float x = seg_min.x + 2.0f * u;
    for( size_t i = 0; i < names.size(); ++i ) {
        const ImVec2 pos( x, seg_min.y + 2.0f * u );
        const ImVec2 size( widths[i], seg_h - 4.0f * u );
        const bool on = value == static_cast<int>( i );
        if( mobile_ui::pressable( id + std::to_string( i ), pos, size ) && !on ) {
            value = static_cast<int>( i );
            changed = true;
        }
        if( on ) {
            mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), mobile_ui::col_accent, 0,
                            8.0f );
        }
        const ImVec2 name_px = mobile_ui::text_size( face::bold, 12.5f, names[i] );
        mobile_ui::text( draw, face::bold, 12.5f, ImVec2( pos.x + ( size.x - name_px.x ) * 0.5f,
                         pos.y + ( size.y - name_px.y ) * 0.5f ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_muted, names[i] );
        x += widths[i];
    }
    row_y += h;
    return changed;
}

bool editor_window::button_row( const std::string &label, const std::string &button )
{
    const float u = mobile_ui::unit();
    const float h = row_h * u;
    const std::string id = "##hudopt" + std::to_string( row_index++ );
    mobile_ui::text( draw, face::body, 14.0f, ImVec2( row_x, row_y + ( h - 17.0f * u ) * 0.5f ),
                     mobile_ui::col_text_soft, label );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 13.0f, button );
    const ImVec2 size( label_px.x + 28.0f * u, 38.0f * u );
    const ImVec2 pos( row_x + row_w - size.x, row_y + ( h - size.y ) * 0.5f );
    bool held = false;
    const bool tapped_now = mobile_ui::pressable( id, pos, size, &held );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ),
                    held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 11.0f );
    mobile_ui::text( draw, face::bold, 13.0f, ImVec2( pos.x + 14.0f * u,
                     pos.y + ( size.y - label_px.y ) * 0.5f ), mobile_ui::col_fg, button );
    row_y += h;
    return tapped_now;
}

// Cancel, Presets, Pieces and Done, with a line of help; in the middle of the
// screen, where no piece of the HUD lives.
void editor_window::draw_bar( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const std::array<std::pair<std::string, bar_action>, 4> entries = { {
            { _( "Cancel" ), bar_action::cancel }, { _( "Presets" ), bar_action::presets },
            { _( "Pieces" ), bar_action::pieces }, { _( "Done" ), bar_action::done }
        }
    };
    const float avail = size.x - safe.x - safe.z - 24.0f * u;
    const float gap = 8.0f * u;
    const float pad = 10.0f * u;
    // narrow enough to leave the drawer handles at the sides free
    const float w = std::min( avail - 48.0f * u, 372.0f * u );
    const float button_w = ( w - 2.0f * pad - 3.0f * gap ) / 4.0f;
    const float button_h = 44.0f * u;
    const std::string help = _( "Drag a piece to move it. Tap it for options." );
    const ImVec2 help_px = mobile_ui::text_size( face::body, 12.5f, help, w - 2.0f * pad );
    const float h = pad + help_px.y + 8.0f * u + button_h + pad;
    const ImVec2 min( origin.x + safe.x + ( size.x - safe.x - safe.z - w ) * 0.5f,
                      origin.y + size.y * 0.42f - h * 0.5f );
    const ImVec2 max( min.x + w, min.y + h );
    mobile_ui::box( draw, min, max, mobile_ui::col_sheet, mobile_ui::col_line, 16.0f );
    editor.chrome.emplace_back( min, max );
    mobile_ui::text( draw, face::body, 12.5f, ImVec2( min.x + ( w - help_px.x ) * 0.5f, min.y + pad ),
                     mobile_ui::col_muted, help, w - 2.0f * pad );
    float x = min.x + pad;
    const float y = min.y + pad + help_px.y + 8.0f * u;
    for( const std::pair<std::string, bar_action> &entry : entries ) {
        const bool primary = entry.second == bar_action::done;
        bool held = false;
        if( mobile_ui::pressable( "##hudbar" + entry.first, ImVec2( x, y ), ImVec2( button_w, button_h ),
                                  &held ) ) {
            tapped = entry.second;
        }
        mobile_ui::box( draw, ImVec2( x, y ), ImVec2( x + button_w, y + button_h ),
                        primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                        ( held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 ) ),
                        primary ? mobile_ui::col_accent : mobile_ui::col_line, 12.0f );
        const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.0f, entry.first );
        mobile_ui::text( draw, face::bold, 14.0f, ImVec2( x + ( button_w - label_px.x ) * 0.5f,
                         y + ( button_h - label_px.y ) * 0.5f ),
                         primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, entry.first );
        x += button_w + gap;
    }
}

// The options of the selected piece, on a panel that keeps away from it.
void editor_window::draw_options( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    mobile_hud::layout &layout = working_layout( landscape );
    const size_t index = static_cast<size_t>( editor.selection );
    const bool drawer = is_drawer( editor.selection );
    const piece_id id = drawer ? piece_id::num : static_cast<piece_id>( index );

    int rows = 1;
    if( drawer ) {
        rows += 3;
    } else {
        switch( id ) {
            case piece_id::status:
            case piece_id::log:
            case piece_id::location:
                rows += 3;
                break;
            case piece_id::zoom:
                rows += layout.pieces[index].slider ? 4 : 3;
                break;
            case piece_id::quick:
                rows += 3;
                break;
            case piece_id::body:
            case piece_id::pan:
            case piece_id::joy:
                rows += 2;
                break;
            case piece_id::wield:
            case piece_id::recenter:
            case piece_id::num:
                rows += 1;
                break;
        }
    }
    const float pad = 12.0f * u;
    const float w = std::min( size.x - safe.x - safe.z - 24.0f * u, 372.0f * u );
    const float h = static_cast<float>( rows ) * row_h * u + 2.0f * pad;
    // in the middle of the screen, like the bar: the pieces live at the edges
    // and are drawn over everything else
    const float top = origin.y + safe.y + 10.0f * u;
    const float bottom = origin.y + size.y - safe.w - 10.0f * u;
    const ImVec2 min( origin.x + safe.x + ( size.x - safe.x - safe.z - w ) * 0.5f,
                      std::max( top, origin.y + size.y * 0.42f - h * 0.5f ) );
    const ImVec2 max( min.x + w, std::min( bottom, min.y + h ) );
    mobile_ui::box( draw, min, max, mobile_ui::col_sheet, mobile_ui::col_line, 16.0f );
    editor.chrome.emplace_back( min, max );
    draw->PushClipRect( min, max, true );
    row_x = min.x + pad;
    row_y = min.y + pad;
    row_w = w - 2.0f * pad;
    row_index = 0;

    // title, with Hide/Show and a close button
    const std::string name = drawer ? mobile_hud::display_name( static_cast<drawer_id>
                             ( index - num_pieces ) ) : mobile_hud::display_name( id );
    mobile_ui::text( draw, face::display, 19.0f, ImVec2( row_x, row_y + 10.0f * u ),
                     mobile_ui::col_fg, name );
    bool &visible = drawer ? layout.drawers[index - num_pieces].visible : layout.pieces[index].visible;
    const float close_w = 40.0f * u;
    const std::string toggle = visible ? _( "Hide" ) : _( "Show" );
    const float toggle_w = mobile_ui::text_size( face::bold, 13.0f, toggle ).x + 24.0f * u;
    const ImVec2 toggle_pos( row_x + row_w - close_w - 6.0f * u - toggle_w, row_y + 3.0f * u );
    bool held = false;
    bool changed = false;
    if( mobile_ui::pressable( "##hudopt_visible", toggle_pos, ImVec2( toggle_w, 38.0f * u ),
                              &held ) ) {
        visible = !visible;
        changed = true;
    }
    mobile_ui::box( draw, toggle_pos, ImVec2( toggle_pos.x + toggle_w, toggle_pos.y + 38.0f * u ),
                    held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 ), mobile_ui::col_line,
                    11.0f );
    mobile_ui::text( draw, face::bold, 13.0f, ImVec2( toggle_pos.x + 12.0f * u,
                     toggle_pos.y + 11.0f * u ), mobile_ui::col_fg, toggle );
    const ImVec2 close_pos( row_x + row_w - close_w, row_y + 3.0f * u );
    if( mobile_ui::pressable( "##hudopt_close", close_pos, ImVec2( close_w, 38.0f * u ) ) ) {
        editor.selection = -1;
    }
    mobile_ui::icon( draw, "close", ImVec2( close_pos.x + 10.0f * u, close_pos.y + 9.0f * u ), 20.0f,
                     mobile_ui::col_muted );
    row_y += row_h * u;

    const std::vector<std::string> row_or_column = { _( "Row" ), _( "Column" ) };
    const auto percent = []( const float v ) {
        return string_format( "%d%%", static_cast<int>( std::lround( v * 100.0f ) ) );
    };
    const auto units = []( const float v ) {
        return std::to_string( static_cast<int>( std::lround( v ) ) );
    };
    if( drawer ) {
        mobile_hud::drawer &d = layout.drawers[index - num_pieces];
        int edge = static_cast<int>( d.edge );
        if( choice( _( "Edge" ), edge, { _( "Left" ), _( "Right" ), _( "Top" ), _( "Bottom" ) } ) ) {
            d.edge = static_cast<mobile_hud::screen_edge>( edge );
            changed = true;
        }
        changed |= stepper( _( "Handle at" ), d.off, 0.1f, 0.9f, 0.05f, percent( d.off ) );
        changed |= stepper( _( "Depth" ), d.size, 0.3f, 0.9f, 0.05f, percent( d.size ) );
    } else {
        mobile_hud::piece &p = layout.pieces[index];
        int column = p.column ? 1 : 0;
        switch( id ) {
            case piece_id::status:
                if( choice( _( "Direction" ), column, row_or_column ) ) {
                    p.column = column == 1;
                    changed = true;
                }
                changed |= stepper( _( "Icon size" ), p.size, 24.0f, 52.0f, 2.0f, units( p.size ) );
                changed |= stepper( _( "Max shown" ), p.count, 2, 12 );
                break;
            case piece_id::log:
                changed |= stepper( _( "Rows" ), p.rows, 1, 10 );
                changed |= stepper( _( "Width" ), p.width, 160.0f, 800.0f, 10.0f, units( p.width ) );
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            case piece_id::location:
                changed |= stepper( _( "Rows" ), p.rows, 1, 3 );
                changed |= stepper( _( "Width" ), p.width, 140.0f, 340.0f, 10.0f, units( p.width ) );
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            case piece_id::body:
                changed |= stepper( _( "Size" ), p.size, 44.0f, 120.0f, 4.0f, units( p.size ) );
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            case piece_id::quick:
                if( choice( _( "Direction" ), column, row_or_column ) ) {
                    p.column = column == 1;
                    changed = true;
                }
                changed |= stepper( _( "Buttons" ), p.count, mobile_hud::quick_count_min,
                                    mobile_hud::quick_count_max );
                if( button_row( _( "Which buttons, in what order" ), _( "Order" ) ) ) {
                    wants_order = true;
                }
                break;
            case piece_id::zoom: {
                int style = p.slider ? 1 : 0;
                if( choice( _( "Style" ), style, { _( "Dial" ), _( "Slider" ) } ) ) {
                    p.slider = style == 1;
                    if( p.slider ) {
                        // upright and of a middling length to start with
                        p.column = true;
                        p.width = 180.0f;
                    }
                    changed = true;
                }
                if( p.slider ) {
                    int upright = p.column ? 0 : 1;
                    if( choice( _( "Direction" ), upright, { _( "Vertical" ), _( "Horizontal" ) } ) ) {
                        p.column = upright == 0;
                        changed = true;
                    }
                    changed |= stepper( _( "Length" ), p.width, mobile_hud::slider_length_min,
                                        mobile_hud::slider_length_max, 10.0f, units( p.width ) );
                } else {
                    changed |= stepper( _( "Size" ), p.size, mobile_hud::dial_size_min,
                                        mobile_hud::dial_size_max, 4.0f, units( p.size ) );
                }
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            }
            case piece_id::pan:
                changed |= stepper( _( "Size" ), p.size, mobile_hud::dial_size_min,
                                    mobile_hud::dial_size_max, 4.0f, units( p.size ) );
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            case piece_id::joy: {
                // Fixed or Floating is a setting; this is how the fixed pad shows
                int always = p.mode == mobile_hud::pad_mode::always ? 1 : 0;
                if( choice( _( "Shows" ), always, { _( "With its button" ), _( "Always" ) } ) ) {
                    p.mode = always == 1 ? mobile_hud::pad_mode::always : mobile_hud::pad_mode::toggle;
                    changed = true;
                }
                changed |= stepper( _( "Size" ), p.size, 90.0f, 200.0f, 10.0f, units( p.size ) );
                break;
            }
            case piece_id::wield:
            case piece_id::recenter:
                changed |= stepper( _( "Opacity" ), p.opacity, 0.4f, 1.0f, 0.1f, percent( p.opacity ) );
                break;
            case piece_id::num:
                break;
        }
    }
    draw->PopClipRect();
    if( changed ) {
        editor.changed = true;
    }
}

void editor_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    editor.chrome.clear();
    if( editor.selection >= 0 ) {
        draw_options( viewport->Pos, viewport->Size );
    } else {
        draw_bar( viewport->Pos, viewport->Size );
    }
    draw->PopClipRect();
    mobile_hud::update_edit( editor.working, editor.selection );
}

// The buttons of the quick actions bar as a list in the order they appear;
// a row is dragged by its grip to another place.
class order_window : public cataimgui::window
{
    public:
        order_window( std::vector<std::string> &order, int &count ) :
            cataimgui::window( "MOBILE_HUD_ORDER",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            order( order ), count( count ) {}
        bool back = false;
        bool changed = false;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        std::vector<std::string> &order;
        int &count;
        // the row that is being dragged, or -1
        int dragging = -1;
};

void order_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const back_draw = ImGui::GetWindowDrawList();
    back_draw->PushClipRectFullScreen();
    back_draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                              mobile_ui::col_screen );
    back_draw->PopClipRect();

    const float avail = size.x - safe.x - safe.z;
    const float width = std::min( avail, 520.0f * u );
    const float left = origin.x + safe.x + ( avail - width ) * 0.5f;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "Quick actions" ), ImVec2( origin.x + safe.x, origin.y + safe.y ), avail,
                            &bar_h ) ) {
        back = true;
    }
    float y = origin.y + safe.y + bar_h + 8.0f * u;
    const int on_bar = std::clamp( count, mobile_hud::quick_count_min,
                                   mobile_hud::quick_count_max ) - 1;
    const std::string help = string_format(
                                 _( "Drag a row by its grip to move it.  The first %d are on the bar, then Menu.  Change how many with Buttons." ),
                                 on_bar );
    mobile_ui::text( back_draw, face::body, 12.5f, ImVec2( left + 14.0f * u, y ), mobile_ui::col_muted,
                     help, width - 28.0f * u );
    y += mobile_ui::text_size( face::body, 12.5f, help, width - 28.0f * u ).y + 8.0f * u;

    const float row = 52.0f * u;
    const float grip_w = 64.0f * u;
    const float list_h = origin.y + size.y - safe.w - y;
    // fingers on the grips drag; anywhere else they scroll the list
    touch_input::set_raw_drag_rect( left + width - grip_w, y, grip_w, list_h,
                                    touch_input::view_mode::overmap );
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##hud_order_rows", ImVec2( width, list_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const list = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const ImVec2 mouse = ImGui::GetMousePos();
        const int rows = static_cast<int>( order.size() );
        const int under = std::clamp( static_cast<int>( std::floor( ( mouse.y - base.y ) / row ) ), 0,
                                      std::max( rows - 1, 0 ) );
        const bool in_list = mouse.y >= y && mouse.y < y + list_h;
        if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) {
            dragging = -1;
        } else if( dragging < 0 && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && in_list &&
                   mouse.x >= left + width - grip_w && mouse.x < left + width ) {
            dragging = under;
        } else if( dragging >= 0 && dragging < rows && under != dragging ) {
            const std::string moved = order[static_cast<size_t>( dragging )];
            order.erase( order.begin() + dragging );
            order.insert( order.begin() + under, moved );
            dragging = under;
            changed = true;
        }
        if( dragging >= 0 ) {
            // near the ends the list moves along
            if( mouse.y < y + 36.0f * u ) {
                ImGui::SetScrollY( std::max( 0.0f, ImGui::GetScrollY() - 10.0f * u ) );
            } else if( mouse.y > y + list_h - 36.0f * u ) {
                ImGui::SetScrollY( std::min( ImGui::GetScrollMaxY(), ImGui::GetScrollY() + 10.0f * u ) );
            }
        }
        for( int i = 0; i < rows; ++i ) {
            const std::string &id = order[static_cast<size_t>( i )];
            const ImVec2 min( base.x, base.y + static_cast<float>( i ) * row );
            const ImVec2 max( base.x + width, min.y + row );
            const bool shown = i < on_bar;
            if( i == dragging ) {
                list->AddRectFilled( min, max, mobile_ui::col_accent_soft );
            }
            const ImU32 ink = shown ? mobile_ui::col_fg : mobile_ui::col_muted;
            mobile_ui::icon( list, mobile_hud::quick_item_icon( id ), ImVec2( min.x + 14.0f * u,
                             min.y + ( row - 22.0f * u ) * 0.5f ), 22.0f, ink );
            const std::string label = mobile_hud::quick_item_label( id );
            mobile_ui::text( list, face::bold, 14.5f, ImVec2( min.x + 50.0f * u,
                             min.y + ( row - mobile_ui::text_size( face::bold, 14.5f, label ).y ) * 0.5f ), ink, label );
            const std::string state = shown ? string_format( "%d",
                                      i + 1 ) : std::string( _( "Not on the bar" ) );
            const ImVec2 state_px = mobile_ui::text_size( face::mono, 11.0f, state );
            mobile_ui::text( list, face::mono, 11.0f, ImVec2( max.x - grip_w - state_px.x,
                             min.y + ( row - state_px.y ) * 0.5f ),
                             shown ? mobile_ui::col_accent : mobile_ui::col_muted, state );
            mobile_ui::icon( list, "gripH", ImVec2( max.x - grip_w + ( grip_w - 22.0f * u ) * 0.5f,
                                                    min.y + ( row - 22.0f * u ) * 0.5f ), 22.0f,
                             i == dragging ? mobile_ui::col_accent : mobile_ui::col_muted );
            list->AddLine( ImVec2( min.x, max.y ), max, IM_COL32( 255, 255, 255, 13 ), std::max( 1.0f, u ) );
            if( i + 1 == on_bar ) {
                // where the bar ends
                list->AddLine( ImVec2( min.x, max.y ), max, mobile_ui::col_accent, std::max( 1.0f, 2.0f * u ) );
            }
        }
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, static_cast<float>( rows ) * row + 8.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// Lets the player sort the buttons of the quick actions bar.
void order_quick_actions( const bool landscape )
{
    mobile_hud::piece &p = working_layout( landscape )[piece_id::quick];
    std::vector<std::string> order = mobile_hud::quick_order( p );
    // the HUD is drawn over everything, so it steps aside for the list
    editor.in_sheet = true;
    mobile_hud::end_edit();
    bool changed = false;
    {
        order_window window( order, p.count );
        input_context ctxt( "default" );
        const mobile_prompt::own_controls own_controls( ctxt );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "ANY_INPUT" );
        while( !window.back ) {
            ui_manager::redraw();
            if( ctxt.handle_input() == "QUIT" ) {
                break;
            }
        }
        changed = window.changed;
    }
    touch_input::clear_raw_drag_rect();
    mobile_hud::begin_edit( editor.working );
    editor.in_sheet = false;
    if( changed ) {
        p.items = order;
        editor.changed = true;
    }
}

// Lets the player start over from one of the built-in presets.
void pick_preset()
{
    mobile_ui::sheet_spec spec;
    spec.title = _( "Presets" );
    spec.description = _( "Replaces the layout you are editing, in both orientations." );
    const std::vector<mobile_hud::preset> &presets = mobile_hud::builtin_presets();
    for( const mobile_hud::preset &p : presets ) {
        spec.rows.push_back( { "", _( p.name ), "" } );
    }
    // the HUD is drawn over everything, so it steps aside for the sheet
    editor.in_sheet = true;
    mobile_hud::end_edit();
    const int tapped = mobile_ui::show_sheet( spec );
    mobile_hud::begin_edit( editor.working );
    editor.in_sheet = false;
    if( tapped >= 0 && static_cast<size_t>( tapped ) < presets.size() ) {
        editor.working = presets[static_cast<size_t>( tapped )];
        editor.selection = -1;
        editor.changed = true;
    }
}

// Shows and hides pieces, also the ones that are not on screen to be tapped.
void pick_pieces( const bool landscape )
{
    while( true ) {
        mobile_hud::layout &layout = working_layout( landscape );
        mobile_ui::sheet_spec spec;
        spec.title = _( "Pieces" );
        spec.description = _( "Tap a piece to show or hide it." );
        for( size_t i = 0; i < num_pieces; ++i ) {
            spec.rows.push_back( { layout.pieces[i].visible ? "check" : "", mobile_hud::display_name( static_cast<piece_id>( i ) ), layout.pieces[i].visible ? _( "Shown" ) : _( "Hidden" ) } );
        }
        for( size_t i = 0; i < num_drawers; ++i ) {
            spec.rows.push_back( { layout.drawers[i].visible ? "check" : "", mobile_hud::display_name( static_cast<drawer_id>( i ) ), layout.drawers[i].visible ? _( "Shown" ) : _( "Hidden" ) } );
        }
        editor.in_sheet = true;
        mobile_hud::end_edit();
        const int tapped = mobile_ui::show_sheet( spec );
        mobile_hud::begin_edit( editor.working );
        editor.in_sheet = false;
        if( tapped < 0 ) {
            return;
        }
        const size_t index = static_cast<size_t>( tapped );
        bool &visible = index < num_pieces ? layout.pieces[index].visible :
                        layout.drawers[index - num_pieces].visible;
        visible = !visible;
        editor.changed = true;
        mobile_hud::update_edit( editor.working, editor.selection );
    }
}
} // namespace

void run()
{
    if( !available() || editor.running ) {
        return;
    }
    editor = editor_state();
    editor.running = true;
    editor.working = mobile_hud::active_preset();
    mobile_hud::begin_edit( editor.working );
    {
        editor_window window;
        input_context ctxt( "default" );
        const mobile_prompt::own_controls own_controls( ctxt );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "ANY_INPUT" );
        bool done = false;
        while( !done ) {
            // the map again every time, or what was drawn before shows through
            g->invalidate_main_ui_adaptor();
            ui_manager::redraw();
            const std::string action = ctxt.handle_input();
            const bar_action tapped = action == "QUIT" ? bar_action::cancel : window.tapped;
            window.tapped = bar_action::none;
            if( window.wants_order ) {
                window.wants_order = false;
                order_quick_actions( mobile_hud::edit_frame().landscape );
            }
            switch( tapped ) {
                case bar_action::cancel:
                    if( editor.selection >= 0 && action == "QUIT" ) {
                        // back closes the options first
                        editor.selection = -1;
                    } else {
                        editor.changed = false;
                        done = true;
                    }
                    break;
                case bar_action::done:
                    done = true;
                    break;
                case bar_action::presets:
                    pick_preset();
                    break;
                case bar_action::pieces: {
                    const mobile_hud::edit_geometry geo = mobile_hud::edit_frame();
                    pick_pieces( geo.landscape );
                    break;
                }
                case bar_action::none:
                    break;
            }
            mobile_hud::update_edit( editor.working, editor.selection );
        }
    }
    mobile_hud::end_edit();
    if( editor.changed ) {
        mobile_hud::set_active_preset( editor.working );
    }
    editor.running = false;
    editor.chrome.clear();
}

} // namespace mobile_hud_edit

#else // !TILES

namespace mobile_hud_edit
{

bool available()
{
    return false;
}

void run() {}

} // namespace mobile_hud_edit

#endif // TILES
