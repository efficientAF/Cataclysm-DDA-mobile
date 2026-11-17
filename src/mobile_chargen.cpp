#include "mobile_chargen.h"

#if defined(TILES)
#include <algorithm>
#include <array>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cata_imgui.h"
#include "cata_utility.h"
#include "character.h"
#include "character_creator_ui.h"
#include "game_constants.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "input.h"
#include "input_enums.h"
#include "item.h"
#include "mobile_prompt.h"
#include "mobile_ui.h"
#include "mutation.h"
#include "options.h"
#include "output.h"
#include "player_difficulty.h"
#include "proficiency.h"
#include "profession.h"
#include "scenario.h"
#include "skill.h"
#include "start_location.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "uilist.h"
#include "units_utility.h"
#include "veh_type.h"

namespace mobile_chargen
{

using mobile_ui::face;

namespace
{

// colors of the canvas that the shared palette does not have
constexpr ImU32 col_good = IM_COL32( 108, 191, 85, 255 );
constexpr ImU32 col_good_soft = IM_COL32( 108, 191, 85, 41 );
constexpr ImU32 col_cost = IM_COL32( 230, 137, 45, 255 );
constexpr ImU32 col_bad = IM_COL32( 255, 138, 106, 255 );
constexpr ImU32 col_bad_soft = IM_COL32( 224, 73, 43, 46 );
constexpr ImU32 col_trait_good = IM_COL32( 143, 220, 106, 255 );
constexpr ImU32 col_more = IM_COL32( 224, 180, 140, 255 );
constexpr ImU32 col_card = IM_COL32( 255, 255, 255, 8 );
constexpr ImU32 col_card_on = IM_COL32( 240, 145, 63, 23 );
constexpr ImU32 col_row_line = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_input = IM_COL32( 9, 11, 8, 255 );
constexpr ImU32 col_bottom_bar = IM_COL32( 18, 22, 16, 255 );
constexpr ImU32 col_warn_bg = IM_COL32( 224, 73, 43, 36 );
constexpr ImU32 col_warn_fg = IM_COL32( 255, 176, 154, 255 );

ImU32 faded( const ImU32 color, const float opacity )
{
    const ImU32 alpha = ( color >> IM_COL32_A_SHIFT ) & 0xFF;
    const ImU32 new_alpha = static_cast<ImU32>( static_cast<float>( alpha ) * opacity );
    return ( color & ~IM_COL32_A_MASK ) | ( new_alpha << IM_COL32_A_SHIFT );
}

enum class command_kind : int {
    none,
    // a: step to show
    step,
    // a: tab, b: index of the entry to pick or toggle
    confirm,
    // a: stat, b: +1 or -1
    stat,
    // a: skill index, b: +1 or -1
    skill,
    // text: an action of upstream's creator
    action,
    // a: 1 for male
    gender,
    // a: start location (int id), or -1 for a random one
    location,
    // asks for the blood type on a sheet
    blood,
    next,
    back,
    leave,
};

struct command {
    command_kind kind = command_kind::none;
    int a = 0;
    int b = 0;
    std::string text;
};

enum class trait_filter : int {
    all,
    good,
    bad,
};

struct card {
    std::string title;
    ImU32 title_color = mobile_ui::col_fg;
    int cost = 0;
    std::string description;
    // lines shown under the description
    std::vector<std::string> more;
    // why it cannot be picked, if so
    std::string blocked;
    // index to report when tapped
    int index = 0;
    bool on = false;
    bool check = false;
    bool dim = false;
};

struct stat_info {
    character_stat stat;
    const char *abbreviation;
};

const std::array<stat_info, 4> stat_rows = { {
        { character_stat::STRENGTH, translate_marker( "STR" ) },
        { character_stat::DEXTERITY, translate_marker( "DEX" ) },
        { character_stat::INTELLIGENCE, translate_marker( "INT" ) },
        { character_stat::PERCEPTION, translate_marker( "PER" ) },
    }
};

std::string step_name( const int step )
{
    switch( static_cast<character_creator_tab>( step ) ) {
        case CHARCREATOR_SCENARIO:
            return _( "Scenario" );
        case CHARCREATOR_PROFESSION:
            return _( "Profession" );
        case CHARCREATOR_BACKGROUND:
            return _( "Background" );
        case CHARCREATOR_STATS:
            return _( "Stats" );
        case CHARCREATOR_TRAITS:
            return _( "Traits" );
        case CHARCREATOR_SKILLS:
            return _( "Skills" );
        case CHARCREATOR_SUMMARY:
            return _( "Summary" );
        default:
            break;
    }
    return std::string();
}

int stat_base( const Character &u, const character_stat stat )
{
    switch( stat ) {
        case character_stat::STRENGTH:
            return u.get_str_base();
        case character_stat::DEXTERITY:
            return u.get_dex_base();
        case character_stat::INTELLIGENCE:
            return u.get_int_base();
        case character_stat::PERCEPTION:
            return u.get_per_base();
        default:
            break;
    }
    return 0;
}

// What a stat at this level does, from the same numbers upstream's stat tab shows.
std::string stat_effects( avatar &u, const character_stat stat )
{
    switch( stat ) {
        case character_stat::STRENGTH:
            u.recalc_hp();
            u.set_stored_kcal( u.get_healthy_kcal() );
            return string_format( _( "Base HP %d · carry weight %.1f %s · lifting strength %d" ),
                                  u.get_part_hp_max( bodypart_id( "head" ) ),
                                  convert_weight( u.weight_capacity() ), weight_units(), u.get_lift_str() );
        case character_stat::DEXTERITY:
            return string_format( _( "Melee to-hit bonus %+.2f · dodge skill %.0f" ),
                                  u.get_melee_hit_base(), u.get_dodge() );
        case character_stat::INTELLIGENCE:
            return string_format( _( "Read times %d%% · crafting bonus %d%%" ), u.read_speed(),
                                  u.get_int() );
        case character_stat::PERCEPTION:
            if( u.ranged_per_mod() > 0 ) {
                return string_format( _( "Aiming penalty -%d · persuade/lie skill %d" ),
                                      u.ranged_per_mod(), u.persuade_skill() );
            }
            return string_format( _( "No aiming penalty · persuade/lie skill %d" ),
                                  u.persuade_skill() );
        default:
            break;
    }
    return std::string();
}

// Levels a skill gets from the profession and backgrounds, as upstream's skill
// list shows them: the profession's, or a background's if that is higher.
int skill_from_background( const avatar &u, const skill_id &skill )
{
    int level = 0;
    for( const std::pair<skill_id, int> &entry : u.prof->skills() ) {
        if( entry.first == skill ) {
            level += entry.second;
            break;
        }
    }
    for( const profession *const hobby : u.hobbies ) {
        for( const std::pair<skill_id, int> &entry : hobby->skills() ) {
            if( entry.first == skill ) {
                level = std::max( level, entry.second );
                break;
            }
        }
    }
    return level;
}

std::string skills_line( const profession &prof )
{
    std::vector<std::string> parts;
    for( const std::pair<skill_id, int> &entry : prof.skills() ) {
        parts.push_back( string_format( "%s %d", entry.first->name(), entry.second ) );
    }
    return parts.empty() ? std::string( _( "No starting skills" ) ) : enumerate_as_string( parts,
            enumeration_conjunction::none );
}

std::string traits_line( const profession &prof )
{
    std::vector<std::string> parts;
    for( const trait_and_var &entry : prof.get_locked_traits() ) {
        parts.push_back( entry.name() );
    }
    return enumerate_as_string( parts, enumeration_conjunction::none );
}

std::string extras_line( const profession &prof )
{
    std::vector<std::string> parts;
    for( const bionic_id &bionic : prof.CBMs() ) {
        parts.push_back( bionic->name.translated() );
    }
    for( const proficiency_id &known : prof.proficiencies() ) {
        parts.push_back( known->name() );
    }
    return enumerate_as_string( parts, enumeration_conjunction::none );
}

class chargen_window : public cataimgui::window
{
    public:
        chargen_window() : cataimgui::window( "MOBILE_CHARGEN",
                                                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                  ImGuiWindowFlags_NoBackground ) {}

        // what was tapped, for run() to carry out between frames
        command pending;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        trait_filter filter = trait_filter::all;
        float body_width = 0.0f;
        bool landscape = false;
        // the step drawn in the last frame
        int drawn_step = -1;

        void ask( const command_kind kind, const int a = 0, const int b = 0,
                  const std::string &text = std::string() ) {
            if( pending.kind == command_kind::none ) {
                pending = { kind, a, b, text };
            }
        }

        // Each body draws from the top left of the scrolling area and returns its height.
        float draw_scenarios( const ImVec2 &base );
        float draw_professions( const ImVec2 &base );
        float draw_backgrounds( const ImVec2 &base );
        float draw_stats( const ImVec2 &base );
        float draw_traits( const ImVec2 &base );
        float draw_skills( const ImVec2 &base );
        float draw_summary( const ImVec2 &base );

        float draw_lead( const ImVec2 &base, const std::string &text ) const;
        // Cards in one column, or two in landscape.  Returns the height used;
        // tapped receives the index of the card that was tapped.
        float draw_cards( const ImVec2 &pos, const std::vector<card> &cards, bool tight,
                          int *tapped ) const;
        float card_height( const card &c, float width, bool tight ) const;
        void draw_card( const card &c, const ImVec2 &pos, float width, float height, bool tight,
                        bool held ) const;
        // A row of pill buttons that wraps.  Returns the height used.
        float draw_chips( const std::string &id, const ImVec2 &pos, float width,
                          const std::vector<std::string> &labels, int selected, int *tapped ) const;
        // "- N +".  Returns -1, 0 or +1.
        int draw_stepper( const std::string &id, const ImVec2 &row_pos, float row_width,
                          float row_height, const std::string &value, bool can_lower, bool can_raise ) const;
        bool bar_button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                         const std::string &label, bool primary, bool enabled ) const;
};

float chargen_window::draw_lead( const ImVec2 &base, const std::string &text ) const
{
    const float u = mobile_ui::unit();
    const float wrap = body_width - 32.0f * u;
    mobile_ui::text( ImGui::GetWindowDrawList(), face::body, 13.0f,
                     ImVec2( base.x + 16.0f * u, base.y + 12.0f * u ), mobile_ui::col_text_soft, text, wrap );
    return mobile_ui::text_size( face::body, 13.0f, text, wrap ).y + 16.0f * u;
}

float chargen_window::card_height( const card &c, const float width, const bool tight ) const
{
    const float u = mobile_ui::unit();
    const float pad_x = ( tight ? 12.0f : 14.0f ) * u;
    const float pad_y = ( tight ? 10.0f : 12.0f ) * u;
    const float wrap = width - 2.0f * pad_x;
    float h = pad_y + mobile_ui::text_size( face::bold, 15.0f, c.title ).y;
    if( !c.description.empty() ) {
        h += 3.0f * u + mobile_ui::text_size( face::body, 13.0f, c.description, wrap ).y;
    }
    if( !c.blocked.empty() ) {
        h += 5.0f * u + mobile_ui::text_size( face::body, 12.5f, c.blocked, wrap ).y;
    }
    if( !c.more.empty() ) {
        h += 5.0f * u;
        for( const std::string &line : c.more ) {
            h += 3.0f * u + mobile_ui::text_size( face::body, 12.5f, line, wrap ).y;
        }
    }
    return h + pad_y;
}

void chargen_window::draw_card( const card &c, const ImVec2 &pos, const float width,
                                const float height, const bool tight, const bool held ) const
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float pad_x = ( tight ? 12.0f : 14.0f ) * u;
    const float pad_y = ( tight ? 10.0f : 12.0f ) * u;
    const float wrap = width - 2.0f * pad_x;
    const float opacity = c.dim ? 0.45f : 1.0f;
    mobile_ui::box( draw, pos, ImVec2( pos.x + width, pos.y + height ),
                    held ? mobile_ui::col_button_down : c.on ? col_card_on : col_card,
                    c.on ? mobile_ui::col_accent : mobile_ui::col_line, 14.0f );

    // cost, right aligned on the title line
    const std::string cost = c.cost == 0 ? std::string( _( "free" ) ) :
                             c.cost > 0 ? string_format( _( "-%d pts" ), c.cost ) :
                             string_format( _( "+%d pts" ), -c.cost );
    const ImVec2 cost_px = mobile_ui::text_size( face::mono, 11.5f, cost );
    const ImVec2 title_px = mobile_ui::text_size( face::bold, 15.0f, c.title );
    mobile_ui::text( draw, face::mono, 11.5f,
                     ImVec2( pos.x + width - pad_x - cost_px.x, pos.y + pad_y + title_px.y - cost_px.y - 1.0f * u ),
                     faded( c.cost == 0 ? mobile_ui::col_muted : c.cost > 0 ? col_cost : col_good, opacity ), cost );

    float x = pos.x + pad_x;
    float y = pos.y + pad_y;
    if( c.check ) {
        mobile_ui::icon( draw, "check", ImVec2( x, y + ( title_px.y - 15.0f * u ) * 0.5f ), 15.0f,
                         c.title_color );
        x += 19.0f * u;
    }
    draw->PushClipRect( ImVec2( x, y ), ImVec2( pos.x + width - pad_x - cost_px.x - 8.0f * u,
                        y + title_px.y ), true );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( x, y ), faded( c.title_color, opacity ),
                     c.title );
    draw->PopClipRect();
    x = pos.x + pad_x;
    y += title_px.y;
    if( !c.description.empty() ) {
        y += 3.0f * u;
        mobile_ui::text( draw, face::body, 13.0f, ImVec2( x, y ),
                         faded( mobile_ui::col_text_soft, opacity ), c.description, wrap );
        y += mobile_ui::text_size( face::body, 13.0f, c.description, wrap ).y;
    }
    if( !c.blocked.empty() ) {
        y += 5.0f * u;
        mobile_ui::text( draw, face::body, 12.5f, ImVec2( x, y ), col_bad, c.blocked, wrap );
        y += mobile_ui::text_size( face::body, 12.5f, c.blocked, wrap ).y;
    }
    if( !c.more.empty() ) {
        y += 5.0f * u;
        for( const std::string &line : c.more ) {
            y += 3.0f * u;
            mobile_ui::text( draw, face::body, 12.5f, ImVec2( x, y ), faded( col_more, opacity ), line,
                             wrap );
            y += mobile_ui::text_size( face::body, 12.5f, line, wrap ).y;
        }
    }
}

float chargen_window::draw_cards( const ImVec2 &pos, const std::vector<card> &cards,
                                  const bool tight, int *const tapped ) const
{
    const float u = mobile_ui::unit();
    const float gap = 8.0f * u;
    const int columns = landscape ? 2 : 1;
    const float inner = body_width - 24.0f * u;
    const float card_w = ( inner - gap * static_cast<float>( columns - 1 ) ) /
                         static_cast<float>( columns );
    float y = pos.y + 10.0f * u;
    const int count = static_cast<int>( cards.size() );
    for( int i = 0; i < count; i += columns ) {
        float row_h = 0.0f;
        for( int col = 0; col < columns && i + col < count; ++col ) {
            const card &c = cards[i + col];
            const float h = card_height( c, card_w, tight );
            row_h = std::max( row_h, h );
            const ImVec2 card_pos( pos.x + 12.0f * u + static_cast<float>( col ) * ( card_w + gap ), y );
            const ImVec2 card_max( card_pos.x + card_w, card_pos.y + h );
            if( !ImGui::IsRectVisible( card_pos, card_max ) ) {
                continue;
            }
            bool held = false;
            if( mobile_ui::pressable( "##card" + std::to_string( c.index ), card_pos, ImVec2( card_w, h ),
                                      &held ) ) {
                *tapped = c.index;
            }
            draw_card( c, card_pos, card_w, h, tight, held );
        }
        y += row_h + gap;
    }
    return y - gap + 16.0f * u - pos.y;
}

float chargen_window::draw_chips( const std::string &id, const ImVec2 &pos, const float width,
                                  const std::vector<std::string> &labels, const int selected, int *const tapped ) const
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    // the pill is 30 units tall; the area that takes the tap is 44
    const float pill_h = 30.0f * u;
    const float row_h = 44.0f * u;
    const float gap = 6.0f * u;
    float x = pos.x;
    float y = pos.y;
    const int count = static_cast<int>( labels.size() );
    for( int i = 0; i < count; ++i ) {
        const ImVec2 label_px = mobile_ui::text_size( face::bold, 13.0f, labels[i] );
        const float chip_w = label_px.x + 26.0f * u;
        if( x > pos.x && x + chip_w > pos.x + width ) {
            x = pos.x;
            y += row_h;
        }
        bool held = false;
        if( mobile_ui::pressable( id + std::to_string( i ), ImVec2( x, y ), ImVec2( chip_w, row_h ),
                                  &held ) ) {
            *tapped = i;
        }
        const bool on = i == selected;
        const ImVec2 pill( x, y + ( row_h - pill_h ) * 0.5f );
        mobile_ui::box( draw, pill, ImVec2( pill.x + chip_w, pill.y + pill_h ),
                        on ? mobile_ui::col_accent : held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 10 ),
                        on ? mobile_ui::col_accent : mobile_ui::col_line, 15.0f );
        mobile_ui::text( draw, face::bold, 13.0f,
                         ImVec2( x + 13.0f * u, pill.y + ( pill_h - label_px.y ) * 0.5f ),
                         on ? mobile_ui::col_on_accent : IM_COL32( 195, 201, 185, 255 ), labels[i] );
        x += chip_w + gap;
    }
    return y + row_h - pos.y;
}

int chargen_window::draw_stepper( const std::string &id, const ImVec2 &row_pos,
                                  const float row_width, const float row_height, const std::string &value,
                                  const bool can_lower, const bool can_raise ) const
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const float button = 42.0f * u;
    const float value_w = 34.0f * u;
    const float gap = 4.0f * u;
    const float top = row_pos.y + ( row_height - button ) * 0.5f;
    const float plus_x = row_pos.x + row_width - 14.0f * u - button;
    const float value_x = plus_x - gap - value_w;
    const float minus_x = value_x - gap - button;
    int result = 0;
    const auto step_button = [&]( const std::string & suffix, const float x, const char *icon,
    const bool enabled, const int delta ) {
        bool held = false;
        // the whole height of the row takes the tap
        if( enabled && mobile_ui::pressable( id + suffix, ImVec2( x, row_pos.y ),
                                             ImVec2( button, row_height ), &held ) ) {
            result = delta;
        }
        const float opacity = enabled ? 1.0f : 0.35f;
        mobile_ui::box( draw, ImVec2( x, top ), ImVec2( x + button, top + button ),
                        held ? mobile_ui::col_button_down : faded( mobile_ui::col_fill_soft, opacity ),
                        faded( mobile_ui::col_line, opacity ), 11.0f );
        mobile_ui::icon( draw, icon, ImVec2( x + 12.0f * u, top + 12.0f * u ), 18.0f,
                         faded( mobile_ui::col_fg, opacity ) );
    };
    step_button( "_minus", minus_x, "minus", can_lower, -1 );
    step_button( "_plus", plus_x, "plus", can_raise, 1 );
    const ImVec2 value_px = mobile_ui::text_size( face::mono, 18.0f, value );
    mobile_ui::text( draw, face::mono, 18.0f, ImVec2( value_x + ( value_w - value_px.x ) * 0.5f,
                     row_pos.y + ( row_height - value_px.y ) * 0.5f ), mobile_ui::col_fg, value );
    return result;
}

bool chargen_window::bar_button( const std::string &id, const ImVec2 &pos, const ImVec2 &size,
                                 const std::string &label, const bool primary, const bool enabled ) const
{
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    bool held = false;
    const bool tapped = enabled && mobile_ui::pressable( id, pos, size, &held );
    const float opacity = enabled ? 1.0f : 0.4f;
    const ImU32 fill = primary ? ( held ? mobile_ui::col_accent_down : mobile_ui::col_accent ) :
                       ( held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 15 ) );
    mobile_ui::box( draw, pos, ImVec2( pos.x + size.x, pos.y + size.y ), faded( fill, opacity ),
                    faded( primary ? mobile_ui::col_accent : mobile_ui::col_line, opacity ), 12.0f );
    const ImVec2 label_px = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f, ImVec2( pos.x + ( size.x - label_px.x ) * 0.5f,
                     pos.y + ( size.y - label_px.y ) * 0.5f ),
                     faded( primary ? mobile_ui::col_on_accent : mobile_ui::col_fg, opacity ), label );
    return tapped;
}

float chargen_window::draw_scenarios( const ImVec2 &base )
{
    character_creator_uistate &state = upstream_state();
    const avatar &u = get_avatar();
    state.recalc_scenario_list( u );
    std::vector<card> cards;
    const int count = static_cast<int>( state.sorted_scenarios.size() );
    cards.reserve( count );
    for( int i = 0; i < count; ++i ) {
        const scenario *const scen = state.sorted_scenarios[i];
        card c;
        c.index = i;
        c.title = remove_color_tags( scen->gender_appropriate_name( u.male ) );
        c.cost = scen->point_cost();
        c.description = remove_color_tags( scen->description( u.male ) );
        c.on = scen == get_scenario();
        const ret_val<void> can_pick = scen->can_pick();
        if( !can_pick.success() ) {
            c.dim = true;
            c.blocked = remove_color_tags( can_pick.str() );
        }
        if( c.on ) {
            c.more.push_back( string_format( _( "· Starts at: %s" ), string_format(
                                                 _( "%s (%d locations, %d variants)" ), scen->start_name(),
                                                 scen->start_location_count(), scen->start_location_targets_count() ) ) );
            c.more.push_back( string_format( _( "· Professions: %s" ), scen->prof_count_str() ) );
            if( scen->vehicle() ) {
                c.more.push_back( string_format( _( "· Vehicle: %s" ), scen->vehicle()->name.translated() ) );
            }
            if( c.cost < 0 ) {
                c.more.push_back( string_format( n_gettext( "· Gives %d point", "· Gives %d points", -c.cost ),
                                                 -c.cost ) );
            } else if( c.cost > 0 ) {
                c.more.push_back( string_format( n_gettext( "· Costs %d point", "· Costs %d points", c.cost ),
                                                 c.cost ) );
            }
        }
        cards.push_back( c );
    }
    int tapped = -1;
    const float height = draw_cards( base, cards, false, &tapped );
    if( tapped >= 0 ) {
        ask( command_kind::confirm, CHARCREATOR_SCENARIO, tapped );
    }
    return height;
}

float chargen_window::draw_professions( const ImVec2 &base )
{
    character_creator_uistate &state = upstream_state();
    const avatar &u = get_avatar();
    state.recalc_profession_list( u );
    std::vector<card> cards;
    const int count = static_cast<int>( state.sorted_professions.size() );
    cards.reserve( count );
    for( int i = 0; i < count; ++i ) {
        const profession &prof = *state.sorted_professions[i];
        card c;
        c.index = i;
        c.title = remove_color_tags( prof.gender_appropriate_name( u.male ) );
        c.cost = prof.point_cost();
        c.description = remove_color_tags( prof.description( u.male ) );
        c.on = &prof == u.prof;
        const ret_val<void> can_pick = prof.can_pick();
        if( !can_pick.success() ) {
            c.dim = true;
            c.blocked = remove_color_tags( can_pick.str() );
        }
        if( c.on ) {
            c.more.push_back( string_format( _( "Skills: %s" ), skills_line( prof ) ) );
            const std::string traits = traits_line( prof );
            if( !traits.empty() ) {
                c.more.push_back( string_format( _( "Traits: %s" ), traits ) );
            }
            const std::string extras = extras_line( prof );
            if( !extras.empty() ) {
                c.more.push_back( string_format( _( "Also: %s" ), extras ) );
            }
            // the example inventory upstream's profession tab shows
            if( state.cached_profession_inventory.empty() ) {
                state.cached_profession_inventory = prof.items( upstream_outfit_male(), u.get_mutations() );
            }
            std::vector<std::string> items;
            for( const item &it : state.cached_profession_inventory ) {
                items.push_back( remove_color_tags( it.tname( 1, false ) ) );
            }
            if( !items.empty() ) {
                c.more.push_back( string_format( _( "Starts with: %s" ), enumerate_as_string( items,
                                                 enumeration_conjunction::none ) ) );
            }
        }
        cards.push_back( c );
    }
    int tapped = -1;
    const float height = draw_cards( base, cards, false, &tapped );
    if( tapped >= 0 ) {
        ask( command_kind::confirm, CHARCREATOR_PROFESSION, tapped );
    }
    return height;
}

float chargen_window::draw_backgrounds( const ImVec2 &base )
{
    character_creator_uistate &state = upstream_state();
    const avatar &u = get_avatar();
    state.recalc_hobby_list( u );
    const float lead = draw_lead( base,
                                  _( "Pick any number of backgrounds.  Each adds a little skill." ) );
    std::vector<card> cards;
    const int count = static_cast<int>( state.sorted_hobbies.size() );
    cards.reserve( count );
    for( int i = 0; i < count; ++i ) {
        const profession &hobby = *state.sorted_hobbies[i];
        card c;
        c.index = i;
        c.title = remove_color_tags( hobby.gender_appropriate_name( u.male ) );
        c.cost = hobby.point_cost();
        c.description = remove_color_tags( hobby.description( u.male ) );
        c.on = u.hobbies.count( &hobby ) > 0;
        c.check = c.on;
        const ret_val<void> can_pick = hobby.can_pick();
        if( !can_pick.success() ) {
            c.dim = true;
            c.blocked = remove_color_tags( can_pick.str() );
        }
        std::string gives = skills_line( hobby );
        const std::string traits = traits_line( hobby );
        if( !traits.empty() ) {
            gives += string_format( " · %s", traits );
        }
        const std::string extras = extras_line( hobby );
        if( !extras.empty() ) {
            gives += string_format( " · %s", extras );
        }
        c.more.push_back( gives );
        cards.push_back( c );
    }
    int tapped = -1;
    const float height = draw_cards( ImVec2( base.x, base.y + lead - 6.0f * mobile_ui::unit() ), cards,
                                     false, &tapped );
    if( tapped >= 0 ) {
        ask( command_kind::confirm, CHARCREATOR_BACKGROUND, tapped );
    }
    return lead - 6.0f * mobile_ui::unit() + height;
}

float chargen_window::draw_stats( const ImVec2 &base )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    avatar &you = get_avatar();
    const float lead = draw_lead( base, string_format(
                                      _( "8 is average.  Each point above %d costs double." ), HIGH_STAT ) );
    float y = base.y + lead + 4.0f * u;
    const float row_h = 62.0f * u;
    for( const stat_info &row : stat_rows ) {
        const int index = static_cast<int>( row.stat );
        const int value = stat_base( you, row.stat );
        const ImVec2 row_pos( base.x, y );
        mobile_ui::text( draw, face::mono, 16.0f, ImVec2( base.x + 14.0f * u, y + 13.0f * u ),
                         mobile_ui::col_fg, _( row.abbreviation ) );
        mobile_ui::text( draw, face::body, 11.0f, ImVec2( base.x + 14.0f * u, y + 35.0f * u ),
                         mobile_ui::col_muted,
                         uppercase_first_letter( _( io::enum_to_full_string( row.stat ) ) ) );
        const float text_x = base.x + 14.0f * u + 84.0f * u;
        const float text_w = body_width - 14.0f * u - 84.0f * u - 10.0f * u - 154.0f * u;
        const std::string line = string_format( "%s · %s",
                                                uppercase_first_letter( char_creation::stat_level_description( value ) ),
                                                stat_effects( you, row.stat ) );
        const ImVec2 line_px = mobile_ui::text_size( face::body, 12.5f, line, text_w );
        draw->PushClipRect( ImVec2( text_x, y ), ImVec2( text_x + text_w, y + row_h ), true );
        mobile_ui::text( draw, face::body, 12.5f,
                         ImVec2( text_x, y + std::max( 2.0f * u, ( row_h - line_px.y ) * 0.5f ) ),
                         mobile_ui::col_text_soft, line, text_w );
        draw->PopClipRect();
        const int delta = draw_stepper( "##stat" + std::to_string( index ), row_pos, body_width, row_h,
                                        std::to_string( value ), value > CHARACTER_STAT_MIN, value < CHARACTER_STAT_MAX );
        if( delta != 0 ) {
            ask( command_kind::stat, index, delta );
        }
        y += row_h;
        draw->AddLine( ImVec2( base.x, y ), ImVec2( base.x + body_width, y ), col_row_line );
    }
    return y - base.y + 8.0f * u;
}

float chargen_window::draw_traits( const ImVec2 &base )
{
    const float u = mobile_ui::unit();
    character_creator_uistate &state = upstream_state();
    const avatar &you = get_avatar();
    state.recalc_trait_list( you );

    std::vector<card> cards;
    int chosen = 0;
    const int count = static_cast<int>( state.sorted_traits.size() );
    for( int i = 0; i < count; ++i ) {
        const trait_id &trait = state.sorted_traits[i];
        const mutation_branch &data = trait.obj();
        const bool has = you.has_trait( trait );
        if( has ) {
            ++chosen;
        }
        if( ( filter == trait_filter::good && data.points <= 0 ) ||
            ( filter == trait_filter::bad && data.points >= 0 ) ) {
            continue;
        }
        card c;
        c.index = i;
        c.title = remove_color_tags( has ? you.mutation_name( trait ) : data.name() );
        c.title_color = data.points > 0 ? col_trait_good : data.points < 0 ? col_bad : mobile_ui::col_fg;
        c.cost = data.points;
        c.description = remove_color_tags( data.desc() );
        c.on = has;
        c.check = has;
        // greyed out when upstream's list would be
        c.dim = !has && ( you.has_conflicting_trait( trait ) ||
                          get_scenario()->is_forbidden_trait( trait ) || you.prof->is_forbidden_trait( trait ) );
        cards.push_back( c );
    }

    int tapped_chip = -1;
    const float chips_x = base.x + 12.0f * u;
    draw_chips( "##trait_filter", ImVec2( chips_x, base.y + 3.0f * u ), body_width - 24.0f * u,
    { _( "All" ), _( "Good" ), _( "Bad" ) }, static_cast<int>( filter ), &tapped_chip );
    if( tapped_chip >= 0 ) {
        filter = static_cast<trait_filter>( tapped_chip );
    }
    const std::string note = string_format( _( "%d chosen" ), chosen );
    const ImVec2 note_px = mobile_ui::text_size( face::bold, 12.0f, note );
    mobile_ui::text( ImGui::GetWindowDrawList(), face::bold, 12.0f,
                     ImVec2( base.x + body_width - 16.0f * u - note_px.x,
                             base.y + 3.0f * u + ( 44.0f * u - note_px.y ) * 0.5f ),
                     mobile_ui::col_muted, note );

    int tapped = -1;
    const float top = 40.0f * u;
    const float height = draw_cards( ImVec2( base.x, base.y + top ), cards, true, &tapped );
    if( tapped >= 0 ) {
        ask( command_kind::confirm, CHARCREATOR_TRAITS, tapped );
    }
    return top + height;
}

float chargen_window::draw_skills( const ImVec2 &base )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    character_creator_uistate &state = upstream_state();
    const avatar &you = get_avatar();
    state.recalc_skill_list();
    const float lead = draw_lead( base,
                                  _( "Your profession and backgrounds give starting levels.  Higher levels cost more points." ) );
    float y = base.y + lead + 4.0f * u;
    const float row_h = 62.0f * u;
    const int count = static_cast<int>( state.sorted_skills.size() );
    // two columns of rows in landscape
    const int columns = landscape ? 2 : 1;
    const float col_w = body_width / static_cast<float>( columns );
    for( int i = 0; i < count; ++i ) {
        const int col = i % columns;
        const ImVec2 row_pos( base.x + static_cast<float>( col ) * col_w, y );
        if( ImGui::IsRectVisible( row_pos, ImVec2( row_pos.x + col_w, row_pos.y + row_h ) ) ) {
            const skill_id skill = state.sorted_skills[i]->ident();
            const int own = you.get_skill_level( skill );
            const int given = skill_from_background( you, skill );
            mobile_ui::text( draw, face::bold, 14.5f, ImVec2( row_pos.x + 14.0f * u, y + 13.0f * u ),
                             mobile_ui::col_fg, uppercase_first_letter( skill->name() ) );
            mobile_ui::text( draw, face::body, 11.0f, ImVec2( row_pos.x + 14.0f * u, y + 34.0f * u ),
                             mobile_ui::col_muted,
                             given > 0 ? string_format( _( "%d from background" ), given ) : std::string( _( "untrained" ) ) );
            const int delta = draw_stepper( "##skill" + std::to_string( i ), row_pos, col_w, row_h,
                                            std::to_string( own + given ), own > MIN_SKILL, own < MAX_SKILL );
            if( delta != 0 ) {
                ask( command_kind::skill, i, delta );
            }
            draw->AddLine( ImVec2( row_pos.x, y + row_h ), ImVec2( row_pos.x + col_w, y + row_h ),
                           col_row_line );
        }
        if( col == columns - 1 || i == count - 1 ) {
            y += row_h;
        }
    }
    return y - base.y + 8.0f * u;
}

float chargen_window::draw_summary( const ImVec2 &base )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    character_creator_uistate &state = upstream_state();
    const avatar &you = get_avatar();
    const float left = base.x + 16.0f * u;
    const float width = body_width - 32.0f * u;
    float y = base.y + 14.0f * u;

    const auto label = [&]( const std::string & text ) {
        y += 6.0f * u;
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( left, y ), mobile_ui::col_muted, text );
        y += 10.5f * u + 6.0f * u;
    };

    // name: tapping the field opens upstream's name entry
    label( _( "NAME" ) );
    {
        const float field_w = width - 52.0f * u;
        const float field_h = 44.0f * u;
        bool held = false;
        if( mobile_ui::pressable( "##name", ImVec2( left, y ), ImVec2( field_w, field_h ), &held ) ) {
            ask( command_kind::action, 0, 0, "CHANGE_NAME" );
        }
        mobile_ui::box( draw, ImVec2( left, y ), ImVec2( left + field_w, y + field_h ), col_input,
                        held ? mobile_ui::col_accent : mobile_ui::col_line, 11.0f );
        const bool unnamed = you.name.empty();
        const std::string shown = unnamed ? std::string( _( "Random name" ) ) : you.name;
        const ImVec2 shown_px = mobile_ui::text_size( face::body, 15.0f, shown );
        draw->PushClipRect( ImVec2( left, y ), ImVec2( left + field_w - 8.0f * u, y + field_h ), true );
        mobile_ui::text( draw, face::body, 15.0f, ImVec2( left + 12.0f * u,
                         y + ( field_h - shown_px.y ) * 0.5f ), unnamed ? mobile_ui::col_muted : mobile_ui::col_fg,
                         shown );
        draw->PopClipRect();
        const ImVec2 dice( left + field_w + 8.0f * u, y );
        if( mobile_ui::pressable( "##random_name", dice, ImVec2( 44.0f * u, 44.0f * u ), &held ) ) {
            ask( command_kind::action, 0, 0, "RANDOMIZE_CHAR_NAME" );
        }
        mobile_ui::box( draw, dice, ImVec2( dice.x + 44.0f * u, dice.y + 44.0f * u ),
                        held ? mobile_ui::col_button_down : mobile_ui::col_fill_soft, mobile_ui::col_line, 12.0f );
        mobile_ui::icon( draw, "dice", ImVec2( dice.x + 11.0f * u, dice.y + 11.0f * u ), 22.0f,
                         mobile_ui::col_fg );
        y += field_h + 6.0f * u;
    }

    label( _( "GENDER" ) );
    {
        int tapped = -1;
        y += draw_chips( "##gender", ImVec2( left, y - 5.0f * u ), width,
        { remove_color_tags( char_creation::get_gender_string( true ) ), remove_color_tags( char_creation::get_gender_string( false ) ) },
        you.male ? 0 : 1, &tapped ) - 5.0f * u;
        if( tapped >= 0 ) {
            ask( command_kind::gender, tapped == 0 ? 1 : 0 );
        }
    }

    label( _( "STARTING LOCATION" ) );
    {
        // a chip per place the scenario allows; a long list opens upstream's menu instead
        std::vector<const start_location *> allowed;
        for( const start_location &loc : start_locations::get_all() ) {
            if( get_scenario()->allowed_start( loc.id ) ) {
                allowed.push_back( &loc );
            }
        }
        const bool as_chips = allowed.size() <= 8;
        std::vector<std::string> labels = { _( "Random" ) };
        int selected = you.random_start_location ? 0 : -1;
        if( as_chips ) {
            for( const start_location *const loc : allowed ) {
                if( !you.random_start_location && loc->id == you.start_location ) {
                    selected = static_cast<int>( labels.size() );
                }
                labels.push_back( remove_color_tags( loc->name() ) );
            }
        } else {
            if( !you.random_start_location ) {
                selected = 1;
                labels.push_back( remove_color_tags( you.start_location->name() ) );
            }
            labels.push_back( string_format( _( "Choose from %d places…" ),
                                             static_cast<int>( allowed.size() ) ) );
        }
        int tapped = -1;
        y += draw_chips( "##start", ImVec2( left, y - 5.0f * u ), width, labels, selected,
                         &tapped ) - 5.0f * u;
        if( tapped == 0 ) {
            ask( command_kind::location, -1 );
        } else if( tapped > 0 && as_chips ) {
            ask( command_kind::location, allowed[tapped - 1]->id.id().to_i() );
        } else if( tapped == static_cast<int>( labels.size() ) - 1 ) {
            ask( command_kind::action, 0, 0, "CHOOSE_LOCATION" );
        }
    }
    y += 12.0f * u;

    // what upstream has on its description tab: a row each, the value on the right
    label( _( "DETAILS" ) );
    {
        struct detail {
            std::string name;
            std::string value;
            command_kind kind;
            std::string action;
        };
        std::vector<detail> details = {
            { _( "Age" ), you.age_string( get_scenario()->start_of_game() ), command_kind::action, "CHANGE_AGE" },
            { _( "Height" ), you.height_string(), command_kind::action, "CHANGE_HEIGHT" },
            {
                _( "Blood type" ), io::enum_to_string( you.my_blood_type ) + ( you.blood_rh_factor ? "+" : "-" ),
                command_kind::blood, ""
            },
            {
                _( "Start of cataclysm" ), to_string( get_scenario()->start_of_cataclysm() ),
                command_kind::action, "CHANGE_START_OF_CATACLYSM"
            },
            {
                _( "Start of game" ), to_string( get_scenario()->start_of_game() ),
                command_kind::action, "CHANGE_START_OF_GAME"
            },
        };
        if( get_option<bool>( "SELECT_STARTING_CITY" ) ) {
            details.push_back( {
                _( "Starting city" ), you.starting_city.has_value() ? you.starting_city->name : _( "Random" ),
                command_kind::action, "CHOOSE_CITY"
            } );
        }
        const int columns = landscape ? 2 : 1;
        const float gap = 8.0f * u;
        const float cell_w = ( width - static_cast<float>( columns - 1 ) * gap ) /
                             static_cast<float>( columns );
        const float cell_h = 48.0f * u;
        for( int i = 0; i < static_cast<int>( details.size() ); ++i ) {
            const detail &d = details[i];
            const ImVec2 pos( left + static_cast<float>( i % columns ) * ( cell_w + gap ),
                              y + static_cast<float>( i / columns ) * ( cell_h + gap ) );
            bool held = false;
            if( mobile_ui::pressable( "##detail" + std::to_string( i ), pos, ImVec2( cell_w, cell_h ),
                                      &held ) ) {
                ask( d.kind, 0, 0, d.action );
            }
            mobile_ui::box( draw, pos, ImVec2( pos.x + cell_w, pos.y + cell_h ),
                            held ? mobile_ui::col_button_down : col_card, mobile_ui::col_line, 11.0f );
            const ImVec2 name_px = mobile_ui::text_size( face::body, 14.0f, d.name );
            mobile_ui::text( draw, face::body, 14.0f, ImVec2( pos.x + 12.0f * u,
                             pos.y + ( cell_h - name_px.y ) * 0.5f ), mobile_ui::col_muted, d.name );
            const ImVec2 value_px = mobile_ui::text_size( face::bold, 14.0f, d.value );
            const float value_left = pos.x + 12.0f * u + name_px.x + 10.0f * u;
            draw->PushClipRect( ImVec2( value_left, pos.y ), ImVec2( pos.x + cell_w - 8.0f * u,
                                pos.y + cell_h ), true );
            mobile_ui::text( draw, face::bold, 14.0f,
                             ImVec2( std::max( value_left, pos.x + cell_w - 12.0f * u - value_px.x ),
                                     pos.y + ( cell_h - value_px.y ) * 0.5f ), mobile_ui::col_fg, d.value );
            draw->PopClipRect();
        }
        const int lines = ( static_cast<int>( details.size() ) + columns - 1 ) / columns;
        y += static_cast<float>( lines ) * ( cell_h + gap );

        // two buttons for the description as a whole
        const float button_w = ( width - gap ) * 0.5f;
        if( bar_button( "##random_description", ImVec2( left, y ), ImVec2( button_w, 44.0f * u ),
                        _( "Random description" ), false, true ) ) {
            ask( command_kind::action, 0, 0, "RANDOMIZE_CHAR_DESCRIPTION" );
        }
        if( bar_button( "##save_template", ImVec2( left + button_w + gap, y ),
                        ImVec2( button_w, 44.0f * u ), _( "Save template" ), false, true ) ) {
            ask( command_kind::action, 0, 0, "SAVE_TEMPLATE" );
        }
        y += 44.0f * u;
    }
    y += 12.0f * u;

    // the summary list
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back( _( "Scenario" ),
                       remove_color_tags( get_scenario()->gender_appropriate_name( you.male ) ) );
    rows.emplace_back( _( "Profession" ),
                       remove_color_tags( you.prof->gender_appropriate_name( you.male ) ) );
    std::vector<std::string> parts;
    for( const profession *const hobby : you.hobbies ) {
        parts.push_back( remove_color_tags( hobby->gender_appropriate_name( you.male ) ) );
    }
    const auto joined = [&parts]() {
        return parts.empty() ? std::string( _( "None" ) ) : enumerate_as_string( parts,
                enumeration_conjunction::none );
    };
    rows.emplace_back( _( "Background" ), joined() );
    parts.clear();
    for( const stat_info &row : stat_rows ) {
        parts.push_back( string_format( "%s %d", _( row.abbreviation ), stat_base( you, row.stat ) ) );
    }
    rows.emplace_back( _( "Stats" ), string_join( parts, " · " ) );
    parts.clear();
    for( const trait_id &trait : you.get_mutations( false ) ) {
        parts.push_back( remove_color_tags( you.mutation_name( trait ) ) );
    }
    rows.emplace_back( _( "Traits" ), joined() );
    parts.clear();
    state.recalc_skill_list();
    for( const Skill *const skill : state.sorted_skills ) {
        const int level = you.get_skill_level( skill->ident() ) + skill_from_background( you,
                          skill->ident() );
        if( level > 0 ) {
            parts.push_back( string_format( "%s %d", skill->name(), level ) );
        }
    }
    rows.emplace_back( _( "Skills" ), joined() );

    const float value_x = left + 106.0f * u;
    const float value_w = width - 106.0f * u;
    for( const std::pair<std::string, std::string> &row : rows ) {
        y += 8.0f * u;
        mobile_ui::text( draw, face::body, 13.5f, ImVec2( left, y ), mobile_ui::col_muted, row.first );
        mobile_ui::text( draw, face::bold, 13.5f, ImVec2( value_x, y ), mobile_ui::col_fg, row.second,
                         value_w );
        y += mobile_ui::text_size( face::bold, 13.5f, row.second, value_w ).y + 8.0f * u;
        draw->AddLine( ImVec2( left, y ), ImVec2( left + width, y ), IM_COL32( 255, 255, 255, 15 ) );
    }
    y += 12.0f * u;

    const int points = upstream_points_left( you );
    if( points < 0 ) {
        const std::string warning = string_format( n_gettext(
                                        "You are %d point over budget.  Remove a trait or lower a stat to start.",
                                        "You are %d points over budget.  Remove a trait or lower a stat to start.", -points ), -points );
        const float wrap = width - 24.0f * u;
        const float h = mobile_ui::text_size( face::body, 13.0f, warning, wrap ).y + 20.0f * u;
        mobile_ui::box( draw, ImVec2( left, y ), ImVec2( left + width, y + h ), col_warn_bg, 0, 10.0f );
        mobile_ui::text( draw, face::body, 13.0f, ImVec2( left + 12.0f * u, y + 10.0f * u ), col_warn_fg,
                         warning, wrap );
        y += h + 14.0f * u;
    }
    return y - base.y;
}

void chargen_window::draw_controls()
{
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();

    character_creator_uistate &state = upstream_state();
    const avatar &you = get_avatar();
    const int step = static_cast<int>( state.selected_tab );
    const int last_step = CHARACTER_CREATOR_TAB_COUNT - 1;
    const int points = upstream_points_left( you );
    landscape = size.x > size.y;

    const float left = origin.x + safe.x;
    const float right = origin.x + size.x - safe.z;
    const float width = right - left;
    float y = origin.y + safe.y;

    // --- app bar: back, title, points left, randomize ---
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "New character" ), ImVec2( left, y ), width, &bar_h ) ) {
        ask( command_kind::leave );
    }
    {
        const ImVec2 dice( right - 8.0f * u - 44.0f * u, y + 5.0f * u );
        bool held = false;
        if( mobile_ui::pressable( "##randomize", dice, ImVec2( 44.0f * u, 44.0f * u ), &held ) ) {
            ask( command_kind::action, 0, 0, "REROLL_CHARACTER" );
        }
        if( held ) {
            mobile_ui::box( draw, dice, ImVec2( dice.x + 44.0f * u, dice.y + 44.0f * u ),
                            mobile_ui::col_fill_soft, 0, 12.0f );
        }
        mobile_ui::icon( draw, "dice", ImVec2( dice.x + 11.0f * u, dice.y + 11.0f * u ), 22.0f,
                         mobile_ui::col_fg );
        const std::string chip = string_format( n_gettext( "%d pt left", "%d pts left", points ), points );
        const ImVec2 chip_px = mobile_ui::text_size( face::mono, 12.0f, chip );
        const ImVec2 chip_size( chip_px.x + 20.0f * u, 28.0f * u );
        const ImVec2 chip_pos( dice.x - 4.0f * u - chip_size.x, y + ( bar_h - chip_size.y ) * 0.5f );
        mobile_ui::box( draw, chip_pos, ImVec2( chip_pos.x + chip_size.x, chip_pos.y + chip_size.y ),
                        points < 0 ? col_bad_soft : col_good_soft, 0, 14.0f );
        mobile_ui::text( draw, face::mono, 12.0f, ImVec2( chip_pos.x + 10.0f * u,
                         chip_pos.y + ( chip_size.y - chip_px.y ) * 0.5f ), points < 0 ? col_bad : col_good, chip );
    }
    y += bar_h;

    // --- steps: labeled progress segments ---
    {
        const float steps_h = 44.0f * u;
        const float gap = 6.0f * u;
        const float seg_w = ( width - 24.0f * u - gap * static_cast<float>( last_step ) ) /
                            static_cast<float>( CHARACTER_CREATOR_TAB_COUNT );
        for( int i = 0; i <= last_step; ++i ) {
            const float x = left + 12.0f * u + static_cast<float>( i ) * ( seg_w + gap );
            if( mobile_ui::pressable( "##step" + std::to_string( i ), ImVec2( x - gap * 0.5f, y ),
                                      ImVec2( seg_w + gap, steps_h ) ) && i != step ) {
                ask( command_kind::step, i );
            }
            const ImU32 bar = i == step ? mobile_ui::col_accent : i < step ?
                              IM_COL32( 240, 145, 63, 128 ) : IM_COL32( 255, 255, 255, 31 );
            draw->AddRectFilled( ImVec2( x, y + 14.0f * u ), ImVec2( x + seg_w, y + 18.0f * u ), bar,
                                 2.0f * u );
            draw->PushClipRect( ImVec2( x, y ), ImVec2( x + seg_w, y + steps_h ), true );
            mobile_ui::text( draw, i == step ? face::bold : face::body, 11.5f,
                             ImVec2( x, y + 24.0f * u ), i == step ? mobile_ui::col_fg : mobile_ui::col_muted,
                             step_name( i ) );
            draw->PopClipRect();
        }
        y += steps_h;
    }

    // --- bottom bar: Back / Next, or Start game on the last step ---
    const float bottom_h = 68.0f * u + safe.w;
    const float bottom_y = origin.y + size.y - bottom_h;
    {
        draw->AddRectFilled( ImVec2( origin.x, bottom_y ), ImVec2( origin.x + size.x,
                             origin.y + size.y ), col_bottom_bar );
        draw->AddLine( ImVec2( origin.x, bottom_y ), ImVec2( origin.x + size.x, bottom_y ),
                       mobile_ui::col_line, std::max( 1.0f, u ) );
        const float gap = 8.0f * u;
        const float button_w = ( width - 24.0f * u - gap ) * 0.5f;
        const ImVec2 button_size( button_w, 48.0f * u );
        const ImVec2 back_pos( left + 12.0f * u, bottom_y + 10.0f * u );
        const ImVec2 next_pos( back_pos.x + button_w + gap, back_pos.y );
        if( bar_button( "##back", back_pos, button_size, _( "Back" ), false, step > 0 ) ) {
            ask( command_kind::step, step - 1 );
        }
        if( step == last_step ) {
            if( bar_button( "##start", next_pos, button_size, _( "Start game" ), true, points >= 0 ) ) {
                ask( command_kind::next );
            }
        } else if( bar_button( "##next", next_pos, button_size,
                               string_format( _( "Next: %s" ), step_name( step + 1 ) ), true, true ) ) {
            ask( command_kind::step, step + 1 );
        }
    }

    // --- the body of the current step, scrolling ---
    body_width = width;
    ImGui::SetCursorScreenPos( ImVec2( left, y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( ( "##chargen_body" + std::to_string( step ) ).c_str(),
                       ImVec2( width, bottom_y - y ), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    if( step != drawn_step ) {
        // every step opens at its top
        ImGui::SetScrollY( 0.0f );
        drawn_step = step;
    }
    // narrower when the scrollbar shows
    body_width = ImGui::GetContentRegionAvail().x;
    float height = 0.0f;
    switch( state.selected_tab ) {
        case CHARCREATOR_SCENARIO:
            height = draw_scenarios( base );
            break;
        case CHARCREATOR_PROFESSION:
            height = draw_professions( base );
            break;
        case CHARCREATOR_BACKGROUND:
            height = draw_backgrounds( base );
            break;
        case CHARCREATOR_STATS:
            height = draw_stats( base );
            break;
        case CHARCREATOR_TRAITS:
            height = draw_traits( base );
            break;
        case CHARCREATOR_SKILLS:
            height = draw_skills( base );
            break;
        case CHARCREATOR_SUMMARY:
            height = draw_summary( base );
            break;
        default:
            break;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void set_step( character_creator_ui &ccui, const int step )
{
    character_creator_uistate &state = upstream_state();
    const int clamped = std::clamp( step, 0, CHARACTER_CREATOR_TAB_COUNT - 1 );
    state.previous_tab = state.selected_tab;
    state.selected_tab = static_cast<character_creator_tab>( clamped );
    state.switched_tab = state.selected_tab;
    ccui.upon_switching_tab();
}

// Carries out a tap through upstream's creator.
void perform( character_creator_ui &ccui, const command &cmd )
{
    character_creator_uistate &state = upstream_state();
    avatar &you = get_avatar();
    const character_creator_tab shown = state.selected_tab;
    switch( cmd.kind ) {
        case command_kind::step:
            set_step( ccui, cmd.a );
            break;
        case command_kind::confirm: {
            // what upstream does when an entry of the tab's list is confirmed
            uilist picked;
            picked.ret = cmd.b;
            picked.selected = cmd.b;
            state.selected_tab = static_cast<character_creator_tab>( cmd.a );
            character_creator_callback( &ccui ).confirm( &picked );
            break;
        }
        case command_kind::stat:
            state.selected_tab = CHARCREATOR_STATS;
            state.selected_stat_index = cmd.a;
            ccui.handle_action( cmd.b > 0 ? "INCREASE_VALUE" : "DECREASE_VALUE" );
            break;
        case command_kind::skill:
            state.selected_tab = CHARCREATOR_SKILLS;
            state.selected_skill_index = cmd.a;
            ccui.handle_action( cmd.b > 0 ? "INCREASE_VALUE" : "DECREASE_VALUE" );
            break;
        case command_kind::action:
            ccui.handle_action( cmd.text );
            break;
        case command_kind::gender:
            if( you.male != ( cmd.a == 1 ) ) {
                ccui.handle_action( "CHANGE_GENDER" );
            }
            // starting clothes follow the gender; upstream has a separate key for them
            if( upstream_outfit_male() != you.male ) {
                ccui.handle_action( "CHANGE_OUTFIT" );
            }
            break;
        case command_kind::location:
            // as upstream's CHOOSE_LOCATION does with the entry picked from its menu
            if( cmd.a < 0 ) {
                you.random_start_location = true;
            } else {
                for( const start_location &loc : start_locations::get_all() ) {
                    if( loc.id.id().to_i() == cmd.a ) {
                        you.random_start_location = false;
                        you.start_location = loc.id;
                        break;
                    }
                }
            }
            break;
        case command_kind::blood: {
            // upstream's key steps through these eight one at a time
            mobile_ui::sheet_spec spec;
            spec.title = _( "Blood type" );
            spec.grid = true;
            for( int i = 0; i < static_cast<int>( blood_type::blood_acid ); ++i ) {
                for( const char *const rh : {
                         "-", "+"
                     } ) {
                    const std::string name = io::enum_to_string( static_cast<blood_type>( i ) ) + rh;
                    spec.rows.push_back( { "", name, "" } );
                }
            }
            const int picked = mobile_ui::show_sheet( spec );
            if( picked >= 0 && picked < static_cast<int>( spec.rows.size() ) ) {
                you.my_blood_type = static_cast<blood_type>( picked / 2 );
                you.blood_rh_factor = picked % 2 == 1;
            }
            break;
        }
        case command_kind::next:
            // on the last tab this is upstream's "finished?" question and its checks
            state.selected_tab = CHARCREATOR_SUMMARY;
            ccui.handle_action( "NEXT_TAB" );
            break;
        case command_kind::back:
            if( shown == CHARCREATOR_SCENARIO ) {
                ccui.handle_action( "QUIT" );
            } else {
                set_step( ccui, static_cast<int>( shown ) - 1 );
            }
            break;
        case command_kind::leave:
            ccui.handle_action( "QUIT" );
            break;
        case command_kind::none:
            break;
    }
    if( cmd.kind != command_kind::step && cmd.kind != command_kind::back &&
        cmd.kind != command_kind::next ) {
        state.selected_tab = shown;
    }
}

} // namespace

void run( character_creator_ui &ccui )
{
    character_creator_uistate &state = upstream_state();
    // as character_creator_ui::display() starts: fresh state, a list per tab
    state.reset();
    const character_creator_tab first_tab = state.selected_tab;
    for( int i = 0; i < CHARACTER_CREATOR_TAB_COUNT; ++i ) {
        state.selected_tab = static_cast<character_creator_tab>( i );
        ccui.setup_new_uilist();
    }
    state.selected_tab = first_tab;
    state.recalc_scenario_list( get_avatar() );
    ccui.upon_switching_tab();
    ui_manager::invalidate_all_ui_adaptors();
    state.quit_to_main_menu = false;
    state.finished_character_creator = false;

    chargen_window window;
    input_context ctxt( "default" );
    const mobile_prompt::own_controls own_controls( ctxt );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // A window or scrolling area that is new is only laid out in its first
    // frame, so a few frames are drawn without waiting for input after the
    // screen opens and after every change.
    int settle_frames = 3;
    while( !state.finished_character_creator && !state.quit_to_main_menu ) {
        ui_manager::redraw();
        if( window.pending.kind == command_kind::none ) {
            const std::string action = settle_frames > 0 ? ctxt.handle_input( 30 ) : ctxt.handle_input();
            settle_frames = std::max( 0, settle_frames - 1 );
            if( action == "QUIT" ) {
                // the back button goes back one step, and asks before leaving
                window.pending = { command_kind::back, 0, 0, std::string() };
            } else if( action != "TIMEOUT" && window.pending.kind == command_kind::none ) {
                const input_event event = ctxt.get_raw_input();
                if( event.type == input_event_t::keyboard_char ||
                    event.type == input_event_t::keyboard_code ) {
                    // a sideways swipe arrives as an arrow key: the finger moving
                    // left brings the next step in
                    const int key = event.get_first_input();
                    const int step = static_cast<int>( state.selected_tab );
                    if( key == KEY_LEFT && step < CHARACTER_CREATOR_TAB_COUNT - 1 ) {
                        window.pending = { command_kind::step, step + 1, 0, std::string() };
                    } else if( key == KEY_RIGHT && step > 0 ) {
                        window.pending = { command_kind::step, step - 1, 0, std::string() };
                    }
                }
            }
        }
        if( window.pending.kind != command_kind::none ) {
            const command cmd = window.pending;
            window.pending = command();
            perform( ccui, cmd );
            settle_frames = 3;
        }
    }
}

} // namespace mobile_chargen

#else // !TILES

namespace mobile_chargen
{

void run( character_creator_ui & ) {}

} // namespace mobile_chargen

#endif // TILES
