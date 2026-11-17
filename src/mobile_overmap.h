#pragma once
#ifndef CATA_SRC_MOBILE_OVERMAP_H
#define CATA_SRC_MOBILE_OVERMAP_H

#include <memory>
#include <string>

#include "coords_fwd.h"

class input_context;

namespace overmap_ui
{
struct overmap_draw_data_t;
} // namespace overmap_ui

// The map screen of the mobile UI fork, behind the MOBILE_MENUS option.  It
// follows the UI canvas: an app bar, a pill naming the place at the center, zoom
// and center buttons, a legend, and a sheet for the tile that was tapped.
//
// The overmap itself is still drawn, panned, zoomed and travelled by
// overmap_ui.cpp.  The view here is a transparent ImGui window on top of it that
// turns touches into the actions upstream already handles.
namespace mobile_overmap
{

// True if the overmap that is about to be shown should get the mobile chrome:
// every map but the debug editor.  A "pick a spot" prompt (ui::omap::choose_point)
// gets its message as a banner and "Choose this place" on the tile sheet.
bool wanted( const overmap_ui::overmap_draw_data_t &data );

// True while "Travel here" on the tile sheet stands in for upstream's "Travel to
// this point?" question.
bool travel_confirmed();

class view
{
    public:
        view();
        ~view();
        view( const view & ) = delete;
        view &operator=( const view & ) = delete;

        // Call after every input of the overmap loop.  Applies what was touched
        // since the last call: moves the cursor for a pan, and returns the
        // action upstream's loop should handle in place of the given one.
        std::string handle( const std::string &action, input_context &ctxt,
                            tripoint_abs_omt &cursor );
    private:
        class impl;
        std::unique_ptr<impl> pimpl;
};

} // namespace mobile_overmap

#endif // CATA_SRC_MOBILE_OVERMAP_H
