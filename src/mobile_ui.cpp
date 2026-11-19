#if defined(TILES)

#include "mobile_ui.h"
#include "mobile_prompt.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>

#include "cata_imgui.h"
#include "imgui/imgui_internal.h"
#include "filesystem.h"
#include "game.h"
#include "input_context.h"
#include "json.h"
#include "flexbuffer_json.h"
#include "cata_utility.h"
#include "translations.h"
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

// --- Screen sizes ---

namespace
{
struct kind_entry {
    const char *id;
    const char *name;
    const char *covers;
    // window names (after "MOBILE_") of this kind
    std::vector<const char *> windows;
};
} // namespace

static const std::vector<kind_entry> &kind_table()
{
    static const std::vector<kind_entry> table = {
        { "all", translate_marker( "Everything" ), translate_marker( "Every screen and the play screen's pieces, on top of their own size." ), {} },
        { "hud", translate_marker( "Play screen" ), translate_marker( "The pieces over the map: location, status, quick actions, dials, handles." ), {} },
        { "items", translate_marker( "Inventory and item lists" ), translate_marker( "Inventory, pick up, the item pickers (eat, wear, drop…), the item card." ), { "INVENTORY", "PICKER", "ITEM_CARD" } },
        { "lists", translate_marker( "List screens" ), translate_marker( "Zones, bionics, mutations, missions, vehicles, camp, factions, nearby, body, morale and the other lists with a panel." ), { "LIST" } },
        { "crafting", translate_marker( "Crafting" ), translate_marker( "The crafting screen." ), { "CRAFTING" } },
        { "sheets", translate_marker( "Sheets and questions" ), translate_marker( "Bottom sheets, yes/no questions, number and text entry, conversation." ), { "SHEET", "ENTRY" } },
        { "drawers", translate_marker( "Drawers" ), translate_marker( "Info, Log, Actions and Nearby drawers." ), { "DRAWER" } },
        { "map", translate_marker( "Map" ), translate_marker( "The map screen's buttons and sheets." ), { "OVERMAP" } },
        { "menu", translate_marker( "Main menu" ), translate_marker( "Title screen and New game." ), { "MAIN_MENU" } },
        { "chargen", translate_marker( "Character creator" ), translate_marker( "All steps of the creator." ), { "CHARGEN" } },
        { "settings", translate_marker( "Settings" ), translate_marker( "Settings, all options, worlds and mods, help." ), { "SETTINGS", "OPTIONS", "WORLD", "WORLD_MODS", "HELP", "PAD", "PAD_PICKER", "PAD_MENUS" } },
        { "pages", translate_marker( "Pages" ), translate_marker( "Describe, compare, terminal, diary text, engine controller, death and epilogues." ), { "DESCRIBE", "COMPARE", "TERMINAL", "DIARY_TEXT", "SMART_CONTROLLER", "DEATH", "DEATH_PAGE" } },
        { "overlays", translate_marker( "On the map" ), translate_marker( "Look-around banner, direction ring, HUD editor." ), { "LOOK_BANNER", "PICK_DIRECTION", "HUD_EDIT", "HUD_ORDER" } },
    };
    return table;
}

static std::map<std::string, int> &screen_percents()
{
    static std::map<std::string, int> percents;
    static bool loaded = false;
    if( !loaded ) {
        loaded = true;
        read_from_file_optional_json( PATH_INFO::config_dir_path() / "mobile_ui_scale.json",
        []( const JsonValue & jv ) {
            const JsonObject jo = jv;
            jo.allow_omitted_members();
            for( const kind_entry &k : kind_table() ) {
                if( jo.has_int( k.id ) ) {
                    percents[k.id] = std::clamp( jo.get_int( k.id ), 50, 150 );
                }
            }
        } );
    }
    return percents;
}

static float percent_of( const std::string &id )
{
    const std::map<std::string, int> &percents = screen_percents();
    const auto found = percents.find( id );
    return found == percents.end() ? 1.0f : static_cast<float>( found->second ) / 100.0f;
}

std::vector<screen_kind> screen_kinds()
{
    std::vector<screen_kind> kinds;
    for( const kind_entry &k : kind_table() ) {
        screen_kind kind;
        kind.id = k.id;
        kind.name = _( k.name );
        kind.covers = _( k.covers );
        kind.percent = static_cast<int>( std::lround( percent_of( k.id ) * 100.0f ) );
        kinds.push_back( kind );
    }
    return kinds;
}

// bumped when a size changes, so that what was worked out per window is redone
static int scale_epoch = 0;

void set_screen_percent( const std::string &id, const int percent )
{
    std::map<std::string, int> &percents = screen_percents();
    percents[id] = std::clamp( percent, 50, 150 );
    ++scale_epoch;
    write_to_file( PATH_INFO::config_dir_path() / "mobile_ui_scale.json", [&percents](
    std::ostream & out ) {
        JsonOut jsout( out, true );
        jsout.start_object();
        for( const std::pair<const std::string, int> &entry : percents ) {
            jsout.member( entry.first, entry.second );
        }
        jsout.end_object();
    }, _( "screen sizes" ) );
}

float screen_scale( const std::string &id )
{
    return id == "all" ? percent_of( "all" ) : percent_of( "all" ) * percent_of( id );
}

static std::string last_kind;

std::string last_screen_kind()
{
    return last_kind;
}

// The kind of screen a window is, from its name.
static std::string kind_of_window( const ImGuiWindow *const root )
{
    const std::string name = root->Name;
    if( name.rfind( "Debug##", 0 ) == 0 ) {
        // drawing outside any window (the prompt bar)
        return "all";
    }
    if( name.rfind( "MOBILE_", 0 ) != 0 ) {
        // upstream's windows that draw in the mobile look: sheets, questions, conversation
        return "sheets";
    }
    const std::string bare = name.substr( 7,
                                          name.find( "##" ) == std::string::npos ? std::string::npos :
                                          name.find( "##" ) - 7 );
    for( const kind_entry &k : kind_table() ) {
        for( const char *const window : k.windows ) {
            if( bare == window ) {
                if( bare != "SHEET" && bare != "ENTRY" ) {
                    last_kind = k.id;
                }
                return k.id;
            }
        }
    }
    return "all";
}

float base_unit( const float width, const float height )
{
#if !defined(__ANDROID__)
    if( width > height ) {
        return std::max( height / 640.0f, 0.5f );
    }
#endif
    return std::max( std::min( width, height ) / 390.0f, 0.5f );
}

float unit()
{
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float base = base_unit( size.x, size.y );
    const ImGuiContext *const context = ImGui::GetCurrentContext();
    const ImGuiWindow *const window = context != nullptr ? context->CurrentWindow : nullptr;
    if( window == nullptr || window->RootWindow == nullptr ) {
        return base * percent_of( "all" );
    }
    // worked out once per window and change of sizes
    static ImGuiID cached_window = 0;
    static int cached_epoch = -1;
    static float cached_scale = 1.0f;
    const ImGuiWindow *const root = window->RootWindow;
    if( root->ID != cached_window || cached_epoch != scale_epoch ) {
        cached_window = root->ID;
        cached_epoch = scale_epoch;
        cached_scale = screen_scale( kind_of_window( root ) );
    }
    return base * cached_scale;
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

// --- Backdrop ---

static bool backdrop_set = false;
static bool backdrop_used = false;

void snapshot_backdrop()
{
    backdrop_set = mobile_copy_display_buffer() != nullptr;
    // counts as used until the next frame has been drawn
    backdrop_used = true;
}

void dim_screen( ImDrawList *const draw, const ImVec2 &min, const ImVec2 &max )
{
    SDL_Texture *const texture = backdrop_set ? mobile_backdrop_texture() : nullptr;
    if( texture != nullptr ) {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        draw->AddImage( ImTextureRef( static_cast<ImTextureID>( reinterpret_cast<intptr_t>( texture ) ) ),
                        viewport->Pos, ImVec2( viewport->Pos.x + viewport->Size.x,
                                               viewport->Pos.y + viewport->Size.y ) );
        backdrop_used = true;
    }
    draw->AddRectFilled( min, max, col_dim );
}

// --- The HUD around a sheet ---

// a sheet said where it is in the frame being drawn
static bool sheet_marked = false;
// the same for the frame that was drawn last
static bool sheet_shown = false;
static ImVec4 sheet_where;
// the game drew the map in the frame being drawn, and in the one drawn last
static bool map_marked = false;
static bool map_shown = false;

void sheet_area( const ImVec2 &min, const ImVec2 &max )
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    if( viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f ) {
        return;
    }
    const ImVec4 where( ( min.x - viewport->Pos.x ) / viewport->Size.x,
                        ( min.y - viewport->Pos.y ) / viewport->Size.y,
                        ( max.x - viewport->Pos.x ) / viewport->Size.x,
                        ( max.y - viewport->Pos.y ) / viewport->Size.y );
    if( sheet_marked ) {
        // two in one frame: the box around both
        sheet_where = ImVec4( std::min( sheet_where.x, where.x ), std::min( sheet_where.y, where.y ),
                              std::max( sheet_where.z, where.z ), std::max( sheet_where.w, where.w ) );
    } else {
        sheet_where = where;
    }
    sheet_marked = true;
}

void note_map_drawn()
{
    map_marked = true;
}

bool sheet_over_play_screen( ImVec4 &where )
{
    if( !sheet_shown || !map_shown || backdrop_set ) {
        return false;
    }
    where = sheet_where;
    return true;
}

void end_frame()
{
    if( backdrop_set && !backdrop_used ) {
        backdrop_set = false;
    }
    backdrop_used = false;
    sheet_shown = sheet_marked;
    sheet_marked = false;
    map_shown = map_marked;
    map_marked = false;
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

// --- Pictures ---

namespace
{
struct loaded_picture {
    SDL_Texture *texture = nullptr;
    float w = 0.0f;
    float h = 0.0f;
};
} // namespace

static const loaded_picture &load_picture( const std::string &path )
{
    static uint64_t generation = 0;
    static std::map<std::string, loaded_picture> pictures;
    const uint64_t now = renderer_resource_generation();
    if( generation != now ) {
        // the renderer was rebuilt and took the old textures with it
        pictures.clear();
        generation = now;
    }
    const auto found = pictures.find( path );
    if( found != pictures.end() ) {
        return found->second;
    }
    loaded_picture loaded;
    const std::string file = PATH_INFO::datadir() + "mobile_ui/" + path;
    const SDL_Renderer_Ptr &renderer = get_sdl_renderer();
    if( renderer && file_exist( file ) ) {
        if( SDL_Surface *const surface = IMG_Load( file.c_str() ) ) {
            loaded.texture = SDL_CreateTextureFromSurface( renderer.get(), surface );
            loaded.w = static_cast<float>( surface->w );
            loaded.h = static_cast<float>( surface->h );
            SDL_DestroySurface( surface );
        }
    }
    return pictures[path] = loaded;
}

ImVec2 picture_size( const std::string &path, const ImVec2 &box )
{
    const loaded_picture &loaded = load_picture( path );
    if( loaded.texture == nullptr || loaded.w <= 0.0f || loaded.h <= 0.0f || box.x <= 0.0f ||
        box.y <= 0.0f ) {
        return ImVec2( 0.0f, 0.0f );
    }
    const float scale = std::min( box.x / loaded.w, box.y / loaded.h );
    return ImVec2( loaded.w * scale, loaded.h * scale );
}

ImVec2 picture( ImDrawList *const draw, const std::string &path, const ImVec2 &pos,
                const ImVec2 &box )
{
    const ImVec2 size = picture_size( path, box );
    if( size.x <= 0.0f ) {
        return size;
    }
    const loaded_picture &loaded = load_picture( path );
    draw->AddImage( ImTextureRef( static_cast<ImTextureID>( reinterpret_cast<intptr_t>
                                  ( loaded.texture ) ) ), pos, ImVec2( pos.x + size.x, pos.y + size.y ) );
    return size;
}

// --- Controls ---

// The frame around the control that the arrow keys or a controller are on.
// Drawn over everything, but not outside the list the control scrolls in.
static void focus_ring( const ImVec2 &pos, const ImVec2 &size )
{
    const float u = unit();
    const ImGuiWindow *const window = ImGui::GetCurrentWindow();
    ImDrawList *const draw = ImGui::GetForegroundDrawList();
    const float grow = 2.0f * u;
    const ImVec2 min( pos.x - grow, pos.y - grow );
    const ImVec2 max( pos.x + size.x + grow, pos.y + size.y + grow );
    draw->PushClipRect( ImVec2( window->ClipRect.Min.x - 2.0f * grow,
                                window->ClipRect.Min.y - 2.0f * grow ),
                        ImVec2( window->ClipRect.Max.x + 2.0f * grow, window->ClipRect.Max.y + 2.0f * grow ) );
    const float radius = std::min( 14.0f * u, std::min( size.x, size.y ) * 0.5f + grow );
    draw->AddRect( min, max, IM_COL32( 0, 0, 0, 200 ), radius, 0, 5.0f * u );
    draw->AddRect( min, max, col_title, radius, 0, 2.5f * u );
    draw->PopClipRect();
}

bool pressable( const std::string &id, const ImVec2 &pos, const ImVec2 &size, bool *const held )
{
    ImGui::SetCursorScreenPos( pos );
    // Reachable with the arrow keys or a controller's d-pad, and pressed with
    // Enter or A.  ImGui's own focus frame is left out; ours is in the canvas's look.
    ImGui::PushStyleColor( ImGuiCol_NavCursor, 0 );
    const bool tapped = ImGui::InvisibleButton( id.c_str(), size, ImGuiButtonFlags_EnableNav );
    ImGui::PopStyleColor();
    if( held != nullptr ) {
        *held = ImGui::IsItemActive();
    }
    if( ImGui::IsItemFocused() && GImGui->NavCursorVisible ) {
        focus_ring( pos, size );
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

static bool sheet_scroll_reset = false;

// A tap above the sheet or, where the sheet is narrower than the screen, beside it.
static bool outside_sheet( const ImVec2 &pos, const float sheet_w )
{
    const ImVec2 at = ImGui::GetIO().MousePos;
    return at.y < pos.y || at.x < pos.x || at.x > pos.x + sheet_w;
}

int draw_sheet( const sheet_spec &spec, const ImVec2 &origin, const ImVec2 &size )
{
    const float u = unit();
    const ImVec4 safe = safe_insets();
    const bool landscape = size.x > size.y;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    int result = sheet_nothing;

    // dim the screen behind; a tap there closes the sheet
    dim_screen( draw, origin, screen_max );
    // not a control to move the focus to
    ImGui::PushItemFlag( ImGuiItemFlags_NoNav, true );
    const bool outside = pressable( "##sheet_outside", origin, size );
    ImGui::PopItemFlag();

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
    sheet_area( pos, ImVec2( pos.x + sheet_w, screen_max.y ) );
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
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    if( sheet_scroll_reset ) {
        // a sheet that has just opened starts at its top, not where the last one was left
        ImGui::SetScrollY( 0.0f );
        sheet_scroll_reset = false;
    }
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

    if( outside && result == sheet_nothing && outside_sheet( pos, sheet_w ) ) {
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
    sheet_scroll_reset = true;
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
