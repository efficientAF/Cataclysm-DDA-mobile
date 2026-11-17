#include "mobile_describe.h"

#include <algorithm>
#include <cstddef>

#include "character.h"
#include "creature.h"
#include "creature_tracker.h"
#include "map.h"
#include "mapdata.h"
#include "mobile_menu.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "ui_extended_description.h"
#include "viewer.h"
#include "vpart_position.h"

#if defined(TILES)
#include "cata_imgui.h"
#include "color.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "output.h"
#include "ui_manager.h"
#endif

static const trait_id trait_ILLITERATE( "ILLITERATE" );

// Upstream's window gathers the texts in its constructor; this puts them on
// the phone's page, with the sign and the graffiti its tabs add.
void extended_description_window::mobile_show()
{
    const map &here = get_map();
    const bool illiterate = get_player_character().has_trait( trait_ILLITERATE );
    std::vector<mobile_describe::page> pages;
    int first = 0;
    if( !creature_description.empty() ) {
        pages.push_back( { _( "Creature" ), creature_description } );
    }
    if( !furniture_description.empty() ) {
        if( switch_target == description_target::furniture ) {
            first = static_cast<int>( pages.size() );
        }
        pages.push_back( { _( "Furniture" ), furniture_description } );
        const std::string signage = here.get_signage( p );
        if( !signage.empty() ) {
            pages.back().parts.emplace_back( "--" );
            // NOLINTNEXTLINE(cata-text-style): the question mark does not end a sentence
            pages.back().parts.push_back( illiterate ? _( "Sign: ???" ) :
                                          string_format( _( "Sign: %s" ), signage ) );
        }
    }
    if( !terrain_description.empty() ) {
        if( switch_target == description_target::terrain ) {
            first = static_cast<int>( pages.size() );
        }
        pages.push_back( { _( "Terrain" ), terrain_description } );
        const std::string graffiti = here.graffiti_at( p );
        if( !graffiti.empty() ) {
            pages.back().parts.emplace_back( "--" );
            // NOLINTNEXTLINE(cata-text-style): the question mark does not end a sentence
            pages.back().parts.push_back( illiterate ? _( "Inscription: ???" ) :
                                          string_format( _( "Inscription: %s" ), graffiti ) );
        }
    }
    if( !veh_app_description.empty() ) {
        if( switch_target == description_target::vehicle ) {
            first = static_cast<int>( pages.size() );
        }
        pages.push_back( { _( "Vehicle/Appliance" ), veh_app_description } );
    }
    if( pages.empty() ) {
        pages.push_back( { _( "Terrain" ), { _( "You can't see the terrain here." ) } } );
    }
    mobile_describe::show( _( "Description" ), pages, first );
}

namespace mobile_describe
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

// A run of text in one color; a line break is a piece of its own.
struct piece {
    std::string text;
    ImU32 color = 0;
    bool line_break = false;
    float width = 0.0f;
};

constexpr float text_units = 14.0f;

// Splits text with color tags into words, each with its color.
std::vector<piece> to_pieces( const std::string &tagged )
{
    std::vector<piece> out;
    std::vector<ImU32> colors;
    const auto add_words = [&]( const std::string & text ) {
        const ImU32 color = colors.empty() ? mobile_ui::col_text_soft : colors.back();
        std::string word;
        const auto flush = [&]() {
            if( !word.empty() ) {
                piece p;
                p.text = word;
                p.color = color;
                p.width = mobile_ui::text_size( face::body, text_units, word ).x;
                out.push_back( p );
                word.clear();
            }
        };
        for( const char c : text ) {
            if( c == '\n' ) {
                flush();
                piece p;
                p.line_break = true;
                out.push_back( p );
            } else if( c == ' ' ) {
                // the space stays with the word before it
                word += c;
                flush();
            } else {
                word += c;
            }
        }
        flush();
    };
    for( const std::string &part : split_by_color( tagged ) ) {
        if( part.empty() ) {
            continue;
        }
        if( part[0] == '<' ) {
            const color_tag_parse_result tag = get_color_from_tag( part, report_color_error::no );
            if( tag.type == color_tag_parse_result::open_color_tag ) {
                // upstream's plain text colors become the page's own
                colors.push_back( tag.color == c_light_gray || tag.color == c_white || tag.color == c_unset ?
                                  mobile_ui::col_text_soft : cataimgui::ImU32_from_color( tag.color ) );
                continue;
            }
            if( tag.type == color_tag_parse_result::close_color_tag ) {
                if( !colors.empty() ) {
                    colors.pop_back();
                }
                continue;
            }
        }
        add_words( part );
    }
    return out;
}

// One paragraph, or a divider.
struct block {
    bool divider = false;
    std::vector<piece> pieces;
};

class describe_window : public cataimgui::window
{
    public:
        describe_window( const std::string &title, const std::vector<page> &pages, const int first ) :
            cataimgui::window( "MOBILE_DESCRIBE",
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ),
            current( std::clamp( first, 0, std::max( 0, static_cast<int>( pages.size() ) - 1 ) ) ),
            title( title ), pages( pages ) {}
        bool done = false;
        int current;
        void turn( const int by ) {
            const int count = static_cast<int>( pages.size() );
            if( count > 0 ) {
                current = ( current + by + count ) % count;
                rewind = true;
            }
        }
    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;
    private:
        std::string title;
        const std::vector<page> &pages;
        // the words of the pages, measured once the fonts are there
        std::vector<std::vector<block>> blocks;
        float measured_for = 0.0f;
        bool rewind = false;
};

void describe_window::draw_controls()
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

    if( measured_for != u ) {
        measured_for = u;
        blocks.clear();
        for( const page &pg : pages ) {
            std::vector<block> made;
            for( const std::string &part : pg.parts ) {
                block b;
                if( part == "--" ) {
                    b.divider = true;
                } else {
                    b.pieces = to_pieces( part );
                }
                made.push_back( b );
            }
            blocks.push_back( made );
        }
    }

    const float avail = size.x - safe.x - safe.z;
    const float width = std::min( avail, 620.0f * u );
    const float left = origin.x + safe.x + ( avail - width ) * 0.5f;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( title, ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        done = true;
    }
    float y = origin.y + safe.y + bar_h;

    // a chip for each thing on the tile
    if( pages.size() > 1 ) {
        y += 6.0f * u;
        float x = left + 12.0f * u;
        for( size_t i = 0; i < pages.size(); ++i ) {
            const std::string &label = pages[i].name;
            const float w = mobile_ui::text_size( face::bold, 13.0f, label ).x + 26.0f * u;
            const bool on = current == static_cast<int>( i );
            if( x + w > left + width - 12.0f * u && x > left + 12.0f * u ) {
                x = left + 12.0f * u;
                y += 42.0f * u;
            }
            if( mobile_ui::pressable( "##describe_tab" + std::to_string( i ), ImVec2( x, y ),
                                      ImVec2( w, 36.0f * u ) ) && !on ) {
                current = static_cast<int>( i );
                rewind = true;
            }
            mobile_ui::box( back, ImVec2( x, y ), ImVec2( x + w, y + 36.0f * u ),
                            on ? mobile_ui::col_accent : 0, on ? 0 : mobile_ui::col_line, 18.0f );
            mobile_ui::text( back, face::bold, 13.0f, ImVec2( x + 13.0f * u, y + 10.0f * u ),
                             on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, label );
            x += w + 6.0f * u;
        }
        y += 36.0f * u + 6.0f * u;
    } else if( !pages.empty() ) {
        mobile_ui::text( back, face::mono, 10.5f, ImVec2( left + 16.0f * u, y + 6.0f * u ),
                         mobile_ui::col_muted, to_upper_case( pages.front().name ) );
        y += 24.0f * u;
    }

    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##describe_text", ImVec2( width, origin.y + size.y - safe.w - y ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    if( rewind ) {
        ImGui::SetScrollY( 0.0f );
        rewind = false;
    }
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float wrap = width - 32.0f * u;
    const float x0 = base.x + 16.0f * u;
    const float line_h = mobile_ui::text_size( face::body, text_units, "Ag" ).y + 3.0f * u;
    float ty = base.y + 8.0f * u;
    if( current >= 0 && static_cast<size_t>( current ) < blocks.size() ) {
        for( const block &b : blocks[static_cast<size_t>( current )] ) {
            if( b.divider ) {
                ty += 4.0f * u;
                draw->AddLine( ImVec2( x0, ty ), ImVec2( x0 + wrap, ty ), mobile_ui::col_line,
                               std::max( 1.0f, u ) );
                ty += 10.0f * u;
                continue;
            }
            float tx = 0.0f;
            for( const piece &p : b.pieces ) {
                if( p.line_break ) {
                    tx = 0.0f;
                    ty += line_h;
                    continue;
                }
                if( tx > 0.0f && tx + p.width > wrap ) {
                    tx = 0.0f;
                    ty += line_h;
                }
                mobile_ui::text( draw, face::body, text_units, ImVec2( x0 + tx, ty ), p.color, p.text );
                tx += p.width;
            }
            ty += line_h + 5.0f * u;
        }
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, ty - base.y + 24.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace

void show( const std::string &title, const std::vector<page> &pages, const int first )
{
    describe_window window( title, pages, first );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "ANY_INPUT" );
    while( !window.done ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" || action == "CONFIRM" ) {
            break;
        } else if( action == "LEFT" ) {
            // a swipe to the right
            window.turn( -1 );
        } else if( action == "RIGHT" ) {
            window.turn( 1 );
        }
    }
}

#else // !TILES

void show( const std::string &, const std::vector<page> &, int ) {}

#endif // TILES

} // namespace mobile_describe
