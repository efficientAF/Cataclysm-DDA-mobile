#include "mobile_richtext.h"

#if defined(TILES)

#include <algorithm>
#include <cstddef>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "cata_imgui.h"
#include "color.h"
#include "output.h"

namespace mobile_richtext
{

using mobile_ui::face;

void block::clear()
{
    lines.clear();
    laid_width = -1.0f;
}

void block::set( const std::string &text, const ImU32 base, const bool rules )
{
    clear();
    with_rules = rules;
    add_lines( text, base, 0 );
}

void block::add( const std::string &text, const ImU32 base, const int indent )
{
    laid_width = -1.0f;
    add_lines( text, base, indent );
}

void block::add_lines( const std::string &text, const ImU32 base, const int indent )
{
    // a tag may stay open over a newline
    std::stack<ImU32> colors;
    line current;
    current.indent = indent;
    const auto flush = [&]() {
        if( with_rules ) {
            std::string plain;
            for( const run &r : current.runs ) {
                plain += r.text;
            }
            if( !plain.empty() && plain.find_first_not_of( "- " ) == std::string::npos ) {
                current.runs.clear();
                current.rule = true;
            }
        }
        lines.push_back( current );
        current = line();
        current.indent = indent;
    };
    const auto put = [&]( const std::string_view part ) {
        const ImU32 color = colors.empty() ? base : colors.top();
        size_t from = 0;
        while( true ) {
            const size_t nl = part.find( '\n', from );
            const std::string_view piece = part.substr( from, nl == std::string_view::npos ? nl : nl - from );
            if( !piece.empty() ) {
                current.runs.push_back( { color, std::string( piece ) } );
            }
            if( nl == std::string_view::npos ) {
                break;
            }
            flush();
            from = nl + 1;
        }
    };
    for( const std::string &seg : split_by_color( text ) ) {
        if( seg.empty() ) {
            continue;
        }
        if( seg[0] == '<' ) {
            const color_tag_parse_result tag = get_color_from_tag( seg, report_color_error::no );
            if( tag.type == color_tag_parse_result::open_color_tag ) {
                colors.push( ImGui::ColorConvertFloat4ToU32( cataimgui::imvec4_from_color( tag.color ) ) );
                put( rm_prefix( seg ) );
                continue;
            }
            if( tag.type == color_tag_parse_result::close_color_tag ) {
                if( !colors.empty() ) {
                    colors.pop();
                }
                put( rm_prefix( seg ) );
                continue;
            }
        }
        put( seg );
    }
    flush();
}

float block::height( const face f, const float size_units, const float width )
{
    const float u = mobile_ui::unit();
    if( laid_width == width && laid_face == f && laid_size == size_units && laid_unit == u ) {
        return laid_height;
    }
    laid_width = width;
    laid_face = f;
    laid_size = size_units;
    laid_unit = u;
    pieces.clear();
    rule_ys.clear();
    const float line_h = mobile_ui::text_size( f, size_units, "Ag" ).y + 3.0f * u;
    const float char_w = mobile_ui::text_size( f, size_units, "0" ).x;
    float y = 0.0f;
    for( const line &l : lines ) {
        if( l.rule ) {
            y += 6.0f * u;
            rule_ys.push_back( y );
            y += 8.0f * u;
            continue;
        }
        if( l.runs.empty() ) {
            y += line_h * 0.5f;
            continue;
        }
        const float left = std::min( static_cast<float>( l.indent ) * char_w, width * 0.25f );
        float x = left;
        bool line_start = true;
        const auto emit = [&]( const std::string & word, const ImU32 color, const float w ) {
            if( !pieces.empty() && pieces.back().y == y && pieces.back().color == color &&
                !line_start ) {
                pieces.back().text += word;
            } else {
                pieces.push_back( { x, y, color, word } );
            }
            x += w;
            line_start = false;
        };
        for( const run &r : l.runs ) {
            size_t at = 0;
            while( at < r.text.size() ) {
                // a token is a run of spaces or a run of anything else
                const bool space = r.text[at] == ' ';
                size_t end = at;
                while( end < r.text.size() && ( r.text[end] == ' ' ) == space ) {
                    ++end;
                }
                const std::string token = r.text.substr( at, end - at );
                at = end;
                const float w = mobile_ui::text_size( f, size_units, token ).x;
                if( space ) {
                    // spaces at the start of a line are an indent that upstream meant
                    if( x + w <= width || line_start ) {
                        emit( token, r.color, w );
                    }
                    continue;
                }
                if( x + w > width && x > left ) {
                    y += line_h;
                    x = left;
                    line_start = true;
                }
                emit( token, r.color, w );
            }
        }
        y += line_h;
    }
    laid_height = y;
    return laid_height;
}

void block::draw( ImDrawList *const draw, const ImVec2 &pos ) const
{
    const float u = mobile_ui::unit();
    const ImVec2 clip_min = draw->GetClipRectMin();
    const ImVec2 clip_max = draw->GetClipRectMax();
    for( const piece &p : pieces ) {
        const float y = pos.y + p.y;
        if( y > clip_max.y || y + 40.0f * u < clip_min.y ) {
            continue;
        }
        mobile_ui::text( draw, laid_face, laid_size, ImVec2( pos.x + p.x, y ), p.color, p.text );
    }
    for( const float rule_y : rule_ys ) {
        draw->AddLine( ImVec2( pos.x, pos.y + rule_y ), ImVec2( pos.x + laid_width, pos.y + rule_y ),
                       mobile_ui::col_line, std::max( 1.0f, u ) );
    }
}

} // namespace mobile_richtext

#endif // TILES
