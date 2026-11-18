#include "mobile_world.h"

#include "mobile_menu.h"

#if defined(TILES)
#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "cata_imgui.h"
#include "cata_utility.h"
#include "dependency_tree.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "mobile_entry.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "mod_manager.h"
#include "options.h"
#include "output.h"
#include "popup.h"
#include "string_formatter.h"
#include "translation.h"
#include "translations.h"
#include "ui_manager.h"
#include "worldfactory.h"
#endif

namespace mobile_world
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

constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_warn = IM_COL32( 232, 120, 104, 255 );
constexpr ImGuiWindowFlags screen_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

ImU32 faded( const ImU32 color, const float opacity )
{
    const ImU32 alpha = static_cast<ImU32>( static_cast<float>( ( color >> IM_COL32_A_SHIFT ) & 0xFF ) *
                                            opacity );
    return ( color & ~IM_COL32_A_MASK ) | ( alpha << IM_COL32_A_SHIFT );
}

enum class button_look : int {
    plain,
    primary,
};

// A button with its label in the middle.  A disabled one is drawn faint and
// takes no taps.
bool button( ImDrawList *const draw, const std::string &id, const ImVec2 &pos, const ImVec2 &size,
             const std::string &label, const button_look look, const bool enabled = true )
{
    const float u = mobile_ui::unit();
    bool held = false;
    const bool tapped = enabled && mobile_ui::pressable( id, pos, size, &held );
    const ImVec2 max( pos.x + size.x, pos.y + size.y );
    const float opacity = enabled ? 1.0f : 0.4f;
    const bool primary = look == button_look::primary;
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft );
    mobile_ui::box( draw, pos, max, faded( fill, opacity ),
                    primary ? 0 : faded( mobile_ui::col_line, opacity ), 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 14.0f, label );
    draw->PushClipRect( pos, max, true );
    mobile_ui::text( draw, face::bold, 14.0f,
                     ImVec2( pos.x + std::max( 4.0f * u, ( size.x - label_px.x ) * 0.5f ),
                             pos.y + ( size.y - label_px.y ) * 0.5f ),
                     faded( primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, opacity ), label );
    draw->PopClipRect();
    return tapped;
}

void fill_screen( const ImVec2 &origin, const ImVec2 &size )
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();
}

// ---------------------------------------------------------------------------
// New world
// ---------------------------------------------------------------------------

enum class world_act : int {
    none,
    back,
    rename,
    random_name,
    lower,
    raise,
    reset,
    randomize,
    mods,
    options,
    create,
};

class world_window : public cataimgui::window
{
    public:
        world_window( const WORLD &world, const std::vector<option_slider_id> &sliders,
                      const std::vector<int> &levels, const bool &custom ) :
            cataimgui::window( "MOBILE_WORLD", screen_flags ), world( world ), sliders( sliders ),
            levels( levels ), custom( custom ) {}

        // what was tapped since the last call
        world_act take() {
            return std::exchange( act, world_act::none );
        }
        // the slider that lower and raise are about
        int slider = 0;
        // where the list was scrolled to, kept while another screen is open
        float scroll = 0.0f;
        bool restore_scroll = false;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        const WORLD &world;
        const std::vector<option_slider_id> &sliders;
        const std::vector<int> &levels;
        const bool &custom;
        world_act act = world_act::none;

        // the scrolling list being drawn
        ImDrawList *draw = nullptr;
        float left = 0.0f;
        float width = 0.0f;
        float y = 0.0f;

        void section( const std::string &label );
        void row_end( float row_h );
        void name_row();
        void slider_row( int index );
        bool link_row( const std::string &id, const std::string &title, const std::string &sub );
};

void world_window::section( const std::string &label )
{
    const float u = mobile_ui::unit();
    mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left + 16.0f * u, y + 16.0f * u ),
                     mobile_ui::col_muted, label );
    y += 16.0f * u + mobile_ui::text_size( face::mono, 10.5f, label ).y + 6.0f * u;
}

void world_window::row_end( const float row_h )
{
    y += row_h;
    draw->AddLine( ImVec2( left, y ), ImVec2( left + width, y ), col_row_line,
                   std::max( 1.0f, mobile_ui::unit() ) );
}

// The name, which opens the text card, and "Random name".
void world_window::name_row()
{
    const float u = mobile_ui::unit();
    const std::string label = _( "Random name" );
    const float button_w = mobile_ui::text_size( face::bold, 14.0f, label ).x + 28.0f * u;
    const float row_h = 68.0f * u;
    const ImVec2 field( left + 16.0f * u, y + 10.0f * u );
    const ImVec2 field_size( width - 32.0f * u - button_w - 8.0f * u, 48.0f * u );
    bool held = false;
    if( mobile_ui::pressable( "##world_name", field, field_size, &held ) ) {
        act = world_act::rename;
    }
    mobile_ui::box( draw, field, ImVec2( field.x + field_size.x, field.y + field_size.y ),
                    held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 12.0f );
    const bool unnamed = world.world_name.empty();
    const std::string shown = unnamed ? _( "Tap to name the world" ) : world.world_name;
    const ImVec2 shown_px = mobile_ui::text_size( face::bold, 16.0f, shown );
    draw->PushClipRect( field, ImVec2( field.x + field_size.x - 8.0f * u, field.y + field_size.y ),
                        true );
    mobile_ui::text( draw, face::bold, 16.0f, ImVec2( field.x + 14.0f * u,
                     field.y + ( field_size.y - shown_px.y ) * 0.5f ),
                     unnamed ? mobile_ui::col_muted : mobile_ui::col_title, shown );
    draw->PopClipRect();
    if( button( draw, "##world_random_name", ImVec2( left + width - 16.0f * u - button_w, field.y ),
                ImVec2( button_w, 48.0f * u ), label, button_look::plain ) ) {
        act = world_act::random_name;
    }
    row_end( row_h );
}

// One of upstream's world option sliders: its name, a stepper showing the name
// of the level, and what the level means.
void world_window::slider_row( const int index )
{
    const float u = mobile_ui::unit();
    const option_slider &osl = *sliders[static_cast<size_t>( index )];
    const int level = levels[static_cast<size_t>( index )];
    const std::string id = "##world_slider" + std::to_string( index );
    const float text_w = width - 32.0f * u;
    const std::string title = osl.name().translated();
    float row_y = y + 12.0f * u;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, row_y ), mobile_ui::col_fg,
                     title, text_w );
    row_y += mobile_ui::text_size( face::bold, 14.5f, title, text_w ).y + 8.0f * u;

    // minus, the level, plus
    const float step = 48.0f * u;
    const float bar_w = std::min( text_w, 380.0f * u );
    const ImVec2 bar( left + 16.0f * u, row_y );
    mobile_ui::box( draw, bar, ImVec2( bar.x + bar_w, bar.y + step ), mobile_ui::col_fill_soft,
                    mobile_ui::col_line, 12.0f );
    const auto step_button = [&]( const char *suffix, const float x, const char *icon,
    const bool enabled, const world_act what ) {
        bool held = false;
        if( enabled && mobile_ui::pressable( id + suffix, ImVec2( x, bar.y ), ImVec2( step, step ),
                                             &held ) ) {
            act = what;
            slider = index;
        }
        if( held ) {
            mobile_ui::box( draw, ImVec2( x, bar.y ), ImVec2( x + step, bar.y + step ),
                            mobile_ui::col_button_down, 0, 12.0f );
        }
        mobile_ui::icon( draw, icon, ImVec2( x + 14.0f * u, bar.y + 14.0f * u ), 20.0f,
                         faded( mobile_ui::col_fg, enabled ? 1.0f : 0.3f ) );
    };
    // with customized options the buttons ask whether to go back to the sliders
    step_button( "_minus", bar.x, "minus", custom || level > 0, world_act::lower );
    step_button( "_plus", bar.x + bar_w - step, "plus", custom || level < osl.count() - 1,
                 world_act::raise );
    const std::string value = custom ? _( "Custom" ) : osl.level_name( level ).translated();
    const ImVec2 value_px = mobile_ui::text_size( face::bold, 15.0f, value );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( bar.x + ( bar_w - value_px.x ) * 0.5f,
                     bar.y + ( step - value_px.y ) * 0.5f - ( custom ? 0.0f : 5.0f * u ) ),
                     mobile_ui::col_accent, value );
    if( !custom && osl.count() > 1 ) {
        // a dot for each level under the name
        const float dot_gap = 9.0f * u;
        const float dots_w = dot_gap * static_cast<float>( osl.count() - 1 );
        for( int i = 0; i < osl.count(); ++i ) {
            draw->AddCircleFilled( ImVec2( bar.x + ( bar_w - dots_w ) * 0.5f + dot_gap * static_cast<float>
                                           ( i ), bar.y + step - 9.0f * u ), i == level ? 3.0f * u : 2.0f * u,
                                   i == level ? mobile_ui::col_accent : mobile_ui::col_muted, 12 );
        }
    }
    row_y += step + 8.0f * u;
    const std::string desc = custom ?
                             _( "The options of this world were changed one by one under \"All world options\"." ) :
                             osl.level_desc( level ).translated();
    if( !desc.empty() ) {
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( left + 16.0f * u, row_y ),
                         mobile_ui::col_muted, desc, text_w );
        row_y += mobile_ui::text_size( face::body, 12.5f, desc, text_w ).y;
    }
    row_end( row_y + 12.0f * u - y );
}

bool world_window::link_row( const std::string &id, const std::string &title,
                             const std::string &sub )
{
    const float u = mobile_ui::unit();
    const ImVec2 pos( left, y );
    const float text_w = std::max( 80.0f * u, width - 64.0f * u );
    const ImVec2 title_px = mobile_ui::text_size( face::bold, 14.5f, title, text_w );
    const float sub_h = sub.empty() ? 0.0f : 2.0f * u +
                        mobile_ui::text_size( face::body, 12.0f, sub, text_w ).y;
    const float text_h = title_px.y + sub_h;
    const float row_h = std::max( 60.0f * u, text_h + 24.0f * u );
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, ImVec2( width, row_h ), &held );
    if( held ) {
        draw->AddRectFilled( pos, ImVec2( pos.x + width, pos.y + row_h ), mobile_ui::col_button_down );
    }
    const float top = y + ( row_h - text_h ) * 0.5f;
    mobile_ui::text( draw, face::bold, 14.5f, ImVec2( left + 16.0f * u, top ), mobile_ui::col_fg,
                     title, text_w );
    if( !sub.empty() ) {
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( left + 16.0f * u, top + title_px.y + 2.0f * u ),
                         mobile_ui::col_muted, sub, text_w );
    }
    mobile_ui::icon( draw, "chev", ImVec2( left + width - 16.0f * u - 20.0f * u,
                                           y + ( row_h - 20.0f * u ) * 0.5f ), 20.0f, mobile_ui::col_muted );
    row_end( row_h );
    return tapped;
}

void world_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    fill_screen( origin, size );
    ImDrawList *const screen = ImGui::GetWindowDrawList();

    const float bar_left = origin.x + safe.x;
    const float bar_width = size.x - safe.x - safe.z;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "New world" ), ImVec2( bar_left, origin.y + safe.y ), bar_width,
                            &bar_h ) ) {
        act = world_act::back;
    }

    // "Create world" stays at the bottom
    const float foot_h = 64.0f * u;
    const float foot_y = origin.y + size.y - safe.w - foot_h;
    screen->AddLine( ImVec2( bar_left, foot_y ), ImVec2( bar_left + bar_width, foot_y ),
                     mobile_ui::col_line, std::max( 1.0f, u ) );
    const float create_w = std::min( bar_width - 24.0f * u, 420.0f * u );
    if( button( screen, "##world_create", ImVec2( bar_left + ( bar_width - create_w ) * 0.5f,
                foot_y + 8.0f * u ), ImVec2( create_w, 48.0f * u ), _( "Create world" ),
                button_look::primary ) ) {
        act = world_act::create;
    }

    const float list_top = origin.y + safe.y + bar_h;
    ImGui::SetCursorScreenPos( ImVec2( bar_left, list_top ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##world_scroll", ImVec2( bar_width, foot_y - list_top ), ImGuiChildFlags_NavFlattened,
                       ImGuiWindowFlags_NoBackground );
    if( restore_scroll ) {
        ImGui::SetScrollY( scroll );
        restore_scroll = false;
    } else {
        scroll = ImGui::GetScrollY();
    }
    draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    left = base.x;
    width = bar_width;
    y = base.y;

    section( _( "WORLD NAME" ) );
    name_row();

    if( !sliders.empty() ) {
        section( _( "WORLD SETTINGS" ) );
        for( int i = 0; i < static_cast<int>( sliders.size() ); ++i ) {
            slider_row( i );
        }
    }
    {
        // upstream's Reset and Randomize
        const float gap = 8.0f * u;
        const float half = std::min( ( width - 32.0f * u - gap ) * 0.5f, 186.0f * u );
        const float top = y + 10.0f * u;
        if( button( draw, "##world_reset", ImVec2( left + 16.0f * u, top ), ImVec2( half, 46.0f * u ),
                    _( "Reset to defaults" ), button_look::plain ) ) {
            act = world_act::reset;
        }
        if( !sliders.empty() &&
            button( draw, "##world_randomize", ImVec2( left + 16.0f * u + half + gap, top ),
                    ImVec2( half, 46.0f * u ), _( "Randomize" ), button_look::plain ) ) {
            act = world_act::randomize;
        }
        row_end( 66.0f * u );
    }

    section( _( "MORE" ) );
    const int mod_count = static_cast<int>( world.active_mod_order.size() );
    if( link_row( "##world_mods", _( "Mods" ),
                  string_format( n_gettext( "%d mod active", "%d mods active", mod_count ), mod_count ) ) ) {
        act = world_act::mods;
    }
    if( link_row( "##world_options", _( "All world options" ),
                  custom ? _( "Customized" ) : _( "Every option of this world, one by one" ) ) ) {
        act = world_act::options;
    }

    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, y - base.y + 16.0f * u ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------

enum class mods_act : int {
    none,
    back,
    add,
    remove,
    move_up,
    move_down,
    save_default,
    search,
    clear_search,
};

// A mod on the Available tab, with the texts of its row.
struct mod_entry {
    mod_id id;
    // index into get_mod_list_tabs(), as upstream sorts categories onto tabs
    int chip = 0;
    std::string name;
    std::string detail;
    std::string category;
};

std::string mod_name( const mod_id &id )
{
    return id.is_valid() ? id->name() : id.str();
}

std::string mod_category( const mod_id &id )
{
    if( !id.is_valid() ) {
        return _( "MISSING MODS" );
    }
    return id->obsolete ? _( "OBSOLETE MODS" ) : id->category.second.translated();
}

// The authors, then the start of the description.
std::string mod_detail( const mod_id &id )
{
    if( !id.is_valid() ) {
        return _( "This mod is not installed." );
    }
    std::string detail = enumerate_as_string( id->authors, enumeration_conjunction::none );
    std::string desc = id->description.translated();
    std::replace( desc.begin(), desc.end(), '\n', ' ' );
    if( !detail.empty() && !desc.empty() ) {
        detail += " · ";
    }
    return detail + remove_color_tags( desc );
}

class mods_window : public cataimgui::window
{
    public:
        mods_window( const std::vector<mod_id> &active, mod_ui &ui, const bool read_only ) :
            cataimgui::window( "MOBILE_WORLD_MODS", screen_flags ), read_only( read_only ),
            active( active ), ui( ui ) {
            const std::vector<std::pair<std::string, translation>> &tabs = get_mod_list_tabs();
            const std::map<std::string, std::string> &cat_tab = get_mod_list_cat_tab();
            // which of upstream's tabs each mod is on, as show_worldgen_tab_modselection has it
            for( const mod_id &mod : ui.active_manager.get_usable_mods() ) {
                mod_entry entry;
                entry.id = mod;
                std::string dest_tab = "tab_default";
                const int cat_idx = mod->category.first;
                if( cat_idx >= 0 && static_cast<size_t>( cat_idx ) < get_mod_list_categories().size() ) {
                    const auto iter = cat_tab.find( get_mod_list_categories()[static_cast<size_t>( cat_idx )].first );
                    if( iter != cat_tab.end() ) {
                        dest_tab = iter->second;
                    }
                }
                for( size_t i = 0; i < tabs.size(); ++i ) {
                    if( tabs[i].first == dest_tab ) {
                        entry.chip = static_cast<int>( i );
                    }
                }
                entry.name = mod_name( mod );
                entry.detail = mod_detail( mod );
                entry.category = mod_category( mod );
                available.push_back( std::move( entry ) );
            }
        }

        mods_act take() {
            return std::exchange( act, mods_act::none );
        }
        // true if a row is open in the panel; clears it
        bool close_panel() {
            return std::exchange( has_selected, false );
        }
        void select( const mod_id &id ) {
            selected = id;
            has_selected = true;
            reveal = true;
        }
        const mod_id &selection() const {
            return selected;
        }
        // 0 Active, 1 Available
        int tab = 0;
        std::string filter;
        const bool read_only;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        const std::vector<mod_id> &active;
        mod_ui &ui;
        std::vector<mod_entry> available;
        mods_act act = mods_act::none;
        // 0 all, 1 and up upstream's tabs
        int chip = 0;
        mod_id selected;
        bool has_selected = false;
        // scroll the list to the selected row
        bool reveal = false;

        bool is_active( const mod_id &id ) const {
            return std::find( active.begin(), active.end(), id ) != active.end();
        }
        // why the mod cannot be added, or empty
        std::string blocked( const mod_id &id ) const;
        float draw_tabs( const ImVec2 &pos, float right, bool align_right );
        float draw_filters( const ImVec2 &pos, float width, bool one_row );
        // returns the height drawn; a row is as tall for every mod
        bool draw_row( ImDrawList *list, const std::string &id, const ImVec2 &pos, float width,
                       const std::string &lead, const std::string &name, const std::string &detail,
                       const std::string &right, ImU32 right_color, bool on, bool dim );
        void draw_active( float list_w );
        void draw_available( float list_w );
        void draw_panel( const ImVec2 &min, const ImVec2 &max );
};

std::string mods_window::blocked( const mod_id &id ) const
{
    if( !id.is_valid() ) {
        return _( "This mod is not installed." );
    }
    if( const std::optional<mod_id> conflict = ui.find_mod_conflict( id, active ) ) {
        return string_format( _( "Cannot be added: it conflicts with %s." ), mod_name( *conflict ) );
    }
    dependency_node *const node = ui.mm_tree.get_node( id );
    if( node == nullptr ) {
        return _( "Cannot be added: the mod manager does not know this mod." );
    }
    if( node->has_errors() ) {
        return string_format( _( "Cannot be added: %s" ), remove_color_tags( node->s_errors() ) );
    }
    if( id->obsolete ) {
        return _( "Obsolete: kept only so that old saves still load." );
    }
    return std::string();
}

float mods_window::draw_tabs( const ImVec2 &pos, const float right, const bool align_right )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const std::vector<std::string> labels = {
        string_format( "%s  %d", _( "Active" ), static_cast<int>( active.size() ) ),
        string_format( "%s  %d", _( "Available" ), static_cast<int>( available.size() ) )
    };
    std::vector<float> widths;
    float total = 0.0f;
    for( const std::string &label : labels ) {
        widths.push_back( mobile_ui::text_size( face::bold, 13.0f, label ).x + 26.0f * u );
        total += widths.back() + 6.0f * u;
    }
    float x = align_right ? right - total : pos.x;
    for( size_t i = 0; i < labels.size(); ++i ) {
        const bool on = tab == static_cast<int>( i );
        // the pill is 36 units tall; the area that takes the tap is 44
        if( mobile_ui::pressable( "##mods_tab" + std::to_string( i ), ImVec2( x, pos.y - 4.0f * u ),
                                  ImVec2( widths[i], 44.0f * u ) ) && !on ) {
            tab = static_cast<int>( i );
            has_selected = false;
        }
        mobile_ui::box( draw, ImVec2( x, pos.y ), ImVec2( x + widths[i], pos.y + 36.0f * u ),
                        on ? mobile_ui::col_accent : 0, on ? 0 : mobile_ui::col_line, 18.0f );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( x + 13.0f * u, pos.y + 10.0f * u ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, labels[i] );
        x += widths[i] + 6.0f * u;
    }
    return 36.0f * u;
}

// The search field and upstream's mod tabs as chips.
float mods_window::draw_filters( const ImVec2 &pos, const float width, const bool one_row )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float field_w = one_row ? std::min( 260.0f * u, width * 0.4f ) : width;
    const float field_h = 40.0f * u;
    bool held = false;
    const bool has_filter = !filter.empty();
    const float clear_w = has_filter ? 40.0f * u : 0.0f;
    if( mobile_ui::pressable( "##mods_search", pos, ImVec2( field_w - clear_w, field_h ), &held ) ) {
        act = mods_act::search;
    }
    mobile_ui::box( draw, pos, ImVec2( pos.x + field_w, pos.y + field_h ),
                    held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft,
                    has_filter ? mobile_ui::col_accent : mobile_ui::col_line, 12.0f );
    mobile_ui::icon( draw, "search", ImVec2( pos.x + 11.0f * u, pos.y + 11.0f * u ), 18.0f,
                     mobile_ui::col_muted );
    const std::string shown = has_filter ? filter : std::string( _( "Search mods" ) );
    const ImVec2 shown_px = mobile_ui::text_size( face::body, 14.0f, shown );
    draw->PushClipRect( pos, ImVec2( pos.x + field_w - clear_w - 4.0f * u, pos.y + field_h ), true );
    mobile_ui::text( draw, face::body, 14.0f, ImVec2( pos.x + 36.0f * u,
                     pos.y + ( field_h - shown_px.y ) * 0.5f ),
                     has_filter ? mobile_ui::col_fg : mobile_ui::col_muted, shown );
    draw->PopClipRect();
    if( has_filter ) {
        if( mobile_ui::pressable( "##mods_search_clear", ImVec2( pos.x + field_w - clear_w, pos.y ),
                                  ImVec2( clear_w, field_h ) ) ) {
            act = mods_act::clear_search;
        }
        mobile_ui::icon( draw, "x", ImVec2( pos.x + field_w - clear_w + 11.0f * u, pos.y + 11.0f * u ),
                         18.0f, mobile_ui::col_muted );
    }

    // chips
    std::vector<std::string> labels = { std::string( _( "All" ) ) };
    for( const std::pair<std::string, translation> &t : get_mod_list_tabs() ) {
        labels.push_back( t.second.translated() );
    }
    const float chip_h = 30.0f * u;
    const float row_h = one_row ? field_h : 44.0f * u;
    float x = one_row ? pos.x + field_w + 10.0f * u : pos.x;
    const float chips_y = one_row ? pos.y : pos.y + field_h + 4.0f * u;
    for( size_t i = 0; i < labels.size(); ++i ) {
        const ImVec2 label_px = mobile_ui::text_size( face::bold, 13.0f, labels[i] );
        const float chip_w = label_px.x + 26.0f * u;
        const bool on = chip == static_cast<int>( i );
        bool chip_held = false;
        if( mobile_ui::pressable( "##mods_chip" + std::to_string( i ), ImVec2( x, chips_y ),
                                  ImVec2( chip_w, row_h ), &chip_held ) && !on ) {
            chip = static_cast<int>( i );
            has_selected = false;
        }
        const ImVec2 pill( x, chips_y + ( row_h - chip_h ) * 0.5f );
        mobile_ui::box( draw, pill, ImVec2( pill.x + chip_w, pill.y + chip_h ),
                        on ? mobile_ui::col_accent : chip_held ? mobile_ui::col_button_down :
                        IM_COL32( 255, 255, 255, 10 ), on ? mobile_ui::col_accent : mobile_ui::col_line, 15.0f );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( x + 13.0f * u,
                         pill.y + ( chip_h - label_px.y ) * 0.5f ),
                         on ? mobile_ui::col_on_accent : mobile_ui::col_text_soft, labels[i] );
        x += chip_w + 6.0f * u;
    }
    return one_row ? field_h : field_h + 4.0f * u + row_h;
}

bool mods_window::draw_row( ImDrawList *const list, const std::string &id, const ImVec2 &pos,
                            const float width, const std::string &lead, const std::string &name,
                            const std::string &detail, const std::string &right, const ImU32 right_color,
                            const bool on, const bool dim )
{
    const float u = mobile_ui::unit();
    const float h = 56.0f * u;
    // rows scrolled out of sight only take their space
    if( !ImGui::IsRectVisible( pos, ImVec2( pos.x + width, pos.y + h ) ) ) {
        return false;
    }
    bool held = false;
    const bool tapped = mobile_ui::pressable( id, pos, ImVec2( width, h ), &held );
    if( on || held ) {
        list->AddRectFilled( pos, ImVec2( pos.x + width, pos.y + h ),
                             held ? mobile_ui::col_button_down : mobile_ui::col_accent_soft );
    }
    float x = pos.x + 14.0f * u;
    if( !lead.empty() ) {
        const ImVec2 lead_px = mobile_ui::text_size( face::mono, 12.0f, lead );
        mobile_ui::text( list, face::mono, 12.0f, ImVec2( x + 20.0f * u - lead_px.x,
                         pos.y + ( h - lead_px.y ) * 0.5f ), mobile_ui::col_muted, lead );
        x += 30.0f * u;
    }
    const float right_w = right.empty() ? 0.0f :
                          mobile_ui::text_size( face::mono, 11.0f, right ).x + 10.0f * u;
    // the scroll bar is at the right edge
    const float text_right = pos.x + width - 22.0f * u - right_w;
    list->PushClipRect( ImVec2( x, pos.y ), ImVec2( text_right, pos.y + h ), true );
    mobile_ui::text( list, face::bold, 14.5f, ImVec2( x, pos.y + 9.0f * u ),
                     dim ? mobile_ui::col_muted : mobile_ui::col_fg, name );
    mobile_ui::text( list, face::body, 12.0f, ImVec2( x, pos.y + 31.0f * u ), mobile_ui::col_muted,
                     detail );
    list->PopClipRect();
    if( !right.empty() ) {
        mobile_ui::text( list, face::mono, 11.0f, ImVec2( text_right + 10.0f * u,
                         pos.y + ( h - mobile_ui::text_size( face::mono, 11.0f, right ).y ) * 0.5f ),
                         right_color, right );
    }
    list->AddLine( ImVec2( pos.x, pos.y + h ), ImVec2( pos.x + width, pos.y + h ), col_row_line,
                   std::max( 1.0f, u ) );
    return tapped;
}

// The world's mods in load order.
void mods_window::draw_active( const float list_w )
{
    const float u = mobile_ui::unit();
    const float h = 56.0f * u;
    ImDrawList *const list = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float row_y = base.y;
    if( active.empty() ) {
        mobile_ui::text( list, face::body, 14.0f, ImVec2( base.x + 14.0f * u, row_y + 20.0f * u ),
                         mobile_ui::col_muted, _( "No active mods." ), list_w - 28.0f * u );
        row_y += 60.0f * u;
    }
    for( size_t i = 0; i < active.size(); ++i ) {
        const mod_id &id = active[i];
        const bool on = has_selected && selected == id;
        if( on && reveal ) {
            ImGui::SetScrollY( std::max( 0.0f, row_y - base.y - ( ImGui::GetWindowHeight() - h ) * 0.5f ) );
            reveal = false;
        }
        const bool valid = id.is_valid();
        if( draw_row( list, "##mods_active" + std::to_string( i ), ImVec2( base.x, row_y ), list_w,
                      std::to_string( i + 1 ), mod_name( id ), mod_detail( id ),
                      valid ? std::string() : std::string( _( "Missing" ) ), col_warn, on, !valid ) ) {
            if( on ) {
                has_selected = false;
            } else {
                selected = id;
                has_selected = true;
            }
        }
        row_y += h;
    }
    if( !read_only && !active.empty() ) {
        // upstream's "save Mod Load Order as default"
        const std::string label = _( "Use these mods for new worlds" );
        const float button_w = std::min( list_w - 28.0f * u,
                                         mobile_ui::text_size( face::bold, 14.0f, label ).x + 32.0f * u );
        if( button( list, "##mods_save_default", ImVec2( base.x + 14.0f * u, row_y + 12.0f * u ),
                    ImVec2( button_w, 46.0f * u ), label, button_look::plain ) ) {
            act = mods_act::save_default;
        }
        row_y += 70.0f * u;
    }
    reveal = false;
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, row_y - base.y + 8.0f * u ) );
}

// The mods that can be used, under their categories.
void mods_window::draw_available( const float list_w )
{
    const float u = mobile_ui::unit();
    const float h = 56.0f * u;
    ImDrawList *const list = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float row_y = base.y;
    std::string last_category;
    bool any = false;
    for( size_t i = 0; i < available.size(); ++i ) {
        const mod_entry &entry = available[i];
        if( chip > 0 && entry.chip != chip - 1 ) {
            continue;
        }
        if( !filter.empty() && !lcmatch( entry.name, filter ) ) {
            continue;
        }
        if( !any || entry.category != last_category ) {
            last_category = entry.category;
            mobile_ui::text( list, face::mono, 10.5f, ImVec2( base.x + 14.0f * u, row_y + 14.0f * u ),
                             mobile_ui::col_muted, entry.category );
            row_y += 14.0f * u + mobile_ui::text_size( face::mono, 10.5f, entry.category ).y + 6.0f * u;
        }
        any = true;
        const bool on = has_selected && selected == entry.id;
        if( on && reveal ) {
            ImGui::SetScrollY( std::max( 0.0f, row_y - base.y - ( ImGui::GetWindowHeight() - h ) * 0.5f ) );
            reveal = false;
        }
        std::string right;
        ImU32 right_color = mobile_ui::col_accent;
        if( is_active( entry.id ) ) {
            right = _( "Active" );
        } else if( ui.find_mod_conflict( entry.id, active ) ) {
            right = _( "Conflict" );
            right_color = col_warn;
        }
        if( draw_row( list, "##mods_available" + std::to_string( i ), ImVec2( base.x, row_y ), list_w,
                      std::string(), entry.name, entry.detail, right, right_color, on, false ) ) {
            if( on ) {
                has_selected = false;
            } else {
                selected = entry.id;
                has_selected = true;
            }
        }
        row_y += h;
    }
    if( !any ) {
        // upstream's two messages
        mobile_ui::text( list, face::body, 14.0f, ImVec2( base.x + 14.0f * u, row_y + 20.0f * u ),
                         mobile_ui::col_muted, filter.empty() ? _( "No mods here." ) :
                         _( "No mod has that in its name." ), list_w - 28.0f * u );
        row_y += 60.0f * u;
    }
    reveal = false;
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, row_y - base.y + 8.0f * u ) );
}

// The selected mod: upstream's information text and what can be done with it.
void mods_window::draw_panel( const ImVec2 &min, const ImVec2 &max )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    mobile_ui::box( draw, min, max, mobile_ui::col_sheet, mobile_ui::col_line, 14.0f );
    const float pad = 14.0f * u;
    const float width = max.x - min.x - 2.0f * pad;
    float y = min.y + pad;
    const std::string name = mod_name( selected );
    mobile_ui::text( draw, face::display, 19.0f, ImVec2( min.x + pad, y ), mobile_ui::col_title, name,
                     width );
    y += mobile_ui::text_size( face::display, 19.0f, name, width ).y + 2.0f * u;
    const std::string sub = string_format( "%s · %s", mod_category( selected ), selected.str() );
    mobile_ui::text( draw, face::mono, 10.5f, ImVec2( min.x + pad, y ), mobile_ui::col_muted, sub,
                     width );
    y += mobile_ui::text_size( face::mono, 10.5f, sub, width ).y + 8.0f * u;

    const bool valid = selected.is_valid();
    const auto pos_it = std::find( active.begin(), active.end(), selected );
    const bool in_world = pos_it != active.end();
    const size_t index = static_cast<size_t>( std::distance( active.begin(), pos_it ) );

    // what the button would also do, or why there is none
    std::string note;
    bool warn = false;
    if( !read_only && valid && !in_world ) {
        note = blocked( selected );
        warn = !note.empty();
        if( !warn ) {
            std::vector<std::string> also;
            for( const mod_id &dep : ui.mm_tree.get_dependencies_of_X_as_strings( selected ) ) {
                if( !is_active( dep ) ) {
                    also.push_back( mod_name( dep ) );
                }
            }
            if( !also.empty() ) {
                note = string_format( _( "Adding it also adds: %s." ),
                                      enumerate_as_string( also, enumeration_conjunction::none ) );
            }
            if( selected->core && !active.empty() && active.front().is_valid() &&
                active.front()->core ) {
                note += ( note.empty() ? "" : "  " ) +
                        string_format( _( "It takes the place of %s." ), mod_name( active.front() ) );
            }
        }
    } else if( !read_only && valid && in_world ) {
        std::vector<std::string> also;
        for( const mod_id &dep : ui.mm_tree.get_dependents_of_X_as_strings( selected ) ) {
            if( is_active( dep ) ) {
                also.push_back( mod_name( dep ) );
            }
        }
        if( !also.empty() ) {
            note = string_format( _( "Removing it also removes: %s." ),
                                  enumerate_as_string( also, enumeration_conjunction::none ) );
        }
    }

    const bool has_buttons = !read_only && valid && !warn;
    const float button_h = has_buttons ? 48.0f * u + 10.0f * u : 0.0f;
    const float text_h = max.y - pad - button_h - y;
    ImGui::SetCursorScreenPos( ImVec2( min.x + pad, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##mods_description", ImVec2( width, std::max( text_h, 10.0f * u ) ),
                       ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    {
        ImDrawList *const inner = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        float text_y = base.y;
        // room for the scroll bar
        const float wrap = width - 12.0f * u;
        if( !note.empty() ) {
            mobile_ui::text( inner, face::bold, 13.0f, ImVec2( base.x, text_y ),
                             warn ? col_warn : mobile_ui::col_accent, note, wrap );
            text_y += mobile_ui::text_size( face::bold, 13.0f, note, wrap ).y + 8.0f * u;
        }
        const std::string info = valid ? remove_color_tags( ui.get_information( &*selected ) ) :
                                 std::string( _( "This mod is not installed." ) );
        mobile_ui::text( inner, face::body, 13.5f, ImVec2( base.x, text_y ), mobile_ui::col_text_soft,
                         info, wrap );
        text_y += mobile_ui::text_size( face::body, 13.5f, info, wrap ).y;
        ImGui::SetCursorScreenPos( base );
        ImGui::Dummy( ImVec2( 1.0f, text_y - base.y + 8.0f * u ) );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if( !has_buttons ) {
        return;
    }
    const float top = max.y - pad - 48.0f * u;
    const ImVec2 button_size( 0.0f, 48.0f * u );
    if( !in_world ) {
        if( button( draw, "##mods_add", ImVec2( min.x + pad, top ), ImVec2( width, button_size.y ),
                    _( "Add" ), button_look::primary ) ) {
            act = mods_act::add;
        }
        return;
    }
    if( tab == 1 ) {
        if( button( draw, "##mods_remove", ImVec2( min.x + pad, top ), ImVec2( width, button_size.y ),
                    _( "Remove" ), button_look::plain ) ) {
            act = mods_act::remove;
        }
        return;
    }
    // Remove, and the load order
    const float gap = 8.0f * u;
    const float third = ( width - 2.0f * gap ) / 3.0f;
    if( button( draw, "##mods_remove", ImVec2( min.x + pad, top ), ImVec2( third, button_size.y ),
                _( "Remove" ), button_look::plain ) ) {
        act = mods_act::remove;
    }
    if( button( draw, "##mods_up", ImVec2( min.x + pad + third + gap, top ),
                ImVec2( third, button_size.y ), _( "Move up" ), button_look::plain,
                ui.can_shift_up( index, active ) ) ) {
        act = mods_act::move_up;
    }
    if( button( draw, "##mods_down", ImVec2( min.x + pad + 2.0f * ( third + gap ), top ),
                ImVec2( third, button_size.y ), _( "Move down" ), button_look::plain,
                ui.can_shift_down( index, active ) ) ) {
        act = mods_act::move_down;
    }
}

void mods_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    fill_screen( origin, size );

    const float left = origin.x + safe.x;
    const float width = size.x - safe.x - safe.z;
    const bool landscape = size.x > size.y;
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( read_only ? _( "Active world mods" ) : _( "Mods" ),
                            ImVec2( left, origin.y + safe.y ), width, &bar_h ) ) {
        act = mods_act::back;
    }
    float y = origin.y + safe.y + bar_h;
    if( read_only ) {
        tab = 0;
    } else if( landscape ) {
        // a phone on its side has no height to spare: the tabs go into the bar
        draw_tabs( ImVec2( left, origin.y + safe.y + 9.0f * u ), left + width - 6.0f * u, true );
    } else {
        y += 10.0f * u;
        y += draw_tabs( ImVec2( left + 12.0f * u, y ), left + width, false );
    }
    if( tab == 1 ) {
        y += 8.0f * u;
        y += draw_filters( ImVec2( left + 12.0f * u, y ), width - 24.0f * u, landscape );
    }
    y += 6.0f * u;

    // the selected mod may have left the list
    if( has_selected && tab == 0 && !is_active( selected ) ) {
        has_selected = false;
    }
    const float bottom = origin.y + size.y - safe.w;
    float list_w = width;
    float list_h = bottom - y;
    if( has_selected && landscape ) {
        list_w = width * 0.5f;
    } else if( has_selected ) {
        list_h = ( bottom - y ) * 0.45f;
    }
    const bool had_selected = has_selected;
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    // a list of its own for each tab, so that each keeps its place
    ImGui::BeginChild( tab == 0 ? "##mods_rows_active" : "##mods_rows_available",
                       ImVec2( list_w, list_h ), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground );
    if( tab == 0 ) {
        draw_active( list_w );
    } else {
        draw_available( list_w );
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // a tap on the selected row has just closed its panel
    if( had_selected && has_selected ) {
        if( landscape ) {
            draw_panel( ImVec2( left + list_w + 6.0f * u, y ),
                        ImVec2( left + width - 10.0f * u, bottom - 10.0f * u ) );
        } else {
            draw_panel( ImVec2( left + 10.0f * u, y + list_h + 6.0f * u ),
                        ImVec2( left + width - 10.0f * u, bottom - 10.0f * u ) );
        }
    }
}

void run_mods( std::vector<mod_id> &active, mod_ui &ui, const bool read_only )
{
    mods_window window( active, ui, read_only );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    while( true ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        mods_act act = window.take();
        if( action == "QUIT" ) {
            // back closes the open mod first
            act = window.close_panel() ? mods_act::none : mods_act::back;
        }
        const mod_id selected = window.selection();
        const auto pos = std::find( active.begin(), active.end(), selected );
        size_t index = static_cast<size_t>( std::distance( active.begin(), pos ) );
        switch( act ) {
            case mods_act::none:
                break;
            case mods_act::back:
                return;
            case mods_act::add:
                ui.try_add( selected, active );
                break;
            case mods_act::remove:
                if( pos != active.end() ) {
                    ui.try_rem( index, active );
                }
                break;
            case mods_act::move_up:
            case mods_act::move_down:
                if( pos != active.end() && active.size() > 1 ) {
                    ui.try_shift( act == mods_act::move_up ? '-' : '+', index, active );
                    window.select( selected );
                }
                break;
            case mods_act::save_default:
                if( ui.active_manager.set_default_mods( active ) ) {
                    popup( _( "Saved list of active mods as default" ) );
                }
                break;
            case mods_act::search: {
                mobile_entry::spec what;
                what.title = _( "Search mods" );
                what.description = _( "Shows the mods with this in their name." );
                what.text = window.filter;
                what.max_length = 256;
                if( const std::optional<std::string> text = mobile_entry::query( what ) ) {
                    window.filter = *text;
                }
                break;
            }
            case mods_act::clear_search:
                window.filter.clear();
                break;
        }
    }
}

} // namespace

int new_world( WORLD &world, const hooks &upstream )
{
    // the sliders of upstream's world creation screen, see show_worldgen_basic
    std::vector<option_slider_id> sliders;
    std::vector<int> levels;
    for( const option_slider &osl : option_slider::get_all() ) {
        if( osl.context() == "WORLDGEN" ) {
            sliders.emplace_back( osl.id );
            levels.emplace_back( osl.default_level() );
        }
    }
    const std::vector<int> default_levels = levels;
    const auto differs = []( options_manager::options_container & now,
    const options_manager::options_container & before ) {
        for( const auto &elem : before ) {
            if( elem.second != now[elem.first] ) {
                return true;
            }
        }
        return false;
    };
    // a copied world brings options that no slider level stands for
    bool custom = differs( world.WORLD_OPTIONS, get_options().get_world_defaults() );

    std::unique_ptr<world_window> window = std::make_unique<world_window>( world, sliders, levels,
                                           custom );
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // true if the sliders may be used, after asking where upstream asks
    const auto sliders_free = [&]() {
        if( !custom ) {
            return true;
        }
        if( query_yn( _( "Currently using customized advanced options.  "
                         "Reset world options to defaults?" ) ) ) {
            world.WORLD_OPTIONS = get_options().get_world_defaults();
            levels = default_levels;
            custom = false;
        }
        return false;
    };
    while( true ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        world_act act = window->take();
        if( action == "QUIT" ) {
            act = world_act::back;
        }
        switch( act ) {
            case world_act::none:
                break;
            case world_act::back:
                if( query_yn( _( "Do you want to abort World Generation?" ) ) ) {
                    return -999;
                }
                break;
            case world_act::rename: {
                mobile_entry::spec what;
                what.title = _( "World name:" );
                what.text = world.world_name;
                what.max_length = upstream.max_name_length;
                const std::optional<std::string> name = mobile_entry::query( what );
                if( name && !name->empty() ) {
                    world.world_name = *name;
                }
                break;
            }
            case world_act::random_name:
                world.world_name = upstream.random_name();
                break;
            case world_act::lower:
            case world_act::raise:
                if( sliders_free() ) {
                    const size_t i = static_cast<size_t>( window->slider );
                    levels[i] = clamp<int>( levels[i] + ( act == world_act::lower ? -1 : 1 ), 0,
                                            sliders[i]->count() - 1 );
                    sliders[i]->apply_opts( levels[i], world.WORLD_OPTIONS );
                }
                break;
            case world_act::reset:
                if( query_yn( _( "Are you sure you want to reset this world?" ) ) ) {
                    world.WORLD_OPTIONS = get_options().get_world_defaults();
                    world.world_saves.clear();
                    world.active_mod_order = world_generator->get_mod_manager().get_default_mods();
                    levels = default_levels;
                    custom = false;
                }
                break;
            case world_act::randomize:
                if( sliders_free() ) {
                    for( size_t i = 0; i < sliders.size(); ++i ) {
                        levels[i] = sliders[i]->random_level();
                        // upstream only moves the slider here; the level shown is applied
                        // so that the world gets what the screen says
                        sliders[i]->apply_opts( levels[i], world.WORLD_OPTIONS );
                    }
                }
                break;
            case world_act::mods:
                upstream.edit_mods();
                break;
            case world_act::options: {
                const options_manager::options_container before = world.WORLD_OPTIONS;
                // upstream's screen is drawn under this one: step aside for it
                const float scroll = window->scroll;
                window.reset();
                upstream.edit_options();
                window = std::make_unique<world_window>( world, sliders, levels, custom );
                window->scroll = scroll;
                window->restore_scroll = scroll > 0.0f;
                // one frame to put the list back where it was, the next shows it
                ui_manager::redraw();
                if( differs( world.WORLD_OPTIONS, before ) ) {
                    custom = true;
                }
                break;
            }
            case world_act::create:
                // upstream's "Finish"
                if( world.world_name.empty() ) {
                    if( !query_yn( _( "World name will be randomly generated.  Proceed anyway?" ) ) ) {
                        break;
                    }
                    world.world_name = upstream.random_name();
                    if( world_generator->valid_worldname( world.world_name ) && upstream.mods_ok() ) {
                        return 1;
                    }
                } else if( world_generator->valid_worldname( world.world_name ) &&
                           query_yn( _( "Are you finished creating your world?" ) ) ) {
                    if( upstream.mods_ok() ) {
                        return 1;
                    }
                }
                break;
        }
    }
}

void edit_mods( std::vector<mod_id> &active, mod_ui &ui )
{
    run_mods( active, ui, false );
}

void show_mods( const std::vector<mod_id> &active, mod_ui &ui )
{
    // the window only reads the list when it is told to
    std::vector<mod_id> copy = active;
    run_mods( copy, ui, true );
}

#else // !TILES

int new_world( WORLD &, const hooks & )
{
    return -999;
}

void edit_mods( std::vector<mod_id> &, mod_ui & )
{
}

void show_mods( const std::vector<mod_id> &, mod_ui & )
{
}

#endif // TILES

} // namespace mobile_world
