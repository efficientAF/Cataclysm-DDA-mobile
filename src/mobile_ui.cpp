#if defined(TILES)

#include "mobile_ui.h"
#include "mobile_prompt.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>

#include "cata_imgui.h"
#include "filesystem.h"
#include "game.h"
#include "input_context.h"
#include "path_info.h"
#include "sdl_wrappers.h"
#include "sdltiles.h"
#include "ui_manager.h"

namespace mobile_ui
{

// --- Type ---

static std::array<ImFont *, 4> fonts = { nullptr, nullptr, nullptr, nullptr };

void add_fonts( ImGuiIO &io )
{
    static const std::array<const char *, 4> files = {
        "Barlow-Medium.ttf", "Barlow-Bold.ttf", "BarlowCondensed-Bold.ttf", "JetBrainsMono-Medium.ttf"
    };
    for( size_t i = 0; i < files.size(); ++i ) {
        const std::string path = PATH_INFO::fontdir() + files[i];
        fonts[i] = file_exist( path ) ? io.Fonts->AddFontFromFileTTF( path.c_str(), 16.0f ) : nullptr;
    }
}

static ImFont *font_for( const face f )
{
    ImFont *const wanted = fonts[static_cast<size_t>( f )];
    // fall back to the game's font if ours did not load
    return wanted != nullptr ? wanted : ImGui::GetFont();
}

float unit()
{
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    return std::max( std::min( size.x, size.y ) / 390.0f, 0.5f );
}

ImVec4 safe_insets()
{
    ImVec4 insets( 0.0f, 0.0f, 0.0f, 0.0f );
    SDL_Window *const win = get_sdl_window();
    int w = 0;
    int h = 0;
    SDL_Rect safe;
    if( win == nullptr || !SDL_GetWindowSize( win, &w, &h ) || w <= 0 || h <= 0 ||
        !SDL_GetWindowSafeArea( win, &safe ) || safe.w <= 0 || safe.h <= 0 ) {
        return insets;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sx = display.x / static_cast<float>( w );
    const float sy = display.y / static_cast<float>( h );
    insets.x = std::max( 0.0f, static_cast<float>( safe.x ) * sx );
    insets.y = std::max( 0.0f, static_cast<float>( safe.y ) * sy );
    insets.z = std::max( 0.0f, static_cast<float>( w - safe.x - safe.w ) * sx );
    insets.w = std::max( 0.0f, static_cast<float>( h - safe.y - safe.h ) * sy );
    return insets;
}

ImVec2 text_size( const face f, const float size_units, const std::string &text,
                  const float wrap_width )
{
    return font_for( f )->CalcTextSizeA( size_units * unit(), FLT_MAX, wrap_width, text.c_str() );
}

void text( ImDrawList *const draw, const face f, const float size_units, const ImVec2 &pos,
           const ImU32 color, const std::string &text, const float wrap_width )
{
    draw->AddText( font_for( f ), size_units * unit(), pos, color, text.c_str(), nullptr,
                   wrap_width );
}

void keep_map_under()
{
    if( g != nullptr ) {
        g->invalidate_main_ui_adaptor();
    }
}

// --- Icons ---

namespace
{
struct icon_cache {
    // renderer generation the textures were made for
    uint64_t generation = 0;
    std::map<std::string, SDL_Texture *> textures;
};
} // namespace

static icon_cache icons;

// Icons are drawn once at this size and scaled down by the GPU.
static constexpr int icon_pixels = 96;

SDL_Texture *icon_texture( const std::string &name )
{
    const uint64_t generation = renderer_resource_generation();
    if( icons.generation != generation ) {
        // the renderer was rebuilt and took the old textures with it
        icons.textures.clear();
        icons.generation = generation;
    }
    const auto found = icons.textures.find( name );
    if( found != icons.textures.end() ) {
        return found->second;
    }
    SDL_Texture *texture = nullptr;
    const std::string path = PATH_INFO::datadir() + "mobile_ui/icons/" + name + ".svg";
    const SDL_Renderer_Ptr &renderer = get_sdl_renderer();
    if( renderer && file_exist( path ) ) {
        if( SDL_IOStream *const io = SDL_IOFromFile( path.c_str(), "rb" ) ) {
            if( SDL_Surface *const surface = IMG_LoadSizedSVG_IO( io, icon_pixels, icon_pixels ) ) {
                texture = SDL_CreateTextureFromSurface( renderer.get(), surface );
                SDL_DestroySurface( surface );
            }
            SDL_CloseIO( io );
        }
    }
    icons.textures[name] = texture;
    return texture;
}

void icon( ImDrawList *const draw, const std::string &name, const ImVec2 &pos,
           const float size_units, const ImU32 color )
{
    SDL_Texture *const texture = icon_texture( name );
    if( texture == nullptr ) {
        return;
    }
    const float size = size_units * unit();
    draw->AddImage( ImTextureRef( static_cast<ImTextureID>( reinterpret_cast<intptr_t>( texture ) ) ),
                    pos, ImVec2( pos.x + size, pos.y + size ), ImVec2( 0.0f, 0.0f ), ImVec2( 1.0f, 1.0f ), color );
}

// --- Controls ---

bool pressable( const std::string &id, const ImVec2 &pos, const ImVec2 &size, bool *const held )
{
    ImGui::SetCursorScreenPos( pos );
    const bool tapped = ImGui::InvisibleButton( id.c_str(), size );
    if( held != nullptr ) {
        *held = ImGui::IsItemActive();
    }
    return tapped;
}

void box( ImDrawList *const draw, const ImVec2 &min, const ImVec2 &max, const ImU32 fill,
          const ImU32 border, const float radius_units )
{
    const float radius = radius_units * unit();
    if( ( fill & IM_COL32_A_MASK ) != 0 ) {
        draw->AddRectFilled( min, max, fill, radius );
    }
    if( ( border & IM_COL32_A_MASK ) != 0 ) {
        draw->AddRect( min, max, border, radius, 0, std::max( 1.0f, unit() ) );
    }
}

bool app_bar( const std::string &title, const ImVec2 &pos, const float width,
              float *const height_out )
{
    const float u = unit();
    const float height = 54.0f * u;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const ImVec2 back_pos( pos.x + 4.0f * u, pos.y + 5.0f * u );
    const bool back = pressable( "##appbar_back", back_pos, ImVec2( 44.0f * u, 44.0f * u ), &held );
    if( held ) {
        box( draw, back_pos, ImVec2( back_pos.x + 44.0f * u, back_pos.y + 44.0f * u ), col_fill_soft, 0,
             12.0f );
    }
    icon( draw, "back", ImVec2( back_pos.x + 11.0f * u, back_pos.y + 11.0f * u ), 22.0f, col_fg );
    const ImVec2 title_size = text_size( face::display, 21.0f, title );
    text( draw, face::display, 21.0f, ImVec2( pos.x + 52.0f * u,
            pos.y + ( height - title_size.y ) * 0.5f ), col_fg, title );
    draw->AddLine( ImVec2( pos.x, pos.y + height ), ImVec2( pos.x + width, pos.y + height ), col_line,
                   std::max( 1.0f, u ) );
    if( height_out != nullptr ) {
        *height_out = height;
    }
    return back;
}

// --- Bottom sheet ---

int draw_sheet( const sheet_spec &spec, const ImVec2 &origin, const ImVec2 &size )
{
    const float u = unit();
    const ImVec4 safe = safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    int result = sheet_nothing;

    // dim the screen behind; a tap there closes the sheet
    draw->AddRectFilled( origin, screen_max, col_dim );
    const bool outside = pressable( "##sheet_outside", origin, size );

    const float sheet_w = landscape ? std::min( 520.0f * u, size.x * 0.76f ) : size.x;
    const float max_h = ( size.y - safe.y ) * ( landscape ? 0.92f : 0.84f );
    const float body_w = sheet_w - 36.0f * u;
    const float desc_h = spec.description.empty() ? 0.0f :
                         text_size( face::body, 13.5f, spec.description, body_w ).y + 12.0f * u;
    const float head_h = 22.0f * u + 36.0f * u;
    const float row_h = 54.0f * u;
    const float cell_h = 48.0f * u;
    const float gap = 8.0f * u;
    const int count = static_cast<int>( spec.rows.size() );
    const float rows_h = spec.grid ?
                         static_cast<float>( ( count + 1 ) / 2 ) * ( cell_h + gap ) + 10.0f * u :
                         row_h * static_cast<float>( count );
    const float sheet_h = std::min( max_h, head_h + desc_h + rows_h + safe.w + 6.0f * u );
    const ImVec2 pos( origin.x + ( size.x - sheet_w ) * 0.5f, screen_max.y - sheet_h );
    draw->AddRectFilled( pos, ImVec2( pos.x + sheet_w, screen_max.y ), col_sheet, 22.0f * u,
                         ImDrawFlags_RoundCornersTop );
    draw->AddRect( pos, ImVec2( pos.x + sheet_w, screen_max.y + 30.0f * u ), col_line, 22.0f * u,
                   ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    // grab handle
    draw->AddRectFilled( ImVec2( pos.x + sheet_w * 0.5f - 21.0f * u, pos.y + 9.0f * u ),
                         ImVec2( pos.x + sheet_w * 0.5f + 21.0f * u, pos.y + 14.0f * u ),
                         IM_COL32( 255, 255, 255, 71 ), 3.0f * u );
    text( draw, face::display, 21.0f, ImVec2( pos.x + 18.0f * u, pos.y + 24.0f * u ), col_fg,
          spec.title );

    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + head_h ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##sheet_scroll", ImVec2( sheet_w, sheet_h - head_h - safe.w - 6.0f * u ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    ImDrawList *const inner = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float y = base.y;
    if( !spec.description.empty() ) {
        text( inner, face::body, 13.5f, ImVec2( base.x + 18.0f * u, y ), IM_COL32( 201, 207, 191, 255 ),
              spec.description, body_w );
        y += desc_h;
    }
    for( int i = 0; i < count; ++i ) {
        const sheet_row &row = spec.rows[i];
        const std::string id = "##sheet_row" + std::to_string( i );
        bool held = false;
        if( spec.grid ) {
            const float cell_w = ( sheet_w - 32.0f * u - gap ) * 0.5f;
            const ImVec2 cell( base.x + 16.0f * u + static_cast<float>( i % 2 ) * ( cell_w + gap ),
                               y + static_cast<float>( i / 2 ) * ( cell_h + gap ) );
            if( pressable( id, cell, ImVec2( cell_w, cell_h ), &held ) ) {
                result = i;
            }
            box( inner, cell, ImVec2( cell.x + cell_w, cell.y + cell_h ),
                 held ? col_button_down : IM_COL32( 255, 255, 255, 15 ), col_line, 12.0f );
            const ImVec2 label = text_size( face::bold, 14.0f, row.title );
            text( inner, face::bold, 14.0f, ImVec2( cell.x + ( cell_w - label.x ) * 0.5f,
                                                    cell.y + ( cell_h - label.y ) * 0.5f ), col_fg, row.title );
            continue;
        }
        const ImVec2 row_pos( base.x, y );
        if( pressable( id, row_pos, ImVec2( sheet_w, row_h ), &held ) ) {
            result = i;
        }
        if( held ) {
            inner->AddRectFilled( row_pos, ImVec2( row_pos.x + sheet_w, row_pos.y + row_h ),
                                  col_button_down );
        }
        inner->AddLine( row_pos, ImVec2( row_pos.x + sheet_w, row_pos.y ), IM_COL32( 255, 255, 255, 13 ) );
        icon( inner, row.icon, ImVec2( row_pos.x + 18.0f * u, row_pos.y + 16.5f * u ), 21.0f, col_muted );
        const float text_x = row_pos.x + 53.0f * u;
        if( row.detail.empty() ) {
            text( inner, face::bold, 15.0f, ImVec2( text_x, row_pos.y + 17.0f * u ), col_fg, row.title );
        } else {
            text( inner, face::bold, 15.0f, ImVec2( text_x, row_pos.y + 9.0f * u ), col_fg, row.title );
            text( inner, face::body, 12.0f, ImVec2( text_x, row_pos.y + 30.0f * u ), col_muted,
                  row.detail );
        }
        y += row_h;
    }
    if( spec.grid ) {
        y += rows_h;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if( outside && result == sheet_nothing && ImGui::GetIO().MousePos.y < pos.y ) {
        result = sheet_dismissed;
    }
    return result;
}

namespace
{
class sheet_window : public cataimgui::window
{
    public:
        explicit sheet_window( const sheet_spec &spec ) : cataimgui::window( "MOBILE_SHEET",
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ), spec( spec ) {}
        int result = sheet_nothing;
    protected:
        cataimgui::bounds get_bounds() override {
            // one pixel short of the screen: what is under the sheet keeps being
            // redrawn and shows dimmed
            const ImVec2 screen = ImGui::GetMainViewport()->Size;
            return { 0.0f, 0.0f, screen.x, std::max( screen.y - 1.0f, 2.0f ) };
        }
        void draw_controls() override {
            const ImGuiViewport *const viewport = ImGui::GetMainViewport();
            const int tapped = draw_sheet( spec, viewport->Pos, viewport->Size );
            if( tapped != sheet_nothing ) {
                result = tapped;
            }
        }
    private:
        sheet_spec spec;
};
} // namespace

int show_sheet( const sheet_spec &spec )
{
    sheet_window window( spec );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    while( window.result == sheet_nothing ) {
        keep_map_under();
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            return sheet_dismissed;
        }
    }
    return window.result;
}

} // namespace mobile_ui

#endif // TILES
