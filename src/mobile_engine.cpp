#include "mobile_engine.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>

#include "cata_imgui.h"
#include "cata_utility.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_richtext.h"
#include "mobile_ui.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#endif

namespace mobile_engine
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

constexpr ImU32 col_row_line = IM_COL32( 232, 236, 222, 20 );
constexpr ImU32 col_switch_off = IM_COL32( 255, 255, 255, 36 );
constexpr ImU32 col_disabled = IM_COL32( 154, 163, 146, 110 );
constexpr float row_units = 68.0f;

class controller_window : public cataimgui::window
{
    public:
        controller_window( bool &enabled, int &lo, int &hi, const std::string &manual ) :
            cataimgui::window( "MOBILE_SMART_CONTROLLER",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            enabled( enabled ), lo( lo ), hi( hi ) {
            manual_text.set( manual, mobile_ui::col_text_soft, false );
        }
        bool done = false;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        bool &enabled;
        int &lo;
        int &hi;
        mobile_richtext::block manual_text;
        bool rewind = true;

        // The name and the explanation of a row; text_w is the room they have.
        void label( ImDrawList *draw, const ImVec2 &pos, float text_w, const std::string &name,
                    const std::string &sub ) const;
        // A row with minus, the value and plus.  Returns -1, 0 or 1.
        int stepper( ImDrawList *draw, const std::string &id, const ImVec2 &pos, float width,
                     const std::string &name, const std::string &sub, int value, bool can_dec,
                     bool can_inc ) const;
};

void controller_window::label( ImDrawList *const draw, const ImVec2 &pos, const float text_w,
                               const std::string &name, const std::string &sub ) const
{
    const float u = mobile_ui::unit();
    const ImVec2 name_px = mobile_ui::text_size( face::bold, 14.5f, name, text_w );
    const ImVec2 sub_px = mobile_ui::text_size( face::body, 12.0f, sub, text_w );
    const float top = pos.y + ( row_units * u - name_px.y - 2.0f * u - sub_px.y ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( pos.x + 16.0f * u, top ), mobile_ui::col_fg,
                     name, text_w );
    mobile_ui::text( draw, face::body, 12.0f, ImVec2( pos.x + 16.0f * u, top + name_px.y + 2.0f * u ),
                     mobile_ui::col_muted, sub, text_w );
}

static bool step_button( ImDrawList *const draw, const std::string &id, const char *const icon_name,
                         const ImVec2 &pos, const float row_h, const bool on )
{
    const float u = mobile_ui::unit();
    // 36 units to look at, 44 wide and the row's height to touch
    const ImVec2 box_min( pos.x + 4.0f * u, pos.y + ( row_h - 36.0f * u ) * 0.5f );
    const ImVec2 box_max( box_min.x + 36.0f * u, box_min.y + 36.0f * u );
    bool held = false;
    const bool step = on && mobile_ui::pressable( id, pos, ImVec2( 44.0f * u, row_h ), &held );
    mobile_ui::box( draw, box_min, box_max,
                    held ? mobile_ui::col_button_down : on ? IM_COL32( 255, 255, 255, 15 ) :
                    IM_COL32( 255, 255, 255, 6 ), on ? mobile_ui::col_line : IM_COL32( 232, 236, 222, 14 ),
                    10.0f );
    mobile_ui::icon( draw, icon_name, ImVec2( box_min.x + 9.0f * u, box_min.y + 9.0f * u ), 18.0f,
                     on ? mobile_ui::col_fg : col_disabled );
    return step;
}

int controller_window::stepper( ImDrawList *const draw, const std::string &id, const ImVec2 &pos,
                                const float width, const std::string &name, const std::string &sub,
                                const int value, const bool can_dec, const bool can_inc ) const
{
    const float u = mobile_ui::unit();
    const float row_h = row_units * u;
    const float control_w = 140.0f * u;
    const float right = pos.x + width - 16.0f * u;
    const float control_x = right - control_w;
    label( draw, pos, control_x - pos.x - 28.0f * u, name, sub );
    int by = 0;
    if( step_button( draw, id + "_dec", "minus", ImVec2( control_x, pos.y ), row_h, can_dec ) ) {
        by = -1;
    }
    if( step_button( draw, id + "_inc", "plus", ImVec2( right - 44.0f * u, pos.y ), row_h, can_inc ) ) {
        by = 1;
    }
    const std::string shown = string_format( "%d%%", value );
    const ImVec2 value_px = mobile_ui::text_size( face::mono, 14.0f, shown );
    mobile_ui::text( draw, face::mono, 14.0f,
                     ImVec2( control_x + ( control_w - value_px.x ) * 0.5f, pos.y + ( row_h - value_px.y ) * 0.5f ),
                     mobile_ui::col_fg, shown );
    draw->AddLine( ImVec2( pos.x, pos.y + row_h ), ImVec2( pos.x + width, pos.y + row_h ),
                   col_row_line, std::max( 1.0f, u ) );
    return by;
}

void controller_window::draw_controls()
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

    const float avail = size.x - safe.x - safe.z;
    const float width = std::min( avail, 620.0f * u );
    const float left = origin.x + safe.x + ( avail - width ) * 0.5f;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "Smart engine controller" ), ImVec2( left, origin.y + safe.y ), width,
                            &bar_h ) ) {
        done = true;
    }
    const float y = origin.y + safe.y + bar_h;

    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##smart_controller", ImVec2( width, origin.y + size.y - safe.w - y ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    if( rewind ) {
        // the list keeps its place from one opening to the next otherwise
        ImGui::SetScrollY( 0.0f );
        rewind = false;
    }
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float row_h = row_units * u;
    ImVec2 pos = base;

    // the switch
    {
        bool held = false;
        if( mobile_ui::pressable( "##smart_enabled", pos, ImVec2( width, row_h ), &held ) ) {
            enabled = !enabled;
        }
        const float sw_x = pos.x + width - 16.0f * u - 50.0f * u;
        label( draw, pos, sw_x - pos.x - 28.0f * u, _( "Enabled" ),
               _( "The controller turns the engines on and off as needed." ) );
        const ImVec2 sw( sw_x, pos.y + ( row_h - 30.0f * u ) * 0.5f );
        draw->AddRectFilled( sw, ImVec2( sw.x + 50.0f * u, sw.y + 30.0f * u ),
                             enabled ? mobile_ui::col_accent : col_switch_off, 15.0f * u );
        draw->AddCircleFilled( ImVec2( sw.x + ( enabled ? 35.0f : 15.0f ) * u, sw.y + 15.0f * u ),
                               12.0f * u, IM_COL32_WHITE, 32 );
        draw->AddLine( ImVec2( pos.x, pos.y + row_h ), ImVec2( pos.x + width, pos.y + row_h ),
                       col_row_line, std::max( 1.0f, u ) );
        pos.y += row_h;
    }

    mobile_ui::text( draw, face::mono, 10.5f, ImVec2( pos.x + 16.0f * u, pos.y + 16.0f * u ),
                     mobile_ui::col_muted, to_upper_case( _( "Electric motor use (% battery)" ) ) );
    pos.y += 36.0f * u;

    // the same sums as upstream's sliders
    if( const int by = stepper( draw, "##smart_lo", pos, width, _( "Low mark" ),
                                _( "Below this charge the electric motor is not used." ), lo, lo > 5, lo < 90 ) ) {
        lo = clamp( ( lo / 5 + by ) * 5, 5, 90 );
        hi = std::max( lo + 5, hi );
    }
    pos.y += row_h;
    if( const int by = stepper( draw, "##smart_hi", pos, width, _( "High mark" ),
                                _( "Above this charge the electric motor is preferred." ), hi, hi > 10, hi < 95 ) ) {
        hi = clamp( ( hi / 5 + by ) * 5, 10, 95 );
        lo = std::min( hi - 5, lo );
    }
    pos.y += row_h;

    if( !manual_text.empty() ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( pos.x + 16.0f * u, pos.y + 16.0f * u ),
                         mobile_ui::col_muted, to_upper_case( _( "User manual" ) ) );
        pos.y += 36.0f * u;
        const float text_h = manual_text.height( face::body, 13.5f, width - 32.0f * u );
        manual_text.draw( draw, ImVec2( pos.x + 16.0f * u, pos.y ) );
        pos.y += text_h;
    }

    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, pos.y - base.y + 24.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace

void smart_controller( bool &enabled, int &battery_lo, int &battery_hi,
                       const std::string &manual )
{
    controller_window window( enabled, battery_lo, battery_hi, manual );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // twice: the first frame still shows what was under the page
    ui_manager::redraw();
    while( !window.done ) {
        ui_manager::redraw();
        if( ctxt.handle_input() == "QUIT" ) {
            break;
        }
    }
}

#else // !TILES

void smart_controller( bool &, int &, int &, const std::string & ) {}

#endif // TILES

} // namespace mobile_engine
