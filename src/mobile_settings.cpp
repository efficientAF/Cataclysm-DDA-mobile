#include "mobile_settings.h"

#if defined(TILES)
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "cata_imgui.h"
#if defined(__ANDROID__)
#include "cursesport.h"
#endif
#include "game.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_hud_edit.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "options.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

namespace mobile_settings
{

using mobile_ui::face;

namespace
{

constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_switch_off = IM_COL32( 255, 255, 255, 41 );
constexpr ImU32 col_seg = IM_COL32( 255, 255, 255, 15 );
constexpr ImU32 col_lead = IM_COL32( 191, 198, 181, 255 );

bool has( const std::string &name )
{
    return get_options().has_option( name );
}

class settings_window : public cataimgui::window
{
    public:
        settings_window() : cataimgui::window( "MOBILE_SETTINGS",
                                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoBackground ) {}

        // back was tapped
        bool done = false;
        // "All options" was tapped
        bool open_all = false;
        // "Edit HUD" was tapped
        bool edit_hud = false;
        // "Tileset" was tapped
        bool pick_tileset = false;
        // an option was given a new value
        bool changed = false;
        // where the list was scrolled to, kept while the full options are open
        float scroll = 0.0f;
        bool restore_scroll = false;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        // the scrolling list being drawn
        ImDrawList *draw = nullptr;
        float left = 0.0f;
        float width = 0.0f;
        float y = 0.0f;

        // Draws the title and explanation of a row that keeps control_w free
        // on the right; returns the height of the row.
        float row_text( const std::string &title, const std::string &sub, float control_w );
        void row_end( float row_h );
        void section( const std::string &label );
        void lead( const std::string &text );
        void switch_row( const std::string &option, const std::string &title, const std::string &sub );
        // a switch for a number: off is 0, on is on_value
        void switch_row_int( const std::string &option, int on_value, const std::string &title,
                             const std::string &sub );
        void draw_switch( const ImVec2 &row_pos, float row_h, bool on );
        // choices are pairs of option value and label
        void segment_row( const std::string &option, const std::string &title, const std::string &sub,
                          const std::vector<std::pair<std::string, std::string>> &choices );
        // lo and hi are the ends of the option's range
        void slider_row( const std::string &option, int lo, int hi, int step, const std::string &title,
                         const std::string &sub );
        // a row with a button on the right
        bool button_row( const std::string &id, const std::string &title, const std::string &sub,
                         const std::string &label, bool enabled );
        // a row that opens another screen
        bool link_row( const std::string &id, const std::string &title, const std::string &sub );
};

float settings_window::row_text( const std::string &title, const std::string &sub,
                                 const float control_w )
{
    const float u = mobile_ui::unit();
    const float text_w = std::max( 80.0f * u, width - 32.0f * u - 12.0f * u - control_w );
    const ImVec2 title_px = mobile_ui::text_size( face::bold, 14.5f, title, text_w );
    const ImVec2 sub_px = sub.empty() ? ImVec2( 0.0f, 0.0f ) :
                          mobile_ui::text_size( face::body, 12.0f, sub, text_w );
    const float text_h = title_px.y + ( sub.empty() ? 0.0f : 2.0f * u + sub_px.y );
    const float row_h = std::max( 60.0f * u, text_h + 24.0f * u );
    const float top = y + ( row_h - text_h ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, top ), mobile_ui::col_fg,
                     title, text_w );
    if( !sub.empty() ) {
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( left + 16.0f * u, top + title_px.y + 2.0f * u ),
                         mobile_ui::col_muted, sub, text_w );
    }
    return row_h;
}

void settings_window::row_end( const float row_h )
{
    y += row_h;
    draw->AddLine( ImVec2( left, y ), ImVec2( left + width, y ), col_row_line,
                   std::max( 1.0f, mobile_ui::unit() ) );
}

void settings_window::section( const std::string &label )
{
    const float u = mobile_ui::unit();
    mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left + 16.0f * u, y + 16.0f * u ),
                     mobile_ui::col_muted, label );
    y += 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, label ).y + 6.0f * u;
}

void settings_window::lead( const std::string &text )
{
    const float u = mobile_ui::unit();
    const float text_w = width - 32.0f * u;
    mobile_ui::text( draw, face::body, 13.0f, ImVec2( left + 16.0f * u, y + 12.0f * u ), col_lead,
                     text, text_w );
    y += 12.0f * u + mobile_ui::text_size( face::body, 13.0f, text, text_w ).y + 4.0f * u;
}

void settings_window::draw_switch( const ImVec2 &row_pos, const float row_h, const bool on )
{
    const float u = mobile_ui::unit();
    const ImVec2 pos( row_pos.x + width - 16.0f * u - 50.0f * u,
                      row_pos.y + ( row_h - 30.0f * u ) * 0.5f );
    draw->AddRectFilled( pos, ImVec2( pos.x + 50.0f * u, pos.y + 30.0f * u ),
                         on ? mobile_ui::col_accent : col_switch_off, 15.0f * u );
    draw->AddCircleFilled( ImVec2( pos.x + ( on ? 35.0f : 15.0f ) * u, pos.y + 15.0f * u ), 12.0f * u,
                           IM_COL32_WHITE, 32 );
}

void settings_window::switch_row( const std::string &option, const std::string &title,
                                  const std::string &sub )
{
    if( !has( option ) ) {
        return;
    }
    const ImVec2 pos( left, y );
    const float row_h = row_text( title, sub, 50.0f * mobile_ui::unit() );
    const bool on = get_option<bool>( option );
    draw_switch( pos, row_h, on );
    // the whole row is the target
    if( mobile_ui::pressable( "##" + option, pos, ImVec2( width, row_h ) ) ) {
        get_options().get_option( option ).setValue( on ? "false" : "true" );
        changed = true;
    }
    row_end( row_h );
}

void settings_window::switch_row_int( const std::string &option, const int on_value,
                                      const std::string &title, const std::string &sub )
{
    if( !has( option ) ) {
        return;
    }
    const ImVec2 pos( left, y );
    const float row_h = row_text( title, sub, 50.0f * mobile_ui::unit() );
    const bool on = get_option<int>( option ) != 0;
    draw_switch( pos, row_h, on );
    if( mobile_ui::pressable( "##" + option, pos, ImVec2( width, row_h ) ) ) {
        get_options().get_option( option ).setValue( on ? 0 : on_value );
        changed = true;
    }
    row_end( row_h );
}

void settings_window::segment_row( const std::string &option, const std::string &title,
                                   const std::string &sub,
                                   const std::vector<std::pair<std::string, std::string>> &choices )
{
    if( !has( option ) ) {
        return;
    }
    const float u = mobile_ui::unit();
    std::vector<float> widths;
    float seg_w = 4.0f * u;
    for( const std::pair<std::string, std::string> &choice : choices ) {
        widths.push_back( mobile_ui::text_size( face::bold, 12.5f, choice.second ).x + 20.0f * u );
        seg_w += widths.back();
    }
    const ImVec2 pos( left, y );
    const float row_h = row_text( title, sub, seg_w );
    const std::string current = get_options().get_option( option ).getValue( true );
    const float label_h = mobile_ui::text_size( face::bold, 12.5f, "X" ).y;
    const float seg_h = label_h + 18.0f * u + 4.0f * u;
    const ImVec2 seg_pos( left + width - 16.0f * u - seg_w, y + ( row_h - seg_h ) * 0.5f );
    mobile_ui::box( draw, seg_pos, ImVec2( seg_pos.x + seg_w, seg_pos.y + seg_h ), col_seg, 0, 10.0f );
    float x = seg_pos.x + 2.0f * u;
    for( size_t i = 0; i < choices.size(); ++i ) {
        const bool on = choices[i].first == current;
        if( on ) {
            mobile_ui::box( draw, ImVec2( x, seg_pos.y + 2.0f * u ),
                            ImVec2( x + widths[i], seg_pos.y + seg_h - 2.0f * u ), mobile_ui::col_accent, 0, 8.0f );
        }
        mobile_ui::text( draw, face::bold, 12.5f, ImVec2( x + 10.0f * u, seg_pos.y + 11.0f * u ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_muted, choices[i].second );
        // the target is as tall as the row
        if( mobile_ui::pressable( "##" + option + std::to_string( i ), ImVec2( x, pos.y ),
                                  ImVec2( widths[i], row_h ) ) && !on ) {
            get_options().get_option( option ).setValue( choices[i].first );
            changed = true;
        }
        x += widths[i];
    }
    row_end( row_h );
}

void settings_window::slider_row( const std::string &option, const int lo, const int hi,
                                  const int step, const std::string &title, const std::string &sub )
{
    if( !has( option ) ) {
        return;
    }
    const float u = mobile_ui::unit();
    options_manager::cOpt &opt = get_options().get_option( option );
    const int value = get_option<int>( option );
    const float control_w = ( width - 32.0f * u ) * 0.44f;
    const ImVec2 pos( left, y );
    const float row_h = row_text( title, sub, control_w );
    const float mid = y + row_h * 0.5f;
    const float right = left + width - 16.0f * u;

    // the track; touching or dragging anywhere on it moves the thumb there
    const float knob = 10.0f * u;
    const float track_x0 = right - control_w + knob;
    const float track_x1 = right - 52.0f * u - 8.0f * u - knob;
    bool held = false;
    mobile_ui::pressable( "##" + option, ImVec2( track_x0 - knob, pos.y ),
                          ImVec2( track_x1 - track_x0 + 2.0f * knob, row_h ), &held );
    int shown = value;
    if( held && hi > lo && track_x1 > track_x0 ) {
        const float wanted = std::clamp( ( ImGui::GetIO().MousePos.x - track_x0 ) /
                                         ( track_x1 - track_x0 ), 0.0f, 1.0f );
        const int steps = static_cast<int>( std::lround( wanted * static_cast<float>( hi - lo ) /
                                            static_cast<float>( step ) ) );
        shown = std::clamp( lo + steps * step, lo, hi );
        if( shown != value ) {
            opt.setValue( shown );
            changed = true;
        }
    }
    const float fraction = hi > lo ? static_cast<float>( shown - lo ) / static_cast<float>
                           ( hi - lo ) : 0.0f;
    const float thumb_x = track_x0 + ( track_x1 - track_x0 ) * fraction;
    draw->AddRectFilled( ImVec2( track_x0, mid - 2.0f * u ), ImVec2( track_x1, mid + 2.0f * u ),
                         col_switch_off, 2.0f * u );
    draw->AddRectFilled( ImVec2( track_x0, mid - 2.0f * u ), ImVec2( thumb_x, mid + 2.0f * u ),
                         mobile_ui::col_accent, 2.0f * u );
    draw->AddCircleFilled( ImVec2( thumb_x, mid ), held ? knob * 1.15f : knob, mobile_ui::col_accent,
                           32 );

    // the value, right aligned in a column of its own
    //~ a length of time in milliseconds
    const std::string label = string_format( _( "%d ms" ), shown );
    const ImVec2 label_px = mobile_ui::text_size( face::mono, 11.5f, label );
    mobile_ui::text( draw, face::mono, 11.5f, ImVec2( right - label_px.x, mid - label_px.y * 0.5f ),
                     mobile_ui::col_muted, label );
    row_end( row_h );
}

bool settings_window::button_row( const std::string &id, const std::string &title,
                                  const std::string &sub, const std::string &label, const bool enabled )
{
    const float u = mobile_ui::unit();
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 13.5f, label );
    const ImVec2 size( label_px.x + 28.0f * u, 40.0f * u );
    const float row_h = row_text( title, sub, size.x );
    const ImVec2 pos( left + width - 16.0f * u - size.x, y + ( row_h - size.y ) * 0.5f );
    bool held = false;
    // the target reaches the full height of the row
    const bool tapped = enabled &&
                        mobile_ui::pressable( id, ImVec2( pos.x, y ), ImVec2( size.x, row_h ), &held );
    // a disabled button is drawn at 40%, like the canvas
    const ImU32 fill = held ? mobile_ui::col_button_down :
                       enabled ? IM_COL32( 255, 255, 255, 15 ) : IM_COL32( 255, 255, 255, 6 );
    const ImU32 border = enabled ? mobile_ui::col_line : IM_COL32( 232, 236, 222, 14 );
    const ImU32 ink = enabled ? mobile_ui::col_fg : IM_COL32( 233, 236, 223, 102 );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), fill, border, 12.0f );
    mobile_ui::text( draw, face::bold, 13.5f,
                     ImVec2( pos.x + 14.0f * u, pos.y + ( size.y - label_px.y ) * 0.5f ), ink, label );
    row_end( row_h );
    return tapped;
}

bool settings_window::link_row( const std::string &id, const std::string &title,
                                const std::string &sub )
{
    const float u = mobile_ui::unit();
    const ImVec2 pos( left, y );
    const float text_w = std::max( 80.0f * u, width - 64.0f * u );
    const float title_h = mobile_ui::text_size( face::bold, 14.5f, title, text_w ).y;
    const float sub_h = sub.empty() ? 0.0f : 2.0f * u +
                        mobile_ui::text_size( face::body, 12.0f, sub, text_w ).y;
    const float row_h = std::max( 60.0f * u, title_h + sub_h + 24.0f * u );
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, ImVec2( width, row_h ), &held );
    if( held ) {
        draw->AddRectFilled( pos, ImVec2( pos.x + width, pos.y + row_h ), mobile_ui::col_button_down );
    }
    row_text( title, sub, 20.0f * u );
    mobile_ui::icon( draw, "chev", ImVec2( left + width - 16.0f * u - 20.0f * u,
                                           y + ( row_h - 20.0f * u ) * 0.5f ), 20.0f, mobile_ui::col_muted );
    row_end( row_h );
    return tapped;
}

void settings_window::draw_controls()
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
    back->PopClipRect();

    const float bar_left = origin.x + safe.x;
    const float bar_width = size.x - safe.x - safe.z;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "Settings" ), ImVec2( bar_left, origin.y + safe.y ), bar_width,
                            &bar_h ) ) {
        done = true;
    }

    ImGui::SetCursorScreenPos( ImVec2( bar_left, origin.y + safe.y + bar_h ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##settings_scroll", ImVec2( bar_width, size.y - safe.y - safe.w - bar_h ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    if( restore_scroll ) {
        ImGui::SetScrollY( scroll );
        restore_scroll = false;
    } else {
        scroll = ImGui::GetScrollY();
    }
    draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    left = base.x;
    width = bar_width;
    y = base.y;

    section( _( "TOUCH & GESTURES" ) );
    segment_row( "TOUCH_TAP_TO_ACT", _( "Tap on the map" ),
    _( "What a single tap next to you does" ), {
        { "true", _( "Smart action" ) }, { "false", _( "Tap key" ) }
    } );
    switch_row( "TOUCH_LONG_PRESS_MENU", _( "Long-press opens the action menu" ),
                _( "Hold a finger still on a map tile to choose what to do there" ) );
    slider_row( "TOUCH_LONG_PRESS_DELAY", 300, 1500, 10, _( "Long-press time" ),
                _( "How long to hold before the action menu opens" ) );
    switch_row( "TOUCH_ZOOM_NO_WRAP", _( "Zoom stops at the ends" ),
                _( "Off jumps from the closest zoom level to the furthest" ) );
    switch_row( "TOUCH_TWO_FINGER_GESTURES", _( "Pinch and two-finger drag" ),
                _( "Pinch to zoom and drag with two fingers to pan, as well as the Zoom and Pan controls" ) );
    switch_row( "TOUCH_RECENTER_ON_MOVE", _( "Recenter when you move" ),
                _( "Taking a step brings the view back to you" ) );
    segment_row( "TOUCH_JOYSTICK_MODE", _( "Movement joystick" ),
    _( "Fixed is a pad on the HUD with a button to show and hide it; Floating starts where you drag on the map" ), {
        { "fixed", _( "Fixed" ) }, { "floating", _( "Floating" ) }
    } );
    switch_row( "ANDROID_SHOW_VIRTUAL_JOYSTICK", _( "Show the floating joystick" ),
                _( "Draws the Floating joystick under your finger while you drag" ) );
    switch_row_int( "ANDROID_VIBRATION", 10, _( "Vibration" ), _( "Short buzz on taps" ) );

    section( _( "DISPLAY" ) );
    segment_row( "ANDROID_TEXT_SIZE", _( "Text size" ), "", {
        { "auto", _( "Auto" ) }, { "8x16", _( "S" ) }, { "10x20", _( "M" ) }, { "12x24", _( "L" ) },
        { "14x28", _( "XL" ) }
    } );
    segment_row( "USE_TILES", _( "Map graphics" ),
    _( "Tiles draws the map with pictures; ASCII draws it with letters and cannot zoom" ), {
        { "true", _( "Tiles" ) }, { "false", _( "ASCII" ) }
    } );
    if( get_option<bool>( "USE_TILES" ) && has( "TILES" ) &&
        link_row( "##tileset", _( "Tileset" ),
                  get_options().get_option( "TILES" ).getValueName() ) ) {
        pick_tileset = true;
    }
    switch_row( "MOBILE_HUD", _( "Full-screen map" ),
                _( "Hides the sidebar and draws status and messages over the map" ) );
    const bool can_edit = mobile_hud_edit::available();
    if( button_row( "##edit_hud", _( "HUD layout" ),
                    can_edit ? _( "Move, resize and hide the pieces on the play screen" ) :
                    _( "Open Settings from a game to change the layout" ), _( "Edit HUD" ), can_edit ) ) {
        edit_hud = true;
    }

    section( _( "MENUS" ) );
    switch_row( "MOBILE_MENUS", _( "Touch screens" ),
                _( "Off brings back the classic text screens; turn it on again under All options" ) );
    switch_row( "TOUCH_MENU_TAP", _( "Tap items in text menus" ),
                _( "A tap in a classic menu picks the item under your finger at once" ) );

    if( has( "ANDROID_AUTO_KEYBOARD" ) ) {
        section( _( "KEYBOARD" ) );
        switch_row( "ANDROID_AUTO_KEYBOARD", _( "Show keyboard automatically" ),
                    _( "Only when a screen needs typing, like search or naming" ) );
    }

    section( _( "GAMEPLAY" ) );
    switch_row( "ANDROID_QUICKSAVE", _( "Save when the app goes to the background" ), "" );
    switch_row( "AUTOSAVE", _( "Autosave" ), _( "Saves the game every few minutes while you play" ) );
    switch_row( "AUTOSAFEMODE", _( "Safe mode turns itself back on" ),
                _( "Safe mode stops you and warns when a monster comes into view" ) );

    lead( _( "Every other option of the game is under All options." ) );
    if( link_row( "##all_options", _( "All options" ), _( "Graphics, sound, interface, debug…" ) ) ) {
        open_all = true;
    }
    y += 16.0f * u + safe.w;

    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// Lets the player choose one of the installed tilesets.  Returns true if
// another one was chosen.
bool choose_tileset()
{
    options_manager::cOpt &opt = get_options().get_option( "TILES" );
    const std::vector<options_manager::id_and_option> tilesets = opt.getItems();
    mobile_ui::sheet_spec spec;
    spec.title = _( "Tileset" );
    spec.description = _( "How the map is drawn.  Names ending in iso are drawn at an angle." );
    const std::string current = opt.getValue( true );
    for( const options_manager::id_and_option &tileset : tilesets ) {
        spec.rows.push_back( { tileset.first == current ? "check" : "", tileset.second.translated(), "" } );
    }
    const int tapped = mobile_ui::show_sheet( spec );
    if( tapped < 0 || static_cast<size_t>( tapped ) >= tilesets.size() ||
        tilesets[static_cast<size_t>( tapped )].first == current ) {
        return false;
    }
    opt.setValue( tilesets[static_cast<size_t>( tapped )].first );
    return true;
}

// Saves what was changed, the way upstream's options screen does when it is left.
void commit( bool changed, const std::string &tileset_before )
{
    if( !changed ) {
        return;
    }
    const bool was_tiles = use_tiles;
    // also refreshes the cached options, use_tiles among them
    get_options().save();
    if( g ) {
        g->on_options_changed();
        const bool new_tileset = get_option<std::string>( "TILES" ) != tileset_before;
        if( ( use_tiles != was_tiles || ( use_tiles && new_tileset ) ) &&
            !get_avatar().name.empty() ) {
            // Tiles and letters differ in size, so the map window is laid out
            // again.  A game started with letters has only checked the
            // tileset, not loaded it; without a game it is loaded with the world.
            if( use_tiles ) {
                g->reload_tileset();
            } else {
                g->reset_zoom();
                g->mark_main_ui_adaptor_resize();
            }
        }
    }
#if defined(__ANDROID__)
    // the text size decides the terminal grid
    request_terminal_relayout();
#endif
}

} // namespace

void show()
{
    float scroll = 0.0f;
    while( true ) {
        bool open_all = false;
        bool edit_hud = false;
        bool pick_tileset = false;
        bool changed = false;
        const std::string tileset_before = get_option<std::string>( "TILES" );
        {
            settings_window window;
            window.scroll = scroll;
            window.restore_scroll = scroll > 0.0f;
            input_context ctxt( "default" );
            const mobile_prompt::own_controls own_controls( ctxt );
            ctxt.register_action( "QUIT" );
            ctxt.register_action( "ANY_INPUT" );
            while( !window.done && !window.open_all && !window.edit_hud && !window.pick_tileset ) {
                ui_manager::redraw();
                if( ctxt.handle_input() == "QUIT" ) {
                    break;
                }
            }
            open_all = window.open_all;
            edit_hud = window.edit_hud;
            pick_tileset = window.pick_tileset;
            changed = window.changed;
            scroll = window.scroll;
        }
        if( pick_tileset && choose_tileset() ) {
            changed = true;
        }
        commit( changed, tileset_before );
        if( pick_tileset ) {
            continue;
        }
        if( edit_hud ) {
            // straight into the layout editor, over the map
            mobile_hud_edit::run();
            return;
        }
        if( !open_all ) {
            return;
        }
        // upstream's full options screen; it does its own saving
        get_options().show( true );
    }
}

} // namespace mobile_settings

#else // !TILES

namespace mobile_settings
{

void show() {}

} // namespace mobile_settings

#endif // TILES
