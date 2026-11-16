#include "mobile_crafting.h"

#include "avatar.h"

#if defined(TILES)
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_imgui.h"
#include "color.h"
#include "crafting.h"
#include "crafting_gui.h"
#include "crafting_gui_helpers.h"
#include "display.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "item.h"
#include "itype.h"
#include "mobile_ui.h"
#include "output.h"
#include "proficiency.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "skill.h"
#include "string_formatter.h"
#include "temp_crafting_inventory.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "uistate.h"
#endif

namespace mobile_crafting
{

#if defined(TILES)

using mobile_ui::face;

namespace
{

constexpr ImU32 col_good = IM_COL32( 108, 191, 85, 255 );
constexpr ImU32 col_good_soft = IM_COL32( 108, 191, 85, 51 );
constexpr ImU32 col_can = IM_COL32( 143, 220, 106, 255 );
constexpr ImU32 col_miss = IM_COL32( 255, 138, 106, 255 );
constexpr ImU32 col_needs = IM_COL32( 201, 128, 112, 255 );
constexpr ImU32 col_name_off = IM_COL32( 163, 170, 152, 255 );
constexpr ImU32 col_glyph_off = IM_COL32( 95, 102, 90, 255 );
constexpr ImU32 col_glyph_bg = IM_COL32( 5, 6, 5, 255 );
constexpr ImU32 col_hair = IM_COL32( 255, 255, 255, 13 );
constexpr ImU32 col_field = IM_COL32( 9, 11, 8, 255 );
constexpr ImU32 col_chip_text = IM_COL32( 195, 201, 185, 255 );
constexpr ImU32 col_desc = IM_COL32( 201, 207, 191, 255 );

constexpr int max_batch = 50;

// One line of the requirements list: a group of alternatives, of which one is enough.
struct req_line {
    // the alternative the character has, or else the first one
    std::string text;
    // the other alternatives, if any
    std::string others;
    // "have 2 / need 1"
    std::string status;
    bool have = false;
};

struct req_group {
    std::string title;
    std::vector<req_line> lines;
};

// What a row of the list shows.  Built when the row first comes into view.
struct row_info {
    std::string name;
    std::string glyph;
    ImU32 glyph_color = mobile_ui::col_fg;
    std::string status;
    std::string time;
    std::string skill;
};

// What the detail pane shows for one recipe and batch size.
struct detail_info {
    const recipe *rec = nullptr;
    int batch = 0;
    bool can_craft = false;
    std::string status;
    std::string description;
    std::vector<std::pair<std::string, std::string>> facts;
    std::vector<req_group> groups;
};

struct chip {
    crafting_category_id id;
    std::string label;
};

std::string ellipsize( const face f, const float size, std::string text, const float max_width )
{
    if( mobile_ui::text_size( f, size, text ).x <= max_width ) {
        return text;
    }
    const std::string dots = "…";
    while( !text.empty() ) {
        // drop one UTF-8 character
        do {
            text.pop_back();
        } while( !text.empty() && ( static_cast<unsigned char>( text.back() ) & 0xC0 ) == 0x80 );
        while( !text.empty() && text.back() == ' ' ) {
            text.pop_back();
        }
        if( mobile_ui::text_size( f, size, text + dots ).x <= max_width ) {
            break;
        }
    }
    return text + dots;
}

std::string amount_text( const int have, const int need )
{
    if( have == item::INFINITE_CHARGES ) {
        //~ Requirement line of the crafting screen. %d: amount needed
        return string_format( _( "have plenty / need %d" ), need );
    }
    //~ Requirement line of the crafting screen. %1$d: amount at hand, %2$d: amount needed
    return string_format( _( "have %1$d / need %2$d" ), have, need );
}

std::string other_names( const std::vector<std::string> &names )
{
    if( names.empty() ) {
        return std::string();
    }
    std::string ret;
    const size_t shown = std::min<size_t>( names.size(), 3 );
    for( size_t i = 0; i < shown; ++i ) {
        ret += ( i == 0 ? "" : ", " ) + names[i];
    }
    if( names.size() > shown ) {
        //~ End of a list of alternative crafting components. %d: how many more there are
        ret += string_format( _( ", +%d more" ), static_cast<int>( names.size() - shown ) );
    }
    //~ Alternatives to a crafting requirement. %s: a list of items
    return string_format( _( "or %s" ), ret );
}

std::string category_label( const crafting_category_id &id )
{
    // upstream names its tabs this way: the id without "CC_", translated
    const std::string raw = id.str().size() > 3 ? id.str().substr( 3 ) : id.str();
    const std::string label = _( raw );
    if( label != raw ) {
        return label;
    }
    return uppercase_first_letter( to_lower_case( label ) );
}

// The requirement groups of a recipe as lines.  Whether a line is met is
// decided by upstream's has(); the numbers are what the crafting inventory holds.
std::vector<req_group> requirement_lines( const Character &you, const recipe &rec,
        const availability &avail, const int batch )
{
    std::vector<req_group> groups;
    const temp_crafting_inventory &inv = you.crafting_inventory();
    const std::function<bool( const item & )> filter = rec.get_component_filter();
    const craft_flags flags = craft_flags::start_only;

    req_group skills;
    skills.title = _( "Skills" );
    if( rec.skill_used ) {
        req_line line;
        line.text = rec.skill_used->name();
        const int need = rec.get_difficulty( you );
        const int level = static_cast<int>( you.get_skill_level( rec.skill_used ) );
        line.have = avail.crafter_has_primary_skill && ( !rec.is_practice() || avail.has_all_skills );
        //~ Skill line of the crafting screen. %1$d: the character's level, %2$d: recipe difficulty
        line.status = string_format( _( "level %1$d / need %2$d" ), level, need );
        if( need > 0 || !line.have ) {
            skills.lines.push_back( line );
        }
    }
    for( const std::pair<const skill_id, int> &extra : rec.required_skills ) {
        req_line line;
        line.text = extra.first->name();
        const int level = static_cast<int>( you.get_skill_level( extra.first ) );
        line.have = level >= extra.second;
        line.status = string_format( _( "level %1$d / need %2$d" ), level, extra.second );
        skills.lines.push_back( line );
    }
    for( const proficiency_id &prof : rec.required_proficiencies() ) {
        req_line line;
        line.text = prof->name();
        line.have = you.has_proficiency( prof );
        line.status = line.have ? _( "have" ) : _( "missing" );
        skills.lines.push_back( line );
    }
    if( !skills.lines.empty() ) {
        groups.push_back( skills );
    }

    const requirement_data &req = rec.simple_requirements();

    req_group tools;
    tools.title = _( "Tools" );
    for( const std::vector<quality_requirement> &alts : req.get_qualities() ) {
        if( alts.empty() ) {
            continue;
        }
        const quality_requirement *best = nullptr;
        std::vector<std::string> others;
        req_line line;
        for( const quality_requirement &qr : alts ) {
            if( !line.have && qr.has( &you, inv, filter, batch, flags ) ) {
                line.have = true;
                best = &qr;
            }
        }
        if( best == nullptr ) {
            best = &alts.front();
        }
        const auto describe = []( const quality_requirement & qr ) {
            if( qr.count > 1 ) {
                //~ Tool quality needed for a recipe. %1$d: number of tools, %2$s: quality such as "cutting", %3$d: level
                return string_format( _( "%1$d tools with %2$s %3$d" ), qr.count, qr.type->name, qr.level );
            }
            //~ Tool quality needed for a recipe. %1$s: quality such as "cutting", %2$d: level
            return string_format( _( "%1$s %2$d" ), qr.type->name, qr.level );
        };
        for( const quality_requirement &qr : alts ) {
            if( &qr != best ) {
                others.push_back( describe( qr ) );
            }
        }
        line.text = describe( *best );
        line.others = other_names( others );
        line.status = line.have ? _( "have" ) : _( "missing" );
        tools.lines.push_back( line );
    }
    for( const std::vector<tool_comp> &alts : req.get_tools() ) {
        if( alts.empty() ) {
            continue;
        }
        const tool_comp *best = nullptr;
        req_line line;
        for( const tool_comp &tc : alts ) {
            if( tc.has( &you, inv, filter, batch, flags ) ) {
                line.have = true;
                best = &tc;
                break;
            }
        }
        if( best == nullptr ) {
            best = &alts.front();
        }
        std::vector<std::string> others;
        for( const tool_comp &tc : alts ) {
            if( &tc != best && tc.type != best->type ) {
                others.push_back( item::nname( tc.type ) );
            }
        }
        const std::string &group_name = find_tool_group_name( alts );
        line.text = best->to_string( batch );
        if( !line.have && !group_name.empty() ) {
            line.text = group_name;
            others.insert( others.begin(), item::nname( best->type ) );
            //~ Tools that satisfy a named tool requirement. %s: a list of items
            line.others = string_format( _( "such as %s" ), other_names( others ).substr( 3 ) );
        } else {
            line.others = other_names( others );
        }
        if( best->by_charges() ) {
            const int factor = std::max( 1, item::find_type( best->type )->charge_factor() );
            const int need = best->count * batch;
            int have = inv.charges_of( best->type, INT_MAX, filter );
            if( have != item::INFINITE_CHARGES ) {
                have /= factor;
            }
            //~ Tool charges for a recipe. %1$d: charges at hand, %2$d: charges needed
            line.status = have == item::INFINITE_CHARGES ? amount_text( have, need ) :
                          string_format( _( "have %1$d / need %2$d charges" ), have, need );
        } else {
            const int need = std::max( 1, std::abs( best->count ) );
            int have = inv.amount_of( best->type, true, INT_MAX, filter );
            if( line.have ) {
                // substitutes and the like count as the tool itself
                have = std::max( have, need );
            }
            line.status = amount_text( have, need );
        }
        tools.lines.push_back( line );
    }
    if( !tools.lines.empty() ) {
        groups.push_back( tools );
    }

    req_group components;
    components.title = _( "Components" );
    const bool full_magazine = rec.has_flag( "NEED_FULL_MAGAZINE" );
    for( const std::vector<item_comp> &alts : req.get_components() ) {
        if( alts.empty() ) {
            continue;
        }
        const auto at_hand = [&inv, &filter]( const item_comp & ic ) {
            if( item::count_by_charges( ic.type ) ) {
                return inv.charges_of( ic.type, INT_MAX, filter );
            }
            return inv.amount_of( ic.type, false, INT_MAX, filter );
        };
        // the one the character has enough of, or else the one they have some of
        const item_comp *best = nullptr;
        int best_have = 0;
        req_line line;
        for( const item_comp &ic : alts ) {
            if( ic.has( &you, inv, filter, batch, flags ) ) {
                line.have = true;
                best = &ic;
                best_have = at_hand( ic );
                break;
            }
        }
        if( best == nullptr ) {
            best = &alts.front();
            for( const item_comp &ic : alts ) {
                const int have = at_hand( ic );
                if( have > best_have ) {
                    best = &ic;
                    best_have = have;
                }
            }
        }
        const int need = std::abs( best->count ) * batch;
        if( line.have && best_have != item::INFINITE_CHARGES ) {
            best_have = std::max( best_have, need );
        }
        line.text = item::nname( best->type, need );
        if( full_magazine && item( best->type ).is_magazine() ) {
            //~ Appended to the name of a battery or magazine: it must be completely charged or loaded.
            line.text += _( " (full)" );
        }
        std::vector<std::string> others;
        for( const item_comp &ic : alts ) {
            if( &ic != best ) {
                others.push_back( item::nname( ic.type, std::abs( ic.count ) * batch ) );
            }
        }
        line.others = other_names( others );
        line.status = amount_text( best_have, need );
        components.lines.push_back( line );
    }
    if( !components.lines.empty() ) {
        groups.push_back( components );
    }
    return groups;
}

class crafting_window : public cataimgui::window
{
    public:
        explicit crafting_window( avatar &you );

        // the recipe to craft, once Craft was tapped
        const recipe *chosen = nullptr;
        int batch = 1;
        bool close = false;
        // something changed after it was drawn: draw again soon
        bool refresh = false;

        // Goes back one step.  Returns false if there is nothing left but to close.
        bool back();
        void scroll_chips( int direction );
        bool typing() const {
            return search_active;
        }
        void stop_typing() {
            drop_focus = true;
            refresh = true;
        }

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        avatar &you;
        const recipe_subset *known = nullptr;
        std::map<const recipe *, availability> availability_cache;
        std::map<const recipe *, row_info> row_cache;
        std::vector<chip> chips;
        // index into chips, or -1 for all
        int category = -1;
        bool only_craftable = false;
        std::string query;
        std::string applied_query;
        bool search_active = false;
        bool drop_focus = false;
        float chip_scroll_by = 0.0f;

        bool list_dirty = true;
        bool list_to_top = false;
        recipe_list_data list;
        // indices into list of the rows that are shown
        std::vector<int> rows;

        const recipe *selected = nullptr;
        bool sheet_open = false;
        float sheet_content_height = 0.0f;
        std::optional<availability> detail_availability;
        detail_info detail;

        void rebuild_list();
        const row_info &info_for( const recipe &rec, const availability &avail );
        void update_detail();
        void select( const recipe *rec );

        void draw_left( const ImVec2 &pos, const ImVec2 &size, bool interactive );
        void draw_search( const ImVec2 &pos, float width, bool interactive );
        void draw_chips( const ImVec2 &pos, float width, bool interactive );
        void draw_list( const ImVec2 &pos, const ImVec2 &size, bool interactive );
        // Draws the detail of the selected recipe; returns its height.
        float draw_detail( const ImVec2 &pos, float width );
        void draw_glyph( ImDrawList *draw, const ImVec2 &pos, float box, float font_size,
                         const std::string &glyph, ImU32 color ) const;
        void draw_sheet( const ImVec2 &origin, const ImVec2 &size );
};

crafting_window::crafting_window( avatar &you ) : cataimgui::window( "MOBILE_CRAFTING",
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ), you( you )
{
    known = &you.get_group_available_recipes();
    for( const crafting_category &cat : get_crafting_categories() ) {
        if( !cat.is_hidden && !cat.is_wildcard && !known->in_category( cat.id ).empty() ) {
            chips.push_back( { cat.id, category_label( cat.id ) } );
        }
    }
}

bool crafting_window::back()
{
    refresh = true;
    if( sheet_open ) {
        sheet_open = false;
        return true;
    }
    if( selected != nullptr ) {
        selected = nullptr;
        return true;
    }
    if( !query.empty() ) {
        query.clear();
        return true;
    }
    return false;
}

void crafting_window::scroll_chips( const int direction )
{
    chip_scroll_by += static_cast<float>( direction ) * 220.0f * mobile_ui::unit();
    refresh = true;
}

void crafting_window::rebuild_list()
{
    list_dirty = false;
    applied_query = query;
    std::vector<const recipe *> picking;
    const std::string trimmed = trim( query );
    const auto in_category = [this]( const recipe * rec ) {
        if( category >= 0 ) {
            return rec->category == chips[category].id;
        }
        return std::any_of( chips.begin(), chips.end(), [rec]( const chip & c ) {
            return c.id == rec->category;
        } );
    };
    if( !trimmed.empty() ) {
        // upstream's search, with its prefixes; narrowed to the chosen category
        const recipe_subset found = filter_recipes( *known, trimmed, you, []( size_t, size_t ) {} );
        for( const recipe *rec : found ) {
            if( in_category( rec ) ) {
                picking.push_back( rec );
            }
        }
    } else if( category >= 0 ) {
        picking = known->in_category( chips[category].id );
    } else {
        for( const chip &c : chips ) {
            const std::vector<const recipe *> some = known->in_category( c.id );
            picking.insert( picking.end(), some.begin(), some.end() );
        }
    }
    // upstream's list: hidden recipes dropped, what can be made first, groups expanded
    list = build_recipe_list( std::move( picking ), false, false, you, false, nullptr, false, false,
                              availability_cache, *known );
    rows.clear();
    for( int i = 0; i < static_cast<int>( list.entries.size() ); ++i ) {
        if( !only_craftable || list.available[i].can_craft_recipe ) {
            rows.push_back( i );
        }
    }
}

const row_info &crafting_window::info_for( const recipe &rec, const availability &avail )
{
    const auto found = row_cache.find( &rec );
    if( found != row_cache.end() ) {
        return found->second;
    }
    row_info info;
    info.name = remove_color_tags( rec.result_name( true ) );
    if( rec.is_nested() ) {
        info.glyph = "+";
        info.glyph_color = mobile_ui::col_accent;
        //~ A group of recipes in the crafting list. %d: number of recipes in it
        info.status = string_format( n_gettext( "Group of %d recipe", "Group of %d recipes",
                                                rec.nested_category_data.size() ),
                                     static_cast<int>( rec.nested_category_data.size() ) );
        return row_cache.emplace( &rec, info ).first->second;
    }
    if( rec.result().is_valid() && !rec.result().is_null() ) {
        const itype *const type = item::find_type( rec.result() );
        info.glyph = type->sym;
        info.glyph_color = ImGui::ColorConvertFloat4ToU32( cataimgui::imvec4_from_color( type->color ) );
    } else {
        info.glyph = rec.is_practice() ? "*" : "&";
    }
    info.time = approx_craft_time( static_cast<int>( you.expected_time_to_craft( rec, 1 ) /
                                   to_moves<int>( 1_turns ) ) );
    if( rec.skill_used ) {
        info.skill = string_format( "%s %d", rec.skill_used->name(), rec.get_difficulty( you ) );
    }
    if( avail.can_craft_recipe && avail.crafter_has_primary_skill ) {
        info.status = rec.is_practice() ? _( "Can practice" ) : _( "Can craft" );
    } else {
        std::vector<std::string> missing;
        for( const req_group &group : requirement_lines( you, rec, avail, 1 ) ) {
            for( const req_line &line : group.lines ) {
                if( !line.have ) {
                    missing.push_back( line.text );
                }
            }
        }
        if( missing.empty() ) {
            info.status = _( "Can't craft yet" );
        } else {
            std::string names = missing[0];
            if( missing.size() > 1 ) {
                names += ", " + missing[1];
            }
            if( missing.size() > 2 ) {
                names += "…";
            }
            //~ Why a recipe cannot be crafted. %s: one or two things that are missing
            info.status = string_format( _( "Needs %s" ), names );
        }
    }
    return row_cache.emplace( &rec, info ).first->second;
}

void crafting_window::select( const recipe *const rec )
{
    selected = rec;
    batch = 1;
    sheet_open = rec != nullptr;
    sheet_content_height = 0.0f;
    refresh = true;
}

void crafting_window::update_detail()
{
    if( selected == nullptr || ( detail.rec == selected && detail.batch == batch ) ) {
        return;
    }
    const recipe &rec = *selected;
    detail = detail_info();
    detail.rec = selected;
    detail.batch = batch;
    detail_availability.emplace( you, selected, batch );
    const availability &avail = *detail_availability;
    const craft_confirm_result confirm = can_start_craft( rec, avail, you, batch );
    detail.can_craft = confirm == craft_confirm_result::ok;
    detail.groups = requirement_lines( you, rec, avail, batch );
    bool lines_missing = false;
    for( const req_group &group : detail.groups ) {
        for( const req_line &line : group.lines ) {
            lines_missing = lines_missing || !line.have;
        }
    }
    switch( confirm ) {
        case craft_confirm_result::ok:
            detail.status = rec.is_practice() ? _( "You can practice this" ) : _( "You can craft this" );
            break;
        case craft_confirm_result::too_many_results:
            detail.status = _( "That batch makes too many items" );
            break;
        case craft_confirm_result::too_dark:
            detail.status = _( "It is too dark to craft" );
            break;
        case craft_confirm_result::cannot_craft:
            if( !avail.crafter_has_primary_skill ) {
                detail.status = _( "Your skill is too low" );
            } else if( !avail.has_proficiencies ) {
                detail.status = _( "Missing a proficiency" );
            } else if( lines_missing ) {
                detail.status = _( "Missing materials or tools" );
            } else {
                detail.status = _( "You can't craft this right now" );
            }
            break;
    }
    if( rec.is_practice() ) {
        detail.description = practice_recipe_description( rec, you );
    } else if( !rec.description.empty() ) {
        detail.description = rec.get_description( you );
    } else if( rec.result().is_valid() && !rec.result().is_null() ) {
        detail.description = item::find_type( rec.result() )->description.translated();
    }
    detail.description = remove_color_tags( detail.description );

    const int turns = static_cast<int>( you.expected_time_to_craft( rec, batch ) /
                                        to_moves<int>( 1_turns ) );
    detail.facts.emplace_back( _( "Time" ), approx_craft_time( turns ) );
    detail.facts.emplace_back( _( "Skill" ), rec.skill_used ?
                               string_format( "%s %d", rec.skill_used->name(), rec.get_difficulty( you ) ) :
                               //~ The recipe uses no skill
                               std::string( _( "None" ) ) );
    detail.facts.emplace_back( _( "Category" ), category_label( rec.category ) );
    if( !rec.is_practice() ) {
        detail.facts.emplace_back( _( "Makes" ), std::to_string( rec.makes_amount() * batch ) );
    }
    detail.facts.emplace_back( _( "Success" ), string_format( "~%.0f%%",
                               100.0f * you.recipe_success_chance( rec ) ) );
    detail.facts.emplace_back( _( "Activity" ), display::activity_level_str( rec.exertion_level() ) );
}

void crafting_window::draw_glyph( ImDrawList *const draw, const ImVec2 &pos, const float box,
                                  const float font_size, const std::string &glyph, const ImU32 color ) const
{
    const float u = mobile_ui::unit();
    mobile_ui::box( draw, pos, ImVec2( pos.x + box * u, pos.y + box * u ), col_glyph_bg,
                    IM_COL32( 255, 255, 255, 15 ), box * 0.25f );
    const ImVec2 glyph_size = mobile_ui::text_size( face::mono, font_size, glyph );
    mobile_ui::text( draw, face::mono, font_size, ImVec2( pos.x + ( box * u - glyph_size.x ) * 0.5f,
                     pos.y + ( box * u - glyph_size.y ) * 0.5f ), color, glyph );
}

void crafting_window::draw_search( const ImVec2 &pos, const float width, const bool interactive )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 field( pos.x + 12.0f * u, pos.y + 10.0f * u );
    const ImVec2 field_max( pos.x + width - 12.0f * u, field.y + 44.0f * u );
    mobile_ui::box( draw, field, field_max, col_field, mobile_ui::col_line, 11.0f );
    mobile_ui::icon( draw, "search", ImVec2( field.x + 12.0f * u, field.y + 13.0f * u ), 18.0f,
                     mobile_ui::col_muted );
    const float clear_w = query.empty() ? 0.0f : 44.0f * u;
    const float text_h = 15.0f * u;
    const float pad_y = std::floor( ( 44.0f * u - text_h ) * 0.5f - 2.0f * u );
    if( !interactive ) {
        mobile_ui::text( draw, face::body, 15.0f, ImVec2( field.x + 38.0f * u, field.y + 13.0f * u ),
                         query.empty() ? mobile_ui::col_muted : mobile_ui::col_fg,
                         query.empty() ? _( "Search recipes" ) : query );
        return;
    }
    if( drop_focus ) {
        ImGui::ClearActiveID();
        drop_focus = false;
    }
    ImGui::SetCursorScreenPos( ImVec2( field.x + 38.0f * u, field.y ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
    ImGui::PushStyleColor( ImGuiCol_Text, mobile_ui::col_fg );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, mobile_ui::col_muted );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, pad_y ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
    ImGui::PushFont( nullptr, text_h / std::max( 0.1f, ImGui::GetStyle().FontScaleMain *
                     ImGui::GetStyle().FontScaleDpi ) );
    ImGui::SetNextItemWidth( field_max.x - field.x - 38.0f * u - clear_w - 6.0f * u );
    ImGui::InputTextWithHint( "##craft_search", _( "Search recipes" ), &query );
    search_active = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleVar( 2 );
    ImGui::PopStyleColor( 3 );
    if( search_active ) {
        draw->AddRect( field, field_max, mobile_ui::col_accent, 11.0f * u, 0, 2.0f * u );
    }
    if( !query.empty() ) {
        const ImVec2 clear_pos( field_max.x - 44.0f * u, field.y );
        if( mobile_ui::pressable( "##craft_search_clear", clear_pos, ImVec2( 44.0f * u, 44.0f * u ) ) ) {
            query.clear();
            drop_focus = true;
        }
        mobile_ui::icon( draw, "x", ImVec2( clear_pos.x + 13.0f * u, clear_pos.y + 13.0f * u ), 18.0f,
                         mobile_ui::col_muted );
    }
    if( query != applied_query ) {
        list_dirty = true;
        list_to_top = true;
        refresh = true;
    }
}

void crafting_window::draw_chips( const ImVec2 &pos, const float width, const bool interactive )
{
    const float u = mobile_ui::unit();
    const float row_h = 50.0f * u;
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##craft_chips", ImVec2( width, row_h ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar );
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    float x = base.x + 12.0f * u;
    const auto draw_chip = [&]( const std::string & id, const std::string & label, const bool on,
    const bool toggle ) {
        const float chip_w = mobile_ui::text_size( face::bold, 13.0f, label ).x + 26.0f * u;
        const ImVec2 chip_pos( x, base.y + 10.0f * u );
        const ImVec2 chip_max( x + chip_w, chip_pos.y + 30.0f * u );
        bool held = false;
        // the whole height of the row takes the tap
        const bool tapped = interactive &&
                            mobile_ui::pressable( id, ImVec2( x - 3.0f * u, base.y ), ImVec2( chip_w + 6.0f * u, row_h ),
                                    &held );
        ImU32 fill = held ? mobile_ui::col_button_down : IM_COL32( 255, 255, 255, 10 );
        ImU32 border = mobile_ui::col_line;
        ImU32 ink = col_chip_text;
        if( on && toggle ) {
            fill = col_good_soft;
            border = col_good;
            ink = col_good;
        } else if( on ) {
            fill = mobile_ui::col_accent;
            border = mobile_ui::col_accent;
            ink = mobile_ui::col_on_accent;
        }
        mobile_ui::box( draw, chip_pos, chip_max, fill, border, 15.0f );
        const ImVec2 label_size = mobile_ui::text_size( face::bold, 13.0f, label );
        mobile_ui::text( draw, face::bold, 13.0f, ImVec2( chip_pos.x + 13.0f * u,
                         chip_pos.y + ( 30.0f * u - label_size.y ) * 0.5f ), ink, label );
        x += chip_w + 6.0f * u;
        return tapped;
    };
    if( draw_chip( "##chip_can", _( "Can craft" ), only_craftable, true ) ) {
        only_craftable = !only_craftable;
        list_dirty = true;
        list_to_top = true;
    }
    //~ The crafting category chip that shows every category
    if( draw_chip( "##chip_all", _( "All" ), category < 0, false ) ) {
        category = -1;
        list_dirty = true;
        list_to_top = true;
    }
    for( int i = 0; i < static_cast<int>( chips.size() ); ++i ) {
        if( draw_chip( "##chip" + std::to_string( i ), chips[i].label, category == i, false ) ) {
            category = i;
            list_dirty = true;
            list_to_top = true;
        }
    }
    if( list_dirty ) {
        refresh = true;
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( x + 6.0f * u - base.x, 1.0f ) );
    // sideways swipes arrive as arrow keys; the mouse wheel works on desktop
    if( ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheelH != 0.0f ) {
        chip_scroll_by -= ImGui::GetIO().MouseWheelH * 60.0f * u;
    }
    if( chip_scroll_by != 0.0f ) {
        ImGui::SetScrollX( std::clamp( ImGui::GetScrollX() + chip_scroll_by, 0.0f,
                                       std::max( 0.0f, x + 6.0f * u - base.x - width ) ) );
        chip_scroll_by = 0.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void crafting_window::draw_list( const ImVec2 &pos, const ImVec2 &size, const bool interactive )
{
    const float u = mobile_ui::unit();
    const float row_h = 56.0f * u;
    ImGui::SetCursorScreenPos( pos );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##craft_list", size, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                       ( interactive ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoInputs ) );
    if( list_to_top ) {
        ImGui::SetScrollY( 0.0f );
        list_to_top = false;
    }
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const int count = static_cast<int>( rows.size() );
    if( count == 0 ) {
        const std::string none = _( "No recipes match." );
        const ImVec2 none_size = mobile_ui::text_size( face::body, 13.5f, none );
        mobile_ui::text( draw, face::body, 13.5f, ImVec2( pos.x + ( size.x - none_size.x ) * 0.5f,
                         pos.y + 28.0f * u ), mobile_ui::col_muted, none );
    }
    // only the rows in view are laid out
    const int first = std::max( 0, static_cast<int>( ( pos.y - base.y ) / row_h ) );
    const int last = std::min( count, static_cast<int>( ( pos.y + size.y - base.y ) / row_h ) + 1 );
    const recipe *tapped_recipe = nullptr;
    for( int r = first; r < last; ++r ) {
        const int index = rows[r];
        const recipe &rec = *list.entries[index];
        const availability &avail = list.available[index];
        const row_info &info = info_for( rec, avail );
        const bool ok = avail.can_craft_recipe && ( rec.is_nested() || avail.crafter_has_primary_skill );
        const ImVec2 row_pos( base.x, base.y + static_cast<float>( r ) * row_h );
        const ImVec2 row_max( row_pos.x + size.x, row_pos.y + row_h );
        bool held = false;
        if( interactive && mobile_ui::pressable( "##recipe" + std::to_string( r ), row_pos,
                ImVec2( size.x, row_h ), &held ) ) {
            tapped_recipe = &rec;
        }
        if( selected == &rec ) {
            draw->AddRectFilled( row_pos, row_max, IM_COL32( 240, 145, 63, 31 ) );
        } else if( held ) {
            draw->AddRectFilled( row_pos, row_max, col_hair );
        }
        draw->AddLine( ImVec2( row_pos.x, row_max.y ), row_max, col_hair, std::max( 1.0f, u ) );

        const float indent = static_cast<float>( list.indent[index] ) * 16.0f * u;
        const ImVec2 glyph_pos( row_pos.x + 14.0f * u + indent, row_pos.y + 10.0f * u );
        const bool expanded = rec.is_nested() &&
                              uistate.expanded_recipes.count( rec.ident() ) > 0;
        draw_glyph( draw, glyph_pos, 36.0f, 19.0f, expanded ? "-" : info.glyph,
                    ok || rec.is_nested() ? info.glyph_color : col_glyph_off );

        // time above skill, on the right
        float meta_w = 0.0f;
        if( !info.time.empty() ) {
            const ImVec2 time_size = mobile_ui::text_size( face::mono, 11.0f, info.time );
            const ImVec2 skill_size = mobile_ui::text_size( face::mono, 11.0f, info.skill );
            meta_w = std::max( time_size.x, skill_size.x );
            const float line_h = 15.0f * u;
            const float top = row_pos.y + ( row_h - ( info.skill.empty() ? line_h : 2.0f * line_h ) ) * 0.5f;
            mobile_ui::text( draw, face::mono, 11.0f, ImVec2( row_max.x - 14.0f * u - time_size.x, top ),
                             mobile_ui::col_muted, info.time );
            if( !info.skill.empty() ) {
                mobile_ui::text( draw, face::mono, 11.0f,
                                 ImVec2( row_max.x - 14.0f * u - skill_size.x, top + line_h ),
                                 mobile_ui::col_muted, info.skill );
            }
        }
        const float text_x = glyph_pos.x + 48.0f * u;
        const float text_w = row_max.x - 14.0f * u - meta_w - ( meta_w > 0.0f ? 12.0f * u : 0.0f ) - text_x;
        mobile_ui::text( draw, face::bold, 14.5f, ImVec2( text_x, row_pos.y + 10.0f * u ),
                         ok ? mobile_ui::col_fg : col_name_off,
                         ellipsize( face::bold, 14.5f, info.name, text_w ) );
        const ImU32 status_color = rec.is_nested() ? mobile_ui::col_muted : ( ok ? col_can : col_needs );
        mobile_ui::text( draw, face::body, 12.0f, ImVec2( text_x, row_pos.y + 30.0f * u ), status_color,
                         ellipsize( face::body, 12.0f, info.status, text_w ) );
    }
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, static_cast<float>( count ) * row_h ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if( tapped_recipe != nullptr ) {
        if( tapped_recipe->is_nested() ) {
            // open or close the group, as upstream's menu does
            const recipe_id id = tapped_recipe->ident();
            if( uistate.expanded_recipes.erase( id ) == 0 ) {
                uistate.expanded_recipes.insert( id );
            }
            list_dirty = true;
            refresh = true;
        } else {
            select( tapped_recipe );
        }
    }
}

void crafting_window::draw_left( const ImVec2 &pos, const ImVec2 &size, const bool interactive )
{
    const float u = mobile_ui::unit();
    float bar_h = 0.0f;
    if( mobile_ui::app_bar( _( "Crafting" ), pos, size.x, &bar_h ) ) {
        if( interactive ) {
            close = true;
        } else {
            sheet_open = false;
        }
        refresh = true;
    }
    float y = pos.y + bar_h;
    draw_search( ImVec2( pos.x, y ), size.x, interactive );
    y += 54.0f * u;
    draw_chips( ImVec2( pos.x, y ), size.x, interactive );
    y += 50.0f * u;
    if( list_dirty ) {
        rebuild_list();
    }
    draw_list( ImVec2( pos.x, y ), ImVec2( size.x, pos.y + size.y - y ), interactive );
}

float crafting_window::draw_detail( const ImVec2 &pos, const float width )
{
    const float u = mobile_ui::unit();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    if( selected == nullptr ) {
        const std::string hint = _( "Select a recipe to see what it needs." );
        const float wrap = width - 40.0f * u;
        const ImVec2 hint_size = mobile_ui::text_size( face::body, 13.5f, hint, wrap );
        mobile_ui::text( draw, face::body, 13.5f, ImVec2( pos.x + ( width - hint_size.x ) * 0.5f,
                         pos.y + 28.0f * u ), mobile_ui::col_muted, hint, wrap );
        return 56.0f * u + hint_size.y;
    }
    update_detail();
    const recipe &rec = *selected;
    const availability &avail = *detail_availability;
    const row_info &info = info_for( rec, avail );
    const float pad = 16.0f * u;
    const float inner_w = width - 2.0f * pad;
    float y = pos.y + 4.0f * u;

    // head: glyph, name, whether it can be made
    draw_glyph( draw, ImVec2( pos.x + pad, y ), 46.0f, 24.0f, info.glyph, info.glyph_color );
    const float head_x = pos.x + pad + 58.0f * u;
    const float head_w = pos.x + width - pad - head_x;
    const ImVec2 name_size = mobile_ui::text_size( face::display, 20.0f, info.name, head_w );
    const ImVec2 status_size = mobile_ui::text_size( face::body, 12.5f, detail.status, head_w );
    const float head_h = std::max( 46.0f * u, name_size.y + 3.0f * u + status_size.y );
    const float head_top = y + ( head_h - name_size.y - 3.0f * u - status_size.y ) * 0.5f;
    mobile_ui::text( draw, face::display, 20.0f, ImVec2( head_x, head_top ), mobile_ui::col_fg,
                     info.name, head_w );
    mobile_ui::text( draw, face::body, 12.5f, ImVec2( head_x, head_top + name_size.y + 3.0f * u ),
                     detail.can_craft ? col_can : col_miss, detail.status, head_w );
    y += head_h + 10.0f * u;

    if( !detail.description.empty() ) {
        mobile_ui::text( draw, face::body, 13.5f, ImVec2( pos.x + pad, y ), col_desc, detail.description,
                         inner_w );
        y += mobile_ui::text_size( face::body, 13.5f, detail.description, inner_w ).y + 12.0f * u;
    }

    // fact tiles, three to a row
    const float gap = 6.0f * u;
    const float tile_w = ( inner_w - 2.0f * gap ) / 3.0f;
    const float tile_h = 44.0f * u;
    for( size_t i = 0; i < detail.facts.size(); ++i ) {
        const ImVec2 tile( pos.x + pad + static_cast<float>( i % 3 ) * ( tile_w + gap ),
                           y + static_cast<float>( i / 3 ) * ( tile_h + gap ) );
        mobile_ui::box( draw, tile, ImVec2( tile.x + tile_w, tile.y + tile_h ), col_hair, 0, 9.0f );
        mobile_ui::text( draw, face::mono, 9.5f, ImVec2( tile.x + 9.0f * u, tile.y + 7.0f * u ),
                         mobile_ui::col_muted, to_upper_case( detail.facts[i].first ) );
        mobile_ui::text( draw, face::bold, 13.5f, ImVec2( tile.x + 9.0f * u, tile.y + 21.0f * u ),
                         mobile_ui::col_fg,
                         ellipsize( face::bold, 13.5f, detail.facts[i].second, tile_w - 18.0f * u ) );
    }
    y += static_cast<float>( ( detail.facts.size() + 2 ) / 3 ) * ( tile_h + gap ) - gap + 12.0f * u;

    // requirements: one line for each group of alternatives
    for( const req_group &group : detail.groups ) {
        mobile_ui::text( draw, face::mono, 10.5f, ImVec2( pos.x + pad, y ), mobile_ui::col_muted,
                         to_upper_case( group.title ) );
        y += 15.0f * u;
        for( const req_line &line : group.lines ) {
            const ImU32 mark = line.have ? col_good : col_miss;
            const ImVec2 status_px = mobile_ui::text_size( face::mono, 11.0f, line.status );
            const float text_x = pos.x + pad + 24.0f * u;
            const float text_w = pos.x + width - pad - status_px.x - 8.0f * u - text_x;
            const ImVec2 text_px = mobile_ui::text_size( face::body, 13.5f, line.text, text_w );
            const ImVec2 others_px = line.others.empty() ? ImVec2( 0.0f, 0.0f ) :
                                     mobile_ui::text_size( face::body, 11.5f, line.others, text_w );
            mobile_ui::icon( draw, line.have ? "check" : "x", ImVec2( pos.x + pad, y + 7.0f * u ), 16.0f,
                             mark );
            mobile_ui::text( draw, face::body, 13.5f, ImVec2( text_x, y + 6.0f * u ), mobile_ui::col_fg,
                             line.text, text_w );
            if( !line.others.empty() ) {
                mobile_ui::text( draw, face::body, 11.5f, ImVec2( text_x, y + 6.0f * u + text_px.y ),
                                 mobile_ui::col_muted, line.others, text_w );
            }
            mobile_ui::text( draw, face::mono, 11.0f,
                             ImVec2( pos.x + width - pad - status_px.x, y + 8.0f * u ), mark, line.status );
            y += 12.0f * u + text_px.y + others_px.y;
            draw->AddLine( ImVec2( pos.x + pad, y ), ImVec2( pos.x + width - pad, y ), col_hair,
                           std::max( 1.0f, u ) );
            y += 2.0f * u;
        }
        y += 8.0f * u;
    }

    // batch stepper and the Craft button
    y += 6.0f * u;
    float x = pos.x + pad;
    if( detail.can_craft || batch > 1 ) {
        const auto step_button = [&]( const std::string & id, const std::string & icon,
        const bool enabled ) {
            const ImVec2 button( x, y + 3.0f * u );
            const ImVec2 button_max( x + 42.0f * u, button.y + 42.0f * u );
            bool held = false;
            // a little taller than it looks, to be easy to hit
            const bool tapped = mobile_ui::pressable( id, ImVec2( x, y ), ImVec2( 42.0f * u, 48.0f * u ),
                                &held ) && enabled;
            mobile_ui::box( draw, button, button_max, held && enabled ? mobile_ui::col_button_down : col_hair,
                            mobile_ui::col_line, 11.0f );
            mobile_ui::icon( draw, icon, ImVec2( button.x + 12.0f * u, button.y + 12.0f * u ), 18.0f,
                             enabled ? mobile_ui::col_fg : col_glyph_off );
            x += 42.0f * u;
            return tapped;
        };
        if( step_button( "##batch_less", "minus", batch > 1 ) ) {
            batch--;
            refresh = true;
        }
        const std::string count = std::to_string( batch );
        const ImVec2 count_size = mobile_ui::text_size( face::mono, 18.0f, count );
        mobile_ui::text( draw, face::mono, 18.0f,
                         ImVec2( x + 4.0f * u + ( 34.0f * u - count_size.x ) * 0.5f,
                                 y + ( 48.0f * u - count_size.y ) * 0.5f ), mobile_ui::col_fg, count );
        x += 42.0f * u;
        if( step_button( "##batch_more", "plus", batch < max_batch ) ) {
            batch++;
            refresh = true;
        }
        x += 10.0f * u;
    }
    std::string label;
    if( !detail.can_craft ) {
        label = _( "Can't craft yet" );
    } else if( batch > 1 ) {
        //~ The button that starts crafting a batch. %d: the batch size
        label = string_format( rec.is_practice() ? _( "Practice ×%d" ) : _( "Craft ×%d" ), batch );
    } else {
        label = rec.is_practice() ? _( "Practice" ) : _( "Craft" );
    }
    const ImVec2 craft_pos( x, y );
    const ImVec2 craft_max( pos.x + width - pad, y + 48.0f * u );
    bool held = false;
    const bool tapped = mobile_ui::pressable( "##craft_go", craft_pos,
                        ImVec2( craft_max.x - craft_pos.x, 48.0f * u ), &held );
    const ImU32 fade = detail.can_craft ? 0xFFFFFFFF : IM_COL32( 255, 255, 255, 102 );
    mobile_ui::box( draw, craft_pos, craft_max,
                    ( held && detail.can_craft ? mobile_ui::col_accent_down : mobile_ui::col_accent ) & fade,
                    0, 12.0f );
    const ImVec2 label_size = mobile_ui::text_size( face::bold, 15.0f, label );
    mobile_ui::text( draw, face::bold, 15.0f,
                     ImVec2( craft_pos.x + ( craft_max.x - craft_pos.x - label_size.x ) * 0.5f,
                             craft_pos.y + ( 48.0f * u - label_size.y ) * 0.5f ),
                     detail.can_craft ? mobile_ui::col_on_accent : ( mobile_ui::col_fg & fade ), label );
    if( tapped && detail.can_craft &&
        you.check_eligible_containers_for_crafting( rec, batch ) ) {
        chosen = selected;
    }
    y += 48.0f * u + 14.0f * u;
    return y - pos.y;
}

void crafting_window::draw_sheet( const ImVec2 &origin, const ImVec2 &size )
{
    const float u = mobile_ui::unit();
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    const ImVec2 screen_max( origin.x + size.x, origin.y + size.y );
    draw->AddRectFilled( origin, screen_max, mobile_ui::col_dim );
    const bool outside = mobile_ui::pressable( "##craft_sheet_outside", origin, size );

    const float head_h = 22.0f * u;
    const float foot_h = safe.w + 6.0f * u;
    const float max_h = ( size.y - safe.y ) * 0.84f;
    // the height of the content is known once it has been drawn
    const float sheet_h = sheet_content_height > 0.0f ?
                          std::min( max_h, head_h + sheet_content_height + foot_h ) : max_h;
    const ImVec2 pos( origin.x, screen_max.y - sheet_h );
    draw->AddRectFilled( pos, screen_max, mobile_ui::col_sheet, 22.0f * u,
                         ImDrawFlags_RoundCornersTop );
    draw->AddRect( pos, ImVec2( screen_max.x, screen_max.y + 30.0f * u ), mobile_ui::col_line,
                   22.0f * u, ImDrawFlags_RoundCornersTop, std::max( 1.0f, u ) );
    draw->AddRectFilled( ImVec2( pos.x + size.x * 0.5f - 21.0f * u, pos.y + 9.0f * u ),
                         ImVec2( pos.x + size.x * 0.5f + 21.0f * u, pos.y + 14.0f * u ),
                         IM_COL32( 255, 255, 255, 71 ), 3.0f * u );
    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + head_h ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##craft_sheet", ImVec2( size.x, sheet_h - head_h - foot_h ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float height = draw_detail( base, size.x );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if( height != sheet_content_height ) {
        sheet_content_height = height;
        refresh = true;
    }
    if( outside && ImGui::GetIO().MousePos.y < pos.y ) {
        sheet_open = false;
        refresh = true;
    }
}

void crafting_window::draw_controls()
{
    const float u = mobile_ui::unit();
    const ImGuiViewport *const viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec4 safe = mobile_ui::safe_insets();
    ImDrawList *const draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    draw->AddRectFilled( origin, ImVec2( origin.x + size.x, origin.y + size.y ),
                         mobile_ui::col_screen );
    draw->PopClipRect();

    const bool landscape = size.x > size.y;
    const ImVec2 inner( origin.x + safe.x, origin.y + safe.y );
    const ImVec2 inner_size( size.x - safe.x - safe.z, size.y - safe.y );
    if( !landscape ) {
        const bool sheet = sheet_open && selected != nullptr;
        draw_left( inner, inner_size, !sheet );
        if( sheet ) {
            draw_sheet( origin, size );
        }
        return;
    }
    // the detail pane takes 44% of the width, as on the canvas
    const float detail_w = std::floor( inner_size.x * 0.44f );
    const float left_w = inner_size.x - detail_w;
    draw_left( inner, ImVec2( left_w, inner_size.y ), true );
    const ImVec2 pane( inner.x + left_w, inner.y );
    draw->AddLine( pane, ImVec2( pane.x, pane.y + inner_size.y ), mobile_ui::col_line,
                   std::max( 1.0f, u ) );
    ImGui::SetCursorScreenPos( ImVec2( pane.x, pane.y + 8.0f * u ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##craft_detail", ImVec2( detail_w, inner_size.y - 8.0f * u - safe.w ),
                       ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float height = draw_detail( base, detail_w );
    ImGui::SetCursorScreenPos( base );
    ImGui::Dummy( ImVec2( 1.0f, height ) );
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace

void show()
{
    avatar &you = get_avatar();
    const recipe *rec = nullptr;
    int batch = 1;
    {
        crafting_window window( you );
        // keychar: the search field takes text
        input_context ctxt( "MOBILE_CRAFTING", keyboard_mode::keychar );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "LEFT" );
        ctxt.register_action( "RIGHT" );
        ctxt.register_action( "ANY_INPUT" );
        while( true ) {
            ui_manager::redraw();
            if( window.close || window.chosen != nullptr ) {
                break;
            }
            // a tap changes things after they were drawn, so draw once more
            const bool again = window.refresh;
            window.refresh = false;
            const std::string action = again ? ctxt.handle_input( 10 ) : ctxt.handle_input();
            if( window.typing() ) {
                // back closes the keyboard first
                if( action == "QUIT" ) {
                    window.stop_typing();
                }
            } else if( action == "QUIT" ) {
                if( !window.back() ) {
                    break;
                }
            } else if( action == "LEFT" || action == "RIGHT" ) {
                // a swipe to the left brings the chips on the right into view
                window.scroll_chips( action == "LEFT" ? 1 : -1 );
            }
        }
        rec = window.chosen;
        batch = window.batch;
    }
    if( rec == nullptr ) {
        return;
    }
    // from here on it is what Character::craft does with the menu's answer
    uistate.read_recipes.insert( rec->ident() );
    if( crafting_allowed( you, *rec ) ) {
        you.make_craft( rec->ident(), batch );
    }
}

#else // !TILES

void show()
{
    get_avatar().craft();
}

#endif // TILES

} // namespace mobile_crafting
