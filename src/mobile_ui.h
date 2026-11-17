#pragma once
#ifndef CATA_SRC_MOBILE_UI_H
#define CATA_SRC_MOBILE_UI_H

#if defined(TILES)

#include <string>
#include <vector>

#include "imgui/imgui.h"

struct SDL_Texture;

// Shared look of the mobile UI fork: the colors, type, icons and basic controls
// of the UI canvas, for the screens in mobile_menu.cpp and friends.
//
// Sizes are given in canvas units; one unit is 1/390 of the short side of the
// screen, the same as a CSS pixel on the canvas's phone.
namespace mobile_ui
{

// --- Colors (the canvas's game palette) ---
constexpr ImU32 col_bg = IM_COL32( 11, 13, 10, 255 );
constexpr ImU32 col_screen = IM_COL32( 14, 17, 13, 255 );
constexpr ImU32 col_panel = IM_COL32( 20, 24, 18, 255 );
constexpr ImU32 col_sheet = IM_COL32( 22, 27, 20, 255 );
constexpr ImU32 col_button = IM_COL32( 22, 27, 20, 230 );
constexpr ImU32 col_button_down = IM_COL32( 58, 42, 28, 255 );
constexpr ImU32 col_fill_soft = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_line = IM_COL32( 232, 236, 222, 36 );
constexpr ImU32 col_fg = IM_COL32( 233, 236, 223, 255 );
constexpr ImU32 col_title = IM_COL32( 243, 239, 224, 255 );
constexpr ImU32 col_text_soft = IM_COL32( 191, 198, 181, 255 );
constexpr ImU32 col_muted = IM_COL32( 154, 163, 146, 255 );
constexpr ImU32 col_accent = IM_COL32( 240, 145, 63, 255 );
constexpr ImU32 col_accent_down = IM_COL32( 204, 115, 41, 255 );
constexpr ImU32 col_accent_soft = IM_COL32( 240, 145, 63, 36 );
constexpr ImU32 col_on_accent = IM_COL32( 26, 15, 6, 255 );
constexpr ImU32 col_dim = IM_COL32( 0, 0, 0, 128 );

// --- Type ---
enum class face : int {
    // Barlow Medium
    body,
    // Barlow Bold
    bold,
    // Barlow Condensed Bold, for titles
    display,
    // JetBrains Mono, for labels and numbers
    mono,
};

// Adds the canvas fonts to the atlas.  Called whenever ImGui's fonts are (re)built.
void add_fonts( ImGuiIO &io );

// Pixels per canvas unit.
float unit();

// Space taken by notches and system bars: x = left, y = top, z = right, w = bottom.
ImVec4 safe_insets();

ImVec2 text_size( face f, float size_units, const std::string &text, float wrap_width = 0.0f );
void text( ImDrawList *draw, face f, float size_units, const ImVec2 &pos, ImU32 color,
           const std::string &text, float wrap_width = 0.0f );

// --- Icons (data/mobile_ui/icons/<name>.svg) ---
void icon( ImDrawList *draw, const std::string &name, const ImVec2 &pos, float size_units,
           ImU32 color );

// The icon as a white texture to tint, or nullptr.  Owned by the cache here.
SDL_Texture *icon_texture( const std::string &name );

// --- Controls ---

// An invisible button over a rectangle.  Returns true when tapped; held tells
// whether it is being pressed, for drawing.
bool pressable( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                bool *held = nullptr );

// A rounded rectangle with an optional border.
void box( ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, ImU32 fill, ImU32 border,
          float radius_units );

// The bar at the top of a full screen: back arrow and title.  Returns true if
// back was tapped.  height_out receives the height it took.
bool app_bar( const std::string &title, const ImVec2 &pos, float width, float *height_out );

// Call before redrawing a sheet, a dialog or a card that leaves the screen
// around it visible: the map under it is drawn again, so that it shows dimmed
// and not as a black screen.  Their windows are one pixel short of the screen
// for the same reason (see ui_adaptor::invalidate_ui).
void keep_map_under();

// --- Bottom sheet ---

struct sheet_row {
    std::string icon;
    std::string title;
    std::string detail;
};

struct sheet_spec {
    std::string title;
    // a paragraph under the title
    std::string description;
    std::vector<sheet_row> rows;
    // rows as a two-column grid of buttons instead of a list
    bool grid = false;
};

constexpr int sheet_nothing = -2;
constexpr int sheet_dismissed = -1;

// Draws a sheet rising from the bottom edge over a dimmed screen.  Returns the
// index of the row that was tapped, sheet_dismissed for a tap outside, or
// sheet_nothing.
int draw_sheet( const sheet_spec &spec, const ImVec2 &origin, const ImVec2 &size );

// Shows a sheet over the game and waits for a choice.  Returns the row index,
// or sheet_dismissed.
int show_sheet( const sheet_spec &spec );

} // namespace mobile_ui

#endif // TILES

#endif // CATA_SRC_MOBILE_UI_H
