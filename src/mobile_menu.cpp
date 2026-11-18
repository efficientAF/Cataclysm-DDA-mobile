#include "mobile_menu.h"

#include "action.h"
#include "options.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <utility>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "mobile_hud_edit.h"
#include "mobile_ui.h"
#include "sdltiles.h"
#include "translations.h"
#include "units.h"
#endif

namespace mobile_menu
{

static bool quit_after_save = false;

void request_quit_after_save()
{
    quit_after_save = true;
}

void cancel_quit_after_save()
{
    quit_after_save = false;
}

bool take_quit_after_save()
{
    const bool wanted = quit_after_save;
    quit_after_save = false;
    return wanted;
}

bool enabled()
{
#if defined(TILES)
    return get_option<bool>( "MOBILE_MENUS" );
#else
    return false;
#endif
}

#if defined(TILES)

using mobile_ui::face;

namespace
{
enum class page : int {
    title,
    new_game,
};

enum class sheet : int {
    none,
    load,
    worlds,
    // which world a new character goes to
    pick_world,
    settings,
    credits,
};
} // namespace

// The view is made anew after a screen that it would draw over (the world
// screens, settings).  These carry over what the new game screen was showing.
static std::string new_game_world;
static bool reopen_new_game = false;

class main_menu_view::impl : public cataimgui::window
{
    public:
        impl() : cataimgui::window( "MOBILE_MAIN_MENU",
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoBackground ) {
            if( reopen_new_game ) {
                current_page = page::new_game;
                reopen_new_game = false;
            }
        }

        main_menu_model model;
        page current_page = page::title;
        sheet open_sheet = sheet::none;
        std::optional<choice> picked;
        // indices into model.worlds of the worlds a new character can go to
        std::vector<int> playable_worlds() const;
        // makes new_game_world name one of them, or empty if there are none
        void settle_new_game_world();

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        void draw_title( const ImVec2 &origin, const ImVec2 &size, bool interactive );
        void draw_new_game( const ImVec2 &origin, const ImVec2 &size, bool interactive );
        void draw_sheet( const ImVec2 &origin, const ImVec2 &size );
        // A main menu button: icon, label and an optional second line.
        bool menu_button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                          const std::string &icon, const std::string &label, const std::string &detail,
                          bool primary, bool centered, bool interactive );
};

bool main_menu_view::impl::menu_button( const std::string &id, const ImVec2 &pos,
                                        const ImVec2 &size, const std::string &icon, const std::string &label,
                                        const std::string &detail, const bool primary, const bool centered,
                                        const bool interactive )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = interactive && mobile_ui::pressable( id, pos, size, &held );
    const ImVec2 max( pos.x + size.x, pos.y + size.y );
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : mobile_ui::col_button );
    mobile_ui::box( draw, pos, max, fill, primary ? 0 : mobile_ui::col_line, 14.0f );
    const ImU32 ink = primary ? mobile_ui::col_on_accent : mobile_ui::col_fg;
    const float label_size = centered ? 14.0f : 16.0f;
    const ImVec2 label_px = mobile_ui::text_size( face::bold, label_size, label );
    if( centered ) {
        mobile_ui::text( draw, face::bold, label_size,
                         ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f, pos.y + ( size.y - label_px.y ) * 0.5f ),
                         ink, label );
        return tapped;
    }
    float x = pos.x + 16.0f * u;
    if( !icon.empty() ) {
        mobile_ui::icon( draw, icon, ImVec2( x, pos.y + ( size.y - 22.0f * u ) * 0.5f ), 22.0f, ink );
        x += 34.0f * u;
    }
    if( detail.empty() ) {
        mobile_ui::text( draw, face::bold, label_size, ImVec2( x, pos.y + ( size.y - label_px.y ) * 0.5f ),
                         ink, label );
    } else {
        const ImVec2 detail_px = mobile_ui::text_size( face::body, 12.0f, detail );
        const float top = pos.y + ( size.y - label_px.y - detail_px.y - 2.0f * u ) * 0.5f;
        mobile_ui::text( draw, face::bold, label_size, ImVec2( x, top ), ink, label );
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( x, top + label_px.y + 2.0f * u ),
                         primary ? mobile_ui::col_on_accent : mobile_ui::col_muted, detail );
    }
    return tapped;
}

void main_menu_view::impl::draw_title( const ImVec2 &origin, const ImVec2 &size,
                                       const bool interactive )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const bool landscape = size.x > size.y;
    const ImVec4 safe = mobile_ui::safe_insets();
    const float left = origin.x + safe.x + 20.0f * u;
    const float right = origin.x + size.x - safe.z - 20.0f * u;
    const float top = origin.y + safe.y + 30.0f * u;
    const float bottom = origin.y + size.y - safe.w - 18.0f * u;

    // the menu: a column of buttons at the bottom, on the right in landscape
    const bool can_continue = !model.continue_title.empty();
    const float gap = 8.0f * u;
    const float menu_w = landscape ? std::min( 360.0f * u, ( right - left ) * 0.5f ) : right - left;
    const float menu_x = landscape ? right - menu_w : left;
    const float menu_h = ( can_continue ? 64.0f * u + gap : 0.0f ) + 56.0f * u + gap +
                         2.0f * 46.0f * u + gap;
    float y = std::max( top, bottom - menu_h );
    const float menu_top = y;

    // the title block
    {
        const std::string name = "CATACLYSM";
        const std::string sub = "DARK DAYS AHEAD";
        const float name_size = landscape ? 58.0f : std::min( 58.0f, ( right - left ) / u / 5.2f );
        const ImVec2 version_px = mobile_ui::text_size( face::mono, 11.0f, model.version );
        const ImVec2 name_px = mobile_ui::text_size( face::display, name_size, name );
        const ImVec2 sub_px = mobile_ui::text_size( face::display, 21.0f, sub );
        const float motd_w = std::min( 300.0f * u, menu_x - left - 20.0f * u );
        const bool show_motd = landscape && !model.motd.empty() && motd_w > 120.0f * u;
        const ImVec2 motd_px = show_motd ? mobile_ui::text_size( face::body, 13.0f, model.motd,
                               motd_w - 24.0f * u ) : ImVec2( 0.0f, 0.0f );
        const float motd_h = show_motd ? std::min( motd_px.y + 38.0f * u, 150.0f * u ) : 0.0f;
        const float block_h = version_px.y + 8.0f * u + name_px.y + 4.0f * u + sub_px.y +
                              ( show_motd ? 18.0f * u + motd_h : 0.0f );
        // centered vertically in landscape, at the top in portrait
        float ty = landscape ? std::max( top, origin.y + ( size.y - block_h ) * 0.5f ) : top;
        mobile_ui::text( draw, face::mono, 11.0f, ImVec2( left, ty ), mobile_ui::col_muted,
                         model.version );
        ty += version_px.y + 8.0f * u;
        mobile_ui::text( draw, face::display, name_size, ImVec2( left, ty ), mobile_ui::col_title, name );
        ty += name_px.y + 4.0f * u;
        mobile_ui::text( draw, face::display, 21.0f, ImVec2( left, ty ), mobile_ui::col_accent, sub );
        ty += sub_px.y;
        if( show_motd ) {
            ty += 18.0f * u;
            const ImVec2 box_max( left + motd_w, ty + motd_h );
            mobile_ui::box( draw, ImVec2( left, ty ), box_max, IM_COL32( 14, 17, 13, 204 ),
                            mobile_ui::col_line, 12.0f );
            mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left + 12.0f * u, ty + 10.0f * u ),
                             mobile_ui::col_muted, _( "MESSAGE OF THE DAY" ) );
            draw->PushClipRect( ImVec2( left, ty ), ImVec2( box_max.x, box_max.y - 8.0f * u ), true );
            mobile_ui::text( draw, face::body, 13.0f, ImVec2( left + 12.0f * u, ty + 28.0f * u ),
                             IM_COL32( 195, 201, 185, 255 ), model.motd, motd_w - 24.0f * u );
            draw->PopClipRect();
        }
    }

    y = menu_top;
    if( can_continue ) {
        if( menu_button( "##continue", ImVec2( menu_x, y ), ImVec2( menu_w, 64.0f * u ), "play",
                         _( "Continue" ), model.continue_detail, true, false, interactive ) ) {
            picked = choice{ choice_kind::continue_game, 0 };
        }
        y += 64.0f * u + gap;
    }
    if( menu_button( "##newgame", ImVec2( menu_x, y ), ImVec2( menu_w, 56.0f * u ), "user",
                     _( "New game" ), "", !can_continue, false, interactive ) ) {
        current_page = page::new_game;
    }
    y += 56.0f * u + gap;

    // four columns, two rows; Quit takes the last two cells
    const std::array<std::string, 7> labels = {
        _( "Load" ), _( "World" ), _( "Tutorial" ), _( "Settings" ), _( "Help" ), _( "Credits" ),
        _( "Quit game" )
    };
    const float cell_w = ( menu_w - 3.0f * gap ) / 4.0f;
    for( int i = 0; i < 7; ++i ) {
        const ImVec2 pos( menu_x + static_cast<float>( i % 4 ) * ( cell_w + gap ),
                          y + static_cast<float>( i / 4 ) * ( 46.0f * u + gap ) );
        const float w = i == 6 ? 2.0f * cell_w + gap : cell_w;
        if( !menu_button( "##row" + std::to_string( i ), pos, ImVec2( w, 46.0f * u ), "", labels[i],
                          "", false, true, interactive ) ) {
            continue;
        }
        switch( i ) {
            case 0:
                open_sheet = sheet::load;
                break;
            case 1:
                open_sheet = sheet::worlds;
                break;
            case 2:
                picked = choice{ choice_kind::tutorial, 0 };
                break;
            case 3:
                open_sheet = sheet::settings;
                break;
            case 4:
                picked = choice{ choice_kind::help, 0 };
                break;
            case 5:
                open_sheet = sheet::credits;
                break;
            default:
                picked = choice{ choice_kind::quit, 0 };
                break;
        }
    }
}

std::vector<int> main_menu_view::impl::playable_worlds() const
{
    std::vector<int> ret;
    for( int i = 0; i < static_cast<int>( model.worlds.size() ); ++i ) {
        // as worldfactory::pick_world
        if( model.worlds[i].name != "TUTORIAL" && model.worlds[i].name != "DEFENSE" ) {
            ret.push_back( i );
        }
    }
    return ret;
}

void main_menu_view::impl::settle_new_game_world()
{
    const std::vector<int> playable = playable_worlds();
    const auto has = [&]( const std::string & name ) {
        return !name.empty() && std::any_of( playable.begin(), playable.end(), [&]( const int i ) {
            return model.worlds[i].name == name;
        } );
    };
    if( has( new_game_world ) ) {
        return;
    }
    // a world without characters if there is one, as upstream's "play now"
    // does; the one played last comes first
    const auto empty = std::find_if( playable.begin(), playable.end(), [&]( const int i ) {
        return model.worlds[i].characters == 0;
    } );
    const auto last = std::find_if( playable.begin(), playable.end(), [&]( const int i ) {
        return model.worlds[i].name == model.last_world;
    } );
    if( last != playable.end() && model.worlds[*last].characters == 0 ) {
        new_game_world = model.last_world;
    } else if( empty != playable.end() ) {
        new_game_world = model.worlds[*empty].name;
    } else if( last != playable.end() ) {
        new_game_world = model.last_world;
    } else if( !playable.empty() ) {
        new_game_world = model.worlds[playable.front()].name;
    } else {
        new_game_world.clear();
    }
}

void main_menu_view::impl::draw_new_game( const ImVec2 &origin, const ImVec2 &size,
        const bool interactive )
{
    struct option {
        const char *icon;
        std::string title;
        std::string text;
    };
    const std::array<option, 5> options = { {
            { "user", _( "Custom character" ), _( "Choose your scenario, profession, stats, traits and skills." ) },
            { "folder", _( "Preset character" ), _( "Start from a character template you saved before." ) },
            { "shuffle", _( "Random character" ), _( "Roll a random character, then review and change anything before you start." ) },
            { "play", _( "Play now (default scenario)" ), _( "Jump straight in with a random character.  The scenario is the default one." ) },
            { "bolt", _( "Play now (random scenario)" ), _( "Jump straight in with a random character and scenario." ) }
        }
    };

    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    const bool landscape = size.x > size.y;
    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    float bar_h = 0.0f;
    ImGui::BeginDisabled( !interactive );
    if( mobile_ui::app_bar( _( "New game" ), ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        current_page = page::title;
    }
    ImGui::EndDisabled();

    // everything below the bar scrolls
    const ImVec2 scroll_pos( left, origin.y + safe.y + bar_h );
    const ImVec2 scroll_size( width, size.y - safe.y - safe.w - bar_h );
    ImGui::SetCursorScreenPos( scroll_pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##newgame_scroll", scroll_size, ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float pad = 12.0f * u;
    float y = base.y;

    const auto section = [&]( const std::string & label ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( base.x + 16.0f * u, y + 16.0f * u ),
                         mobile_ui::col_muted, label );
        y += 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, label ).y + 6.0f * u;
    };

    section( _( "WORLD" ) );
    {
        const ImVec2 pos( base.x + pad, y );
        const ImVec2 card( width - 2.0f * pad, 62.0f * u );
        bool held = false;
        const bool tapped = interactive && mobile_ui::pressable( "##world", pos, card, &held );
        mobile_ui::box( draw, pos, ImVec2( pos.x + card.x, pos.y + card.y ),
                        held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 14.0f );
        mobile_ui::icon( draw, "globe", ImVec2( pos.x + 14.0f * u, pos.y + 19.0f * u ), 24.0f,
                         mobile_ui::col_accent );
        settle_new_game_world();
        const bool has_world = !new_game_world.empty();
        std::string name = _( "A new world" );
        std::string detail = _( "Created with the default settings when you start" );
        for( const world_info &w : model.worlds ) {
            if( has_world && w.name == new_game_world ) {
                name = w.name;
                detail = w.detail;
            }
        }
        mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + 50.0f * u, pos.y + 12.0f * u ),
                         mobile_ui::col_fg, name );
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( pos.x + 50.0f * u, pos.y + 33.0f * u ),
                         mobile_ui::col_muted, detail );
        const std::string link = _( "Change" );
        const ImVec2 link_px = mobile_ui::text_size( face::bold, 13.0f, link );
        mobile_ui::text( draw, face::bold, 13.0f,
                         ImVec2( pos.x + card.x - 14.0f * u - link_px.x, pos.y + ( card.y - link_px.y ) * 0.5f ),
                         mobile_ui::col_accent, link );
        if( tapped ) {
            open_sheet = has_world ? sheet::pick_world : sheet::worlds;
        }
        y += card.y;
    }

    section( _( "HOW DO YOU WANT TO START?" ) );
    y += 4.0f * u;
    {
        const int columns = landscape ? 2 : 1;
        const float gap = 8.0f * u;
        const float card_w = ( width - 2.0f * pad - static_cast<float>( columns - 1 ) * gap ) /
                             static_cast<float>( columns );
        const float text_w = card_w - 14.0f * u - 38.0f * u - 12.0f * u - 14.0f * u;
        float row_h = 0.0f;
        for( int i = 0; i < static_cast<int>( options.size() ); ++i ) {
            const option &opt = options[i];
            const int col = i % columns;
            if( col == 0 ) {
                // cards in a row share the height of the taller one
                row_h = 0.0f;
                for( int j = i; j < std::min( i + columns, static_cast<int>( options.size() ) ); ++j ) {
                    const float text_h = mobile_ui::text_size( face::body, 13.0f, options[j].text, text_w ).y;
                    row_h = std::max( row_h, 12.0f * u + 20.0f * u + text_h + 12.0f * u );
                }
                row_h = std::max( row_h, 62.0f * u );
            }
            const ImVec2 pos( base.x + pad + static_cast<float>( col ) * ( card_w + gap ), y );
            bool held = false;
            const bool tapped = interactive &&
                                mobile_ui::pressable( "##start" + std::to_string( i ), pos, ImVec2( card_w, row_h ), &held );
            mobile_ui::box( draw, pos, ImVec2( pos.x + card_w, pos.y + row_h ),
                            held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 8 ), mobile_ui::col_line, 14.0f );
            const ImVec2 icon_pos( pos.x + 14.0f * u, pos.y + 12.0f * u );
            mobile_ui::box( draw, icon_pos, ImVec2( icon_pos.x + 38.0f * u, icon_pos.y + 38.0f * u ),
                            mobile_ui::col_accent_soft, 0, 10.0f );
            mobile_ui::icon( draw, opt.icon, ImVec2( icon_pos.x + 9.0f * u, icon_pos.y + 9.0f * u ), 20.0f,
                             mobile_ui::col_accent );
            const float text_x = icon_pos.x + 38.0f * u + 12.0f * u;
            mobile_ui::text( draw, face::bold, 15.0f, ImVec2( text_x, pos.y + 11.0f * u ),
                             mobile_ui::col_fg, opt.title );
            mobile_ui::text( draw, face::body, 13.0f, ImVec2( text_x, pos.y + 32.0f * u ),
                             mobile_ui::col_text_soft, opt.text, text_w );
            if( tapped ) {
                picked = choice{ choice_kind::new_game, i, new_game_world };
            }
            if( col == columns - 1 || i + 1 == static_cast<int>( options.size() ) ) {
                y += row_h + gap;
            }
        }
    }
    y += 16.0f * u;
    // tell ImGui how tall the content is, so it can scroll
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void main_menu_view::impl::draw_sheet( const ImVec2 &origin, const ImVec2 &size )
{
    mobile_ui::sheet_spec spec;
    std::vector<choice> results;
    switch( open_sheet ) {
        case sheet::load:
            spec.title = _( "Load game" );
            for( int i = 0; i < static_cast<int>( model.worlds.size() ); ++i ) {
                spec.rows.push_back( { "folder", model.worlds[i].name, model.worlds[i].detail } );
                results.push_back( { choice_kind::load_world, i } );
            }
            if( spec.rows.empty() ) {
                spec.description = _( "There are no saved games yet." );
            }
            break;
        case sheet::worlds:
            spec.title = _( "Worlds" );
            for( int i = 0; i < static_cast<int>( model.worlds.size() ); ++i ) {
                spec.rows.push_back( { "globe", model.worlds[i].name, model.worlds[i].detail } );
                results.push_back( { choice_kind::edit_world, i } );
            }
            spec.rows.push_back( { "plus", _( "Create a world" ), _( "Choose mods and world options" ) } );
            results.push_back( { choice_kind::edit_world, -1 } );
            break;
        case sheet::pick_world:
            spec.title = _( "World for the new character" );
            for( const int i : playable_worlds() ) {
                const bool current = model.worlds[i].name == new_game_world;
                spec.rows.push_back( { current ? "check" : "globe", model.worlds[i].name, model.worlds[i].detail } );
                // not a choice for main_menu.cpp: handled below
                results.push_back( { choice_kind::new_game, -1, model.worlds[i].name } );
            }
            spec.rows.push_back( { "gear", _( "Manage worlds" ), _( "Create a world, or change or delete one" ) } );
            results.push_back( { choice_kind::edit_world, -2 } );
            break;
        case sheet::settings:
            spec.title = _( "Settings" );
            for( int i = 0; i < static_cast<int>( model.settings.size() ); ++i ) {
                // keybindings, colors and the ImGui style and demo screens are
                // for a keyboard and a mouse: not on the phone's list
                if( i == 1 || i >= 4 ) {
                    continue;
                }
                spec.rows.push_back( { "gear", model.settings[i], "" } );
                results.push_back( { choice_kind::settings, i } );
            }
            break;
        case sheet::credits:
            spec.title = _( "Credits" );
            spec.description = model.credits;
            break;
        case sheet::none:
            return;
    }
    const int tapped = mobile_ui::draw_sheet( spec, origin, size );
    if( tapped == mobile_ui::sheet_dismissed ) {
        open_sheet = sheet::none;
    } else if( tapped >= 0 && tapped < static_cast<int>( results.size() ) ) {
        if( open_sheet == sheet::pick_world ) {
            if( results[tapped].kind == choice_kind::new_game ) {
                new_game_world = results[tapped].world;
                open_sheet = sheet::none;
            } else {
                open_sheet = sheet::worlds;
            }
            return;
        }
        picked = results[tapped];
        // back to the new game screen after managing worlds from there
        reopen_new_game = open_sheet == sheet::worlds && current_page == page::new_game;
        open_sheet = sheet::none;
    }
}

void main_menu_view::impl::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         current_page == page::title ? mobile_ui::col_bg : mobile_ui::col_screen );
    draw->PopClipRect();

    const bool interactive = open_sheet == sheet::none;
    if( current_page == page::title ) {
        draw_title( origin, size, interactive );
    } else {
        draw_new_game( origin, size, interactive );
    }
    if( open_sheet != sheet::none ) {
        // in a layer of its own: the new game screen's scrolling area would
        // otherwise be drawn over the sheet
        ImGui::SetCursorScreenPos( origin );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ImGui::BeginChild( "##sheet_layer", size, ImGuiChildFlags_NavFlattened,
                           ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse );
        draw_sheet( origin, size );
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}

action_id pause_sheet()
{
    mobile_ui::sheet_spec spec;
    spec.title = _( "Paused" );
    const std::vector<std::pair<mobile_ui::sheet_row, action_id>> rows = {
        { { "play", _( "Resume" ), "" }, ACTION_NULL },
        { { "person", _( "Character" ), "" }, ACTION_PL_INFO },
        { { "inventory", _( "Inventory" ), "" }, ACTION_INVENTORY },
        { { "craft", _( "Craft" ), "" }, ACTION_CRAFT },
        { { "globe", _( "Map" ), "" }, ACTION_MAP },
        { { "lines", _( "Messages" ), "" }, ACTION_MESSAGES },
        { { "gear", _( "Settings" ), "" }, ACTION_OPTIONS },
        { { "joy", _( "Edit HUD" ), "" }, NUM_ACTIONS },
        { { "menu", _( "All actions" ), "" }, ACTION_ACTIONMENU },
        { { "keyboard", _( "Keyboard" ), "" }, NUM_ACTIONS },
        { { "save", _( "Save game" ), "" }, ACTION_QUICKSAVE },
        { { "exit", _( "Save and quit to menu" ), "" }, ACTION_SAVE },
        { { "close", _( "Save and quit game" ), "" }, ACTION_SAVE },
    };
    cancel_quit_after_save();
    for( const std::pair<mobile_ui::sheet_row, action_id> &row : rows ) {
        spec.rows.push_back( row.first );
    }
    const int tapped = mobile_ui::show_sheet( spec );
    if( tapped >= 0 && rows[tapped].second == NUM_ACTIONS ) {
        if( rows[tapped].first.icon == "keyboard" ) {
            toggle_soft_keyboard();
        } else {
            mobile_hud_edit::run();
        }
        return ACTION_NULL;
    }
    if( tapped >= 0 && rows[tapped].first.icon == "close" ) {
        // the last row: the main menu closes the program once the game is saved
        request_quit_after_save();
    }
    return tapped >= 0 ? rows[tapped].second : ACTION_NULL;
}

void wait_sheet()
{
    mobile_ui::sheet_spec spec;
    spec.title = _( "Wait" );
    spec.description = _( "Waiting stops early if a monster comes into view." );
    spec.grid = true;
    const time_point dawn = sunrise( calendar::turn ) > calendar::turn ? sunrise( calendar::turn ) :
                            sunrise( calendar::turn + 1_days );
    const std::vector<std::pair<std::string, time_duration>> options = {
        { _( "Wait 5 minutes" ), 5_minutes },
        { _( "Wait 30 minutes" ), 30_minutes },
        { _( "Wait 1 hour" ), 1_hours },
        { _( "Wait 3 hours" ), 3_hours },
        { _( "Wait until dawn" ), dawn - calendar::turn },
        { _( "Wait until something happens" ), 12_hours },
    };
    for( const std::pair<std::string, time_duration> &option : options ) {
        spec.rows.push_back( { "", option.first, "" } );
    }
    const int tapped = mobile_ui::show_sheet( spec );
    if( tapped >= 0 ) {
        get_avatar().assign_activity( wait_activity_actor( options[tapped].second ) );
    }
}

main_menu_view::main_menu_view() : pimpl( std::make_unique<impl>() ) {}

main_menu_view::~main_menu_view() = default;

void main_menu_view::set_model( const main_menu_model &model )
{
    pimpl->model = model;
}

std::optional<choice> main_menu_view::take_choice()
{
    std::optional<choice> ret = pimpl->picked;
    pimpl->picked.reset();
    return ret;
}

bool main_menu_view::back()
{
    if( pimpl->open_sheet != sheet::none ) {
        pimpl->open_sheet = sheet::none;
        return true;
    }
    if( pimpl->current_page != page::title ) {
        pimpl->current_page = page::title;
        return true;
    }
    return false;
}

#else // !TILES

action_id pause_sheet()
{
    return ACTION_ACTIONMENU;
}

void wait_sheet() {}

class main_menu_view::impl
{
};

main_menu_view::main_menu_view() = default;

main_menu_view::~main_menu_view() = default;

void main_menu_view::set_model( const main_menu_model & ) {}

std::optional<choice> main_menu_view::take_choice()
{
    return std::nullopt;
}

bool main_menu_view::back()
{
    return false;
}

#endif // TILES

} // namespace mobile_menu
