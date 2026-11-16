#include "mobile_menu.h"

#include <algorithm>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "options.h"
#include "translations.h"

namespace mobile_menu
{

bool enabled()
{
#if defined(TILES)
    return get_option<bool>( "MOBILE_MENUS" );
#else
    return false;
#endif
}

class main_menu_view::impl : public cataimgui::window
{
    public:
        impl() : cataimgui::window( "MOBILE_MAIN_MENU",
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings ) {}

        std::vector<menu_group> groups;
        std::string version;
        // group whose sub-entries are showing, or -1
        int open_group = -1;
        std::optional<std::pair<int, int>> choice;

    protected:
        cataimgui::bounds get_bounds() override {
            return { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        void draw_controls() override;

    private:
        bool big_button( const std::string &label, const std::string &id, float width, bool primary );
};

bool main_menu_view::impl::big_button( const std::string &label, const std::string &id,
                                       const float width, const bool primary )
{
    if( primary ) {
        // the canvas's accent: orange with dark text
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.94f, 0.57f, 0.25f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.98f, 0.66f, 0.36f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.80f, 0.45f, 0.16f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.10f, 0.06f, 0.02f, 1.0f ) );
    }
    const float height = ImGui::GetFontSize() * 3.0f;
    ImGui::SetWindowFontScale( 1.25f );
    const bool pressed = ImGui::Button( ( label + "##" + id ).c_str(), ImVec2( width, height ) );
    ImGui::SetWindowFontScale( 1.0f );
    if( primary ) {
        ImGui::PopStyleColor( 4 );
    }
    return pressed;
}

void main_menu_view::impl::draw_controls()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float font = ImGui::GetFontSize();
    const bool landscape = avail.x > avail.y;
    const float gap = font * 0.5f;

    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( gap, gap ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, font * 0.5f );

    // In landscape the title sits on the left and the buttons on the right; in
    // portrait the title is on top.
    const float column_w = landscape ? std::min( avail.x * 0.5f, font * 26.0f ) : avail.x;
    if( landscape ) {
        ImGui::BeginChild( "brand", ImVec2( avail.x - column_w - gap, 0.0f ) );
    }
    ImGui::Dummy( ImVec2( 0.0f, landscape ? avail.y * 0.25f : font ) );
    ImGui::SetWindowFontScale( 2.4f );
    ImGui::TextUnformatted( "CATACLYSM" );
    ImGui::SetWindowFontScale( 1.2f );
    ImGui::TextColored( ImVec4( 0.94f, 0.57f, 0.25f, 1.0f ), "%s", "DARK DAYS AHEAD" );
    ImGui::SetWindowFontScale( 1.0f );
    ImGui::TextDisabled( "%s", version.c_str() );
    if( landscape ) {
        ImGui::EndChild();
        ImGui::SameLine();
    } else {
        ImGui::Dummy( ImVec2( 0.0f, font ) );
    }

    ImGui::BeginChild( "buttons", ImVec2( column_w, 0.0f ) );
    const float width = ImGui::GetContentRegionAvail().x;
    if( open_group >= 0 && open_group < static_cast<int>( groups.size() ) ) {
        // sub-entries of one group, with a way back
        const menu_group &group = groups[open_group];
        ImGui::SetWindowFontScale( 1.3f );
        ImGui::TextUnformatted( group.label.c_str() );
        ImGui::SetWindowFontScale( 1.0f );
        for( int i = 0; i < static_cast<int>( group.sub_entries.size() ); ++i ) {
            if( big_button( group.sub_entries[i], std::to_string( i ), width, i == 0 ) ) {
                choice = std::make_pair( group.id, i );
            }
        }
        if( big_button( _( "Back" ), "back", width, false ) ) {
            open_group = -1;
        }
    } else {
        open_group = -1;
        for( int i = 0; i < static_cast<int>( groups.size() ); ++i ) {
            const menu_group &group = groups[i];
            if( big_button( group.label, std::to_string( i ), width, i == 0 ) ) {
                if( group.sub_entries.empty() ) {
                    choice = std::make_pair( group.id, 0 );
                } else {
                    open_group = i;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );
}

main_menu_view::main_menu_view() : pimpl( std::make_unique<impl>() ) {}

main_menu_view::~main_menu_view() = default;

void main_menu_view::set_groups( const std::vector<menu_group> &groups, const std::string &version )
{
    pimpl->groups = groups;
    pimpl->version = version;
}

std::optional<std::pair<int, int>> main_menu_view::take_choice()
{
    std::optional<std::pair<int, int>> ret = pimpl->choice;
    pimpl->choice.reset();
    return ret;
}

} // namespace mobile_menu
