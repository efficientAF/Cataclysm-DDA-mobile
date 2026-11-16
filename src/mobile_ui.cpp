#if defined(TILES)

#include "mobile_ui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>

#include "filesystem.h"
#include "path_info.h"
#include "sdl_wrappers.h"
#include "sdltiles.h"

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

static SDL_Texture *icon_texture( const std::string &name )
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

} // namespace mobile_ui

#endif // TILES
