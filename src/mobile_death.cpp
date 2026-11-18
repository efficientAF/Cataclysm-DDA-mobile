#include "mobile_death.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <sstream>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_variant.h"
#include "creature.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "game.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "kill_tracker.h"
#include "messages.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "stats_tracker.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "worldfactory.h"

static const string_id<event_statistic> event_statistic_num_avatar_zombie_kills(
    "num_avatar_zombie_kills" );
static const string_id<event_statistic> event_statistic_num_moves( "num_moves" );

namespace mobile_death
{

using mobile_ui::face;

namespace
{

constexpr ImU32 col_skull = IM_COL32( 224, 73, 43, 255 );
constexpr ImU32 col_died = IM_COL32( 255, 106, 85, 255 );
constexpr ImU32 col_cause = IM_COL32( 201, 207, 191, 255 );
constexpr ImU32 col_bar = IM_COL32( 18, 22, 16, 255 );
constexpr ImU32 col_input = IM_COL32( 9, 11, 8, 255 );
constexpr size_t max_last_words = 80;

struct death_note {
    bool taken = false;
    bool suicide = false;
    std::string killer;
    std::string place;
    std::string when;
};

// what note_death() saw
death_note noted;
// the world "New character in this world" was chosen for
std::optional<std::string> new_character_world;

enum class choice : int {
    none,
    new_character,
    main_menu,
};

struct death_summary {
    // before and after the name of the killer, which is drawn bold
    std::string cause_before;
    std::string cause_killer;
    std::string cause_after;
    std::vector<std::string> cause_more;
    std::vector<std::pair<std::string, std::string>> stats;
    std::vector<std::string> messages;
    bool can_restart = false;
};

int statistic( const string_id<event_statistic> &id )
{
    if( !id.is_valid() ) {
        return 0;
    }
    const cata_variant value = g->stats().value_of( id );
    return value.type() == cata_variant_type::int_ ? value.get<int>() : 0;
}

death_summary summarize()
{
    death_summary ret;
    const avatar &u = get_avatar();
    const std::string name = u.get_name();
    if( noted.suicide ) {
        ret.cause_before = string_format( _( "%s committed suicide" ), name );
    } else if( noted.killer.empty() ) {
        ret.cause_before = string_format( _( "%s was killed" ), name );
    } else {
        // the name of the killer is drawn in bold, so find where it goes
        const std::string marker = "\x01";
        //~ %1$s is the name of your character, %2$s is what killed them, like "the zombie brute"
        const std::string line = string_format( _( "%1$s was killed by %2$s" ), name, marker );
        const size_t at = line.find( marker );
        ret.cause_before = line.substr( 0, at );
        ret.cause_killer = noted.killer;
        ret.cause_after = at == std::string::npos ? "" : line.substr( at + marker.size() );
    }
    if( !noted.place.empty() ) {
        //~ %1$s is a place, like "a field north of Riverside", %2$s is a date and time
        ret.cause_more.push_back( string_format( _( "in %1$s, %2$s." ), noted.place, noted.when ) );
    }
    const time_duration survived = calendar::turn - calendar::start_of_game;
    //~ %s is a length of time, like "14 days and 3 hours"
    ret.cause_more.push_back( string_format( _( "Survived for %s." ), to_string( survived ) ) );

    const int moves = statistic( event_statistic_num_moves );
    ret.stats = {
        { _( "DAYS" ), std::to_string( to_days<int>( survived ) ) },
        { _( "KILLS" ), std::to_string( g->get_kill_tracker().total_kill_count() ) },
        { _( "ZOMBIES" ), std::to_string( statistic( event_statistic_num_avatar_zombie_kills ) ) },
        //~ distances walked, one map square counted as a meter
        { _( "WALKED" ), moves < 1000 ? string_format( _( "%d m" ), moves ) : string_format( _( "%.1f km" ), moves / 1000.0 ) },
    };
    for( const std::pair<std::string, std::string> &message : Messages::recent_messages( 5 ) ) {
        ret.messages.push_back( remove_color_tags( message.second ) );
    }
    // a world that is deleted when its last character dies cannot be played again
    ret.can_restart = world_generator->active_world != nullptr &&
                      get_option<std::string>( "WORLD_END" ) != "delete";
    return ret;
}

// Breaks text into lines no wider than max_w.
std::vector<std::string> wrap( const face f, const float size, const std::string &text,
                               const float max_w )
{
    std::vector<std::string> lines;
    std::istringstream words( text );
    std::string word;
    std::string line;
    while( words >> word ) {
        const std::string longer = line.empty() ? word : line + " " + word;
        if( !line.empty() && mobile_ui::text_size( f, size, longer ).x > max_w ) {
            lines.push_back( line );
            line = word;
        } else {
            line = longer;
        }
    }
    if( !line.empty() ) {
        lines.push_back( line );
    }
    return lines;
}

class death_window : public cataimgui::window
{
    public:
        explicit death_window( const death_summary &summary ) : cataimgui::window( "MOBILE_DEATH",
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            summary( summary ) {}

        choice result = choice::none;
        std::string last_words;
        // the last words field has the keyboard
        bool typing = false;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        death_summary summary;
        bool button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                     const std::string &label, bool primary );
};

bool death_window::button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                           const std::string &label, const bool primary )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, size, &held );
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 ) );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), fill,
                    primary ? 0 : mobile_ui::col_line, 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, label );
    return tapped;
}

void death_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const back = ImGui::GetWindowDrawList();
    back->PushClipRectFullScreen();
    back->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    back->PopClipRect();

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;

    // the buttons, in a bar at the bottom
    const float button_h = 48.0f * u;
    const float bar_h = 10.0f * u + button_h + ( summary.can_restart ? 8.0f * u + button_h : 0.0f ) +
                        10.0f * u + safe.w;
    const float bar_y = origin.y + size.y - bar_h;
    back->AddRectFilled( ImVec2( origin.x, bar_y ), ImVec2( origin.x + size.x, origin.y + size.y ),
                         col_bar );
    back->AddLine( ImVec2( origin.x, bar_y ), ImVec2( origin.x + size.x, bar_y ), mobile_ui::col_line,
                   std::max( 1.0f, u ) );
    {
        const ImVec2 button_size( width - 24.0f * u, button_h );
        float by = bar_y + 10.0f * u;
        if( summary.can_restart ) {
            if( button( "##new_character", ImVec2( left + 12.0f * u, by ), button_size,
                        _( "New character in this world" ), true ) ) {
                result = choice::new_character;
            }
            by += button_h + 8.0f * u;
        }
        if( button( "##main_menu", ImVec2( left + 12.0f * u, by ), button_size,
                    _( "Back to main menu" ), !summary.can_restart ) ) {
            result = choice::main_menu;
        }
    }

    // everything above the bar scrolls
    ImGui::SetCursorScreenPos( ImVec2( left, origin.y + safe.y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##death_scroll", ImVec2( width, bar_y - origin.y - safe.y ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float pad = 20.0f * u;
    const float gap = 12.0f * u;
    const float inner_x = base.x + pad;
    const float inner_w = width - 2.0f * pad;
    const float center = base.x + width * 0.5f;
    float y = base.y + ( landscape ? 14.0f : 34.0f ) * u;

    mobile_ui::icon( draw, "skull", ImVec2( center - 32.0f * u, y ), 64.0f, col_skull );
    y += 64.0f * u + gap;

    {
        const std::string title = _( "You died" );
        const ImVec2 title_px = mobile_ui::text_size( face::display, 60.0f, title );
        // the canvas sets this title tight, at nine tenths of its height
        mobile_ui::text( draw, face::display, 60.0f,
                         ImVec2( center - title_px.x * 0.5f, y - title_px.y * 0.08f ), col_died, title );
        y += title_px.y * 0.9f + gap;
    }

    // what happened: lines of 14 at one and a half line height, centered
    {
        const float line_h = 21.0f * u;
        const auto centered = [&]( const std::string & line ) {
            for( const std::string &part : wrap( face::body, 14.0f, line, inner_w ) ) {
                const ImVec2 part_px = mobile_ui::text_size( face::body, 14.0f, part );
                mobile_ui::text( draw, face::body, 14.0f,
                                 ImVec2( center - part_px.x * 0.5f, y + ( line_h - part_px.y ) * 0.5f ), col_cause, part );
                y += line_h;
            }
        };
        const float before_w = mobile_ui::text_size( face::body, 14.0f, summary.cause_before ).x;
        const float killer_w = mobile_ui::text_size( face::bold, 14.0f, summary.cause_killer ).x;
        const float after_w = mobile_ui::text_size( face::body, 14.0f, summary.cause_after ).x;
        if( summary.cause_killer.empty() || before_w + killer_w + after_w > inner_w ) {
            centered( summary.cause_before + summary.cause_killer + summary.cause_after );
        } else {
            float x = center - ( before_w + killer_w + after_w ) * 0.5f;
            const float text_y = y + ( line_h - mobile_ui::text_size( face::body, 14.0f, "X" ).y ) * 0.5f;
            mobile_ui::text( draw, face::body, 14.0f, ImVec2( x, text_y ), col_cause, summary.cause_before );
            x += before_w;
            mobile_ui::text( draw, face::bold, 14.0f, ImVec2( x, text_y ), mobile_ui::col_fg,
                             summary.cause_killer );
            x += killer_w;
            mobile_ui::text( draw, face::body, 14.0f, ImVec2( x, text_y ), col_cause, summary.cause_after );
            y += line_h;
        }
        for( const std::string &line : summary.cause_more ) {
            centered( line );
        }
        y += gap;
    }

    // a row of numbers
    {
        const float tile_gap = 6.0f * u;
        const float count = static_cast<float>( summary.stats.size() );
        const float tile_w = ( inner_w - ( count - 1.0f ) * tile_gap ) / count;
        const float label_h = mobile_ui::text_size( face::mono, 10.0f, "X" ).y;
        const float value_h = mobile_ui::text_size( face::mono, 17.0f, "X" ).y;
        const float tile_h = 6.0f * u + label_h + 2.0f * u + value_h + 6.0f * u;
        float x = inner_x;
        for( const std::pair<std::string, std::string> &stat : summary.stats ) {
            mobile_ui::box( draw, ImVec2( x, y ), ImVec2( x + tile_w, y + tile_h ),
                            IM_COL32( 255, 255, 255, 13 ), 0, 8.0f );
            const float label_w = mobile_ui::text_size( face::mono, 10.0f, stat.first ).x;
            mobile_ui::text( draw, face::mono, 10.0f, ImVec2( x + ( tile_w - label_w ) * 0.5f, y + 6.0f * u ),
                             mobile_ui::col_muted, stat.first );
            const float value_w = mobile_ui::text_size( face::mono, 17.0f, stat.second ).x;
            mobile_ui::text( draw, face::mono, 17.0f,
                             ImVec2( x + ( tile_w - value_w ) * 0.5f, y + 6.0f * u + label_h + 2.0f * u ),
                             mobile_ui::col_fg, stat.second );
            x += tile_w + tile_gap;
        }
        y += tile_h + gap;
    }

    // the last messages
    if( !summary.messages.empty() ) {
        const float text_w = inner_w - 24.0f * u;
        const float line_h = 19.5f * u;
        const float head_h = mobile_ui::text_size( face::mono, 10.5f, "X" ).y + 4.0f * u;
        std::vector<std::vector<std::string>> wrapped;
        float lines = 0.0f;
        for( const std::string &message : summary.messages ) {
            wrapped.push_back( wrap( face::body, 13.0f, message, text_w ) );
            lines += static_cast<float>( wrapped.back().size() );
        }
        const float box_h = 10.0f * u + head_h + lines * line_h + 10.0f * u;
        mobile_ui::box( draw, ImVec2( inner_x, y ), ImVec2( inner_x + inner_w, y + box_h ),
                        IM_COL32( 255, 255, 255, 10 ), 0, 12.0f );
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( inner_x + 12.0f * u, y + 10.0f * u ),
                         mobile_ui::col_muted, _( "LAST MESSAGES" ) );
        float ty = y + 10.0f * u + head_h;
        for( size_t i = 0; i < wrapped.size(); ++i ) {
            // the newest one stands out
            const ImU32 ink = i + 1 == wrapped.size() ? col_died : mobile_ui::col_text_soft;
            for( const std::string &line : wrapped[i] ) {
                const float text_h = mobile_ui::text_size( face::body, 13.0f, line ).y;
                mobile_ui::text( draw, face::body, 13.0f,
                                 ImVec2( inner_x + 12.0f * u, ty + ( line_h - text_h ) * 0.5f ), ink, line );
                ty += line_h;
            }
        }
        y += box_h + gap;
    }

    // last words
    {
        y += 6.0f * u;
        const std::string label = _( "LAST WORDS, FOR THE MEMORIAL FILE" );
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( inner_x, y ), mobile_ui::col_muted, label );
        y += mobile_ui::text_size( face::mono, 10.5f, label ).y + gap;
        const float field_h = 44.0f * u;
        const float font_px = 15.0f * u;
        ImGui::PushFont( nullptr, font_px );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding,
                             ImVec2( 12.0f * u, ( field_h - ImGui::GetFontSize() ) * 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 11.0f * u );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, std::max( 1.0f, u ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, col_input );
        ImGui::PushStyleColor( ImGuiCol_Border, typing ? mobile_ui::col_accent : mobile_ui::col_line );
        ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
        ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
        ImGui::SetCursorScreenPos( ImVec2( inner_x, y ) );
        ImGui::SetNextItemWidth( inner_w );
        ImGui::InputTextWithHint( "##last_words", _( "Optional" ), &last_words );
        typing = ImGui::IsItemActive();
        ImGui::PopStyleColor( 4 );
        ImGui::PopStyleVar( 3 );
        ImGui::PopFont();
        if( last_words.size() > max_last_words ) {
            last_words.resize( max_last_words );
        }
        y += field_h;
    }
    y += pad;

    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

class page_window : public cataimgui::window
{
    public:
        page_window( const std::string &label, const std::string &title, const std::string &text ) :
            cataimgui::window( "MOBILE_DEATH_PAGE",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            label( label ), title( title ), text( text ) {}
        bool done = false;
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        std::string label;
        std::string title;
        std::string text;
};

void page_window::draw_controls()
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

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;

    // Continue, in a bar at the bottom
    const float button_h = 48.0f * u;
    const float bar_h = 10.0f * u + button_h + 10.0f * u + safe.w;
    const float bar_y = origin.y + size.y - bar_h;
    back->AddRectFilled( ImVec2( origin.x, bar_y ), ImVec2( origin.x + size.x, origin.y + size.y ),
                         col_bar );
    back->AddLine( ImVec2( origin.x, bar_y ), ImVec2( origin.x + size.x, bar_y ), mobile_ui::col_line,
                   std::max( 1.0f, u ) );
    {
        const ImVec2 pos( left + 12.0f * u, bar_y + 10.0f * u );
        const ImVec2 button_size( width - 24.0f * u, button_h );
        bool held = false;
        if( mobile_ui::pressable( "##page_continue", pos, button_size, &held ) ) {
            done = true;
        }
        mobile_ui::box( back, pos, ImVec2( pos.x + button_size.x, pos.y + button_size.y ),
                        held ? mobile_ui::col_accent_down : mobile_ui::col_accent, 0, 12.0f );
        const std::string name = _( "Continue" );
        const ImVec2 name_px = mobile_ui::text_size( face::bold, 15.0f, name );
        mobile_ui::text( back, face::bold, 15.0f, ImVec2( pos.x + ( button_size.x - name_px.x ) * 0.5f,
                         pos.y + ( button_size.y - name_px.y ) * 0.5f ), mobile_ui::col_on_accent, name );
    }

    // everything above the bar scrolls
    ImGui::SetCursorScreenPos( ImVec2( left, origin.y + safe.y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##page_scroll", ImVec2( width, bar_y - origin.y - safe.y ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float pad = 20.0f * u;
    // text is easier to read in a column than across a phone held sideways
    const float inner_w = std::min( width - 2.0f * pad, 560.0f * u );
    const float inner_x = base.x + ( width - inner_w ) * 0.5f;
    float y = base.y + 22.0f * u;
    if( !label.empty() ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( inner_x, y ), mobile_ui::col_muted, label );
        y += mobile_ui::text_size( face::mono, 10.5f, label ).y + 6.0f * u;
    }
    mobile_ui::text( draw, face::display, 34.0f, ImVec2( inner_x, y ), col_died, title, inner_w );
    y += mobile_ui::text_size( face::display, 34.0f, title, inner_w ).y + 12.0f * u;
    const float text_w = inner_w - 28.0f * u;
    const float text_h = mobile_ui::text_size( face::body, 15.0f, text, text_w ).y;
    mobile_ui::box( draw, ImVec2( inner_x, y ), ImVec2( inner_x + inner_w, y + text_h + 28.0f * u ),
                    IM_COL32( 255, 255, 255, 10 ), 0, 12.0f );
    mobile_ui::text( draw, face::body, 15.0f, ImVec2( inner_x + 14.0f * u, y + 14.0f * u ), col_cause,
                     text, text_w );
    y += text_h + 28.0f * u + pad;
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace

void show_page( const std::string &label, const std::string &title, const std::string &text )
{
    // upstream's texts are indented for its text window
    std::string body = remove_color_tags( text );
    const size_t from = body.find_first_not_of( " \n" );
    const size_t to = body.find_last_not_of( " \n" );
    body = from == std::string::npos ? std::string() : body.substr( from, to - from + 1 );
    page_window window( label, remove_color_tags( title ), body );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "ANY_INPUT" );
    while( !window.done ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        // the back button goes on, as there is nothing to go back to
        if( action == "QUIT" || action == "CONFIRM" ) {
            break;
        }
    }
}

void note_death()
{
    const avatar &u = get_avatar();
    noted = death_note();
    noted.taken = true;
    noted.suicide = g->uquit == QUIT_SUICIDE;
    if( const Creature *const killer = u.get_killer() ) {
        noted.killer = killer->disp_name();
    }
    noted.place = remove_color_tags( overmap_buffer.get_description_at( u.pos_abs_sm(), false ) );
    noted.when = to_string( calendar::turn );
}

void show()
{
    if( !noted.taken ) {
        // not told about the death: the killer may be gone by now, so leave it out
        noted.suicide = g->uquit == QUIT_SUICIDE;
        noted.when = to_string( calendar::turn );
    }
    choice picked = choice::main_menu;
    std::string last_words;
    {
        death_window window( summarize() );
        input_context ctxt( "default" );
        const mobile_prompt::own_controls own_controls( ctxt );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "ANY_INPUT" );
        while( window.result == choice::none ) {
            ui_manager::redraw();
            // the back button leaves for the main menu, but not while typing
            if( ctxt.handle_input() == "QUIT" && !window.typing ) {
                break;
            }
        }
        if( window.result != choice::none ) {
            picked = window.result;
        }
        last_words = window.last_words;
    }
    noted = death_note();

    // what upstream's tombstone screen sends when it closes
    const avatar &u = get_avatar();
    get_event_bus().send<event_type::game_avatar_death>( u.getID(), u.name,
            g->uquit == QUIT_SUICIDE, last_words );

    new_character_world.reset();
    if( picked == choice::new_character && world_generator->active_world != nullptr ) {
        new_character_world = world_generator->active_world->world_name;
    }
}

std::optional<std::string> take_new_character_world()
{
    std::optional<std::string> ret = new_character_world;
    new_character_world.reset();
    return ret;
}

} // namespace mobile_death

#else // !TILES

namespace mobile_death
{

void note_death() {}

void show_page( const std::string &, const std::string &, const std::string & ) {}

void show() {}

std::optional<std::string> take_new_character_world()
{
    return std::nullopt;
}

} // namespace mobile_death

#endif // TILES
