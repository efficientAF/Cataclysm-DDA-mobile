#pragma once
#ifndef CATA_SRC_MOBILE_RICHTEXT_H
#define CATA_SRC_MOBILE_RICHTEXT_H

#if defined(TILES)

#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "mobile_ui.h"

// Text with upstream's color tags, wrapped and drawn in the mobile look: the
// compared item texts (better and worse values keep their colors) and the
// lines of a computer terminal.
namespace mobile_richtext
{

class block
{
    public:
        // Replaces the text.  It may carry color tags and newlines; text without
        // a tag is drawn in base.  With rules, a line of dashes becomes a hairline.
        void set( const std::string &text, ImU32 base, bool rules );
        // Adds a line (or several, at newlines) with an indent in characters.
        void add( const std::string &text, ImU32 base, int indent = 0 );
        void clear();
        bool empty() const {
            return lines.empty();
        }
        // The height the text takes at this width; lays it out when something changed.
        float height( mobile_ui::face f, float size_units, float width );
        // Draws what was laid out, with its top left corner at pos.
        void draw( ImDrawList *draw, const ImVec2 &pos ) const;

    private:
        struct run {
            ImU32 color;
            std::string text;
        };
        struct line {
            std::vector<run> runs;
            int indent = 0;
            bool rule = false;
        };
        struct piece {
            float x;
            float y;
            ImU32 color;
            std::string text;
        };
        std::vector<line> lines;
        bool with_rules = false;

        // the layout and what it was made for
        std::vector<piece> pieces;
        std::vector<float> rule_ys;
        mobile_ui::face laid_face = mobile_ui::face::body;
        float laid_size = 0.0f;
        float laid_width = -1.0f;
        float laid_unit = 0.0f;
        float laid_height = 0.0f;

        void add_lines( const std::string &text, ImU32 base, int indent );
};

} // namespace mobile_richtext

#endif // TILES

#endif // CATA_SRC_MOBILE_RICHTEXT_H
