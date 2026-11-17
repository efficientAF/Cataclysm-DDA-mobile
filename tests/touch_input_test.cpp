#include "cata_catch.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "mobile_hud_layout.h"
#include "point.h"
#include "touch_input.h"

TEST_CASE( "touch_tap_target", "[touch_input]" )
{
    using touch_input::classify_tap;
    using touch_input::tap_target;

    CHECK( classify_tap( point::zero ) == tap_target::self );
    CHECK( classify_tap( point::north ) == tap_target::adjacent );
    CHECK( classify_tap( point::south_east ) == tap_target::adjacent );
    CHECK( classify_tap( point( -1, 1 ) ) == tap_target::adjacent );
    CHECK( classify_tap( point( 2, 0 ) ) == tap_target::distant );
    CHECK( classify_tap( point( 1, -2 ) ) == tap_target::distant );
    CHECK( classify_tap( point( -7, 12 ) ) == tap_target::distant );
}

TEST_CASE( "touch_pinch_zoom_snaps_to_levels", "[touch_input]" )
{
    using touch_input::snap_zoom;

    SECTION( "small pinches keep the level" ) {
        CHECK( snap_zoom( 16, 1.0f, 4, 64 ) == 16 );
        CHECK( snap_zoom( 16, 1.3f, 4, 64 ) == 16 );
        CHECK( snap_zoom( 16, 0.8f, 4, 64 ) == 16 );
    }
    SECTION( "spreading zooms in, pinching zooms out" ) {
        CHECK( snap_zoom( 16, 2.0f, 4, 64 ) == 32 );
        CHECK( snap_zoom( 16, 1.5f, 4, 64 ) == 32 );
        CHECK( snap_zoom( 16, 4.0f, 4, 64 ) == 64 );
        CHECK( snap_zoom( 16, 0.5f, 4, 64 ) == 8 );
        CHECK( snap_zoom( 16, 0.25f, 4, 64 ) == 4 );
    }
    SECTION( "it stops at the ends and never wraps" ) {
        CHECK( snap_zoom( 64, 3.0f, 4, 64 ) == 64 );
        CHECK( snap_zoom( 16, 100.0f, 4, 64 ) == 64 );
        CHECK( snap_zoom( 4, 0.3f, 4, 64 ) == 4 );
        CHECK( snap_zoom( 16, 0.01f, 4, 64 ) == 4 );
    }
    SECTION( "nonsense input is harmless" ) {
        CHECK( snap_zoom( 16, 0.0f, 4, 64 ) == 16 );
        CHECK( snap_zoom( 16, -1.0f, 4, 64 ) == 16 );
        CHECK( snap_zoom( 0, 2.0f, 4, 64 ) == 4 );
    }
}

TEST_CASE( "touch_zoom_steps", "[touch_input]" )
{
    using touch_input::step_zoom;

    SECTION( "in the middle both modes agree" ) {
        for( const bool wrap : {
                 false, true
             } ) {
            CHECK( step_zoom( 16, true, 4, 64, wrap ) == 32 );
            CHECK( step_zoom( 16, false, 4, 64, wrap ) == 8 );
            CHECK( step_zoom( 32, true, 4, 64, wrap ) == 64 );
            CHECK( step_zoom( 8, false, 4, 64, wrap ) == 4 );
        }
    }
    SECTION( "upstream behavior wraps around at the ends" ) {
        CHECK( step_zoom( 64, true, 4, 64, true ) == 4 );
        CHECK( step_zoom( 4, false, 4, 64, true ) == 64 );
    }
    SECTION( "without wrapping it stays at the ends" ) {
        CHECK( step_zoom( 64, true, 4, 64, false ) == 64 );
        CHECK( step_zoom( 4, false, 4, 64, false ) == 4 );
    }
}

TEST_CASE( "touch_pan_accumulates_whole_tiles", "[touch_input]" )
{
    touch_input::pan_accumulator pan;

    SECTION( "a drag shorter than a tile does nothing until it adds up" ) {
        CHECK( pan.add( 10.0f, 0.0f, 32.0f, 32.0f ) == point::zero );
        CHECK( pan.add( 10.0f, 0.0f, 32.0f, 32.0f ) == point::zero );
        CHECK( pan.add( 13.0f, 0.0f, 32.0f, 32.0f ) == point::east );
        // one pixel was left over
        CHECK( pan.add( 30.0f, 0.0f, 32.0f, 32.0f ) == point::zero );
        CHECK( pan.add( 1.0f, 0.0f, 32.0f, 32.0f ) == point::east );
    }
    SECTION( "both axes and both directions" ) {
        CHECK( pan.add( -70.0f, 100.0f, 32.0f, 32.0f ) == point( -2, 3 ) );
        CHECK( pan.add( 6.0f, -4.0f, 32.0f, 32.0f ) == point::zero );
        CHECK( pan.add( 0.0f, -32.0f, 32.0f, 32.0f ) == point::north );
    }
    SECTION( "tiles need not be square" ) {
        CHECK( pan.add( 64.0f, 64.0f, 32.0f, 16.0f ) == point( 2, 4 ) );
    }
    SECTION( "reset drops the remainder" ) {
        CHECK( pan.add( 31.0f, 31.0f, 32.0f, 32.0f ) == point::zero );
        pan.reset();
        CHECK( pan.add( 2.0f, 2.0f, 32.0f, 32.0f ) == point::zero );
    }
    SECTION( "a zero tile size is ignored" ) {
        CHECK( pan.add( 100.0f, 100.0f, 0.0f, 32.0f ) == point::zero );
    }
}

TEST_CASE( "touch_drag_axis", "[touch_input]" )
{
    using touch_input::classify_drag;
    using touch_input::drag_axis;

    CHECK( classify_drag( 0.0f, 0.0f, 20.0f ) == drag_axis::none );
    CHECK( classify_drag( 14.0f, 14.0f, 20.0f ) == drag_axis::none );
    CHECK( classify_drag( 20.0f, 0.0f, 20.0f ) == drag_axis::none );
    CHECK( classify_drag( 21.0f, 0.0f, 20.0f ) == drag_axis::horizontal );
    CHECK( classify_drag( -40.0f, 10.0f, 20.0f ) == drag_axis::horizontal );
    CHECK( classify_drag( 5.0f, 30.0f, 20.0f ) == drag_axis::vertical );
    CHECK( classify_drag( -10.0f, -60.0f, 20.0f ) == drag_axis::vertical );
}

TEST_CASE( "touch_two_finger_gesture", "[touch_input]" )
{
    using touch_input::classify_two_finger;
    using touch_input::two_finger_gesture;

    SECTION( "resting fingers are neither" ) {
        CHECK( classify_two_finger( 1.0f, 0.0f, 20.0f ) == two_finger_gesture::undecided );
        CHECK( classify_two_finger( 1.1f, 15.0f, 20.0f ) == two_finger_gesture::undecided );
        CHECK( classify_two_finger( 0.9f, 20.0f, 20.0f ) == two_finger_gesture::undecided );
    }
    SECTION( "fingers moving apart or together zoom" ) {
        CHECK( classify_two_finger( 1.3f, 0.0f, 20.0f ) == two_finger_gesture::zoom );
        CHECK( classify_two_finger( 0.7f, 5.0f, 20.0f ) == two_finger_gesture::zoom );
        // a pinch wins even if the hand drifts
        CHECK( classify_two_finger( 2.0f, 60.0f, 20.0f ) == two_finger_gesture::zoom );
    }
    SECTION( "fingers moving together pan" ) {
        CHECK( classify_two_finger( 1.0f, 25.0f, 20.0f ) == two_finger_gesture::pan );
        CHECK( classify_two_finger( 1.15f, 200.0f, 20.0f ) == two_finger_gesture::pan );
    }
}

TEST_CASE( "touch_dial_zoom", "[touch_input]" )
{
    using touch_input::dial_zoom;

    SECTION( "at release it settles on a whole zoom level" ) {
        CHECK( dial_zoom( 16, 0.0f, 1, 4, 256 ) == 16 );
        CHECK( dial_zoom( 16, 0.4f, 1, 4, 256 ) == 16 );
        CHECK( dial_zoom( 16, 0.6f, 1, 4, 256 ) == 32 );
        CHECK( dial_zoom( 16, 2.2f, 1, 4, 256 ) == 64 );
        CHECK( dial_zoom( 16, -1.0f, 1, 4, 256 ) == 8 );
        CHECK( dial_zoom( 16, -1.6f, 1, 4, 256 ) == 4 );
    }
    SECTION( "it stops at the ends" ) {
        CHECK( dial_zoom( 16, 20.0f, 1, 4, 256 ) == 256 );
        CHECK( dial_zoom( 16, -20.0f, 1, 4, 256 ) == 4 );
        CHECK( dial_zoom( 16, 20.0f, 4, 4, 64 ) == 64 );
    }
    SECTION( "while dragging it also stops between the levels" ) {
        CHECK( dial_zoom( 16, 0.0f, 4, 4, 256 ) == 16 );
        CHECK( dial_zoom( 16, 0.25f, 4, 4, 256 ) == 19 );
        CHECK( dial_zoom( 16, 0.5f, 4, 4, 256 ) == 23 );
        CHECK( dial_zoom( 16, 0.75f, 4, 4, 256 ) == 27 );
        CHECK( dial_zoom( 16, 1.0f, 4, 4, 256 ) == 32 );
        CHECK( dial_zoom( 16, -0.5f, 4, 4, 256 ) == 11 );
    }
    SECTION( "a zoom left between two levels settles on the nearest" ) {
        CHECK( dial_zoom( 23, 0.0f, 1, 4, 256 ) == 32 );
        CHECK( dial_zoom( 19, 0.0f, 1, 4, 256 ) == 16 );
    }
}

TEST_CASE( "touch_pan_speed", "[touch_input]" )
{
    using touch_input::pan_speed;

    // resting on the dial does not scroll
    CHECK( pan_speed( 0.0f, 20.0f, 100.0f ) == 0.0f );
    CHECK( pan_speed( 20.0f, 20.0f, 100.0f ) == 0.0f );
    // faster the further out
    const float slow = pan_speed( 30.0f, 20.0f, 100.0f );
    const float mid = pan_speed( 70.0f, 20.0f, 100.0f );
    const float fast = pan_speed( 120.0f, 20.0f, 100.0f );
    CHECK( slow > 0.0f );
    CHECK( mid > slow );
    CHECK( fast > mid );
    // and no faster past full travel
    CHECK( pan_speed( 500.0f, 20.0f, 100.0f ) == fast );
}

TEST_CASE( "mobile_hud_layout_dials", "[touch_input]" )
{
    using mobile_hud::piece_id;
    const mobile_hud::preset &base = mobile_hud::fallback_preset();

    SECTION( "a layout from before the dials keeps the preset's zoom and pan" ) {
        const JsonValue jv = json_loader::from_string(
                                 R"({ "id": "old", "portrait": { "zoom": { "v": 0, "a": "bl", "x": 30, "y": 300, "dir": "col" } } })" );
        const mobile_hud::preset p = mobile_hud::read_preset( jv.get_object(), base );
        CHECK( p.portrait[piece_id::zoom].visible );
        CHECK( p.portrait[piece_id::zoom].pin == base.portrait[piece_id::zoom].pin );
        CHECK( p.portrait[piece_id::pan].visible );
    }
    SECTION( "dial sizes stay between 44 and 120" ) {
        const JsonValue jv = json_loader::from_string(
                                 R"({ "id": "sizes", "portrait": { "zoom": { "v": 1, "a": "tr", "x": 12, "y": 150, "size": 10 }, "pan": { "v": 1, "a": "tr", "x": 12, "y": 214, "size": 500 } } })" );
        const mobile_hud::preset p = mobile_hud::read_preset( jv.get_object(), base );
        CHECK( p.portrait[piece_id::zoom].size == 44.0f );
        CHECK( p.portrait[piece_id::pan].size == 120.0f );
    }
}

TEST_CASE( "mobile_hud_layout_zoom_slider", "[touch_input]" )
{
    using mobile_hud::piece_id;
    const mobile_hud::preset &base = mobile_hud::fallback_preset();
    CHECK_FALSE( base.portrait[piece_id::zoom].slider );

    const JsonValue jv = json_loader::from_string(
                             R"({ "id": "s", "portrait": { "zoom": { "v": 1, "a": "tr", "x": 12, "y": 150, "style": "slider", "dir": "col", "size": 56, "width": 900 } } })" );
    const mobile_hud::preset p = mobile_hud::read_preset( jv.get_object(), base );
    CHECK( p.portrait[piece_id::zoom].slider );
    CHECK( p.portrait[piece_id::zoom].column );
    CHECK( p.portrait[piece_id::zoom].width == 320.0f );
    // the other orientation keeps the dial
    CHECK_FALSE( p.landscape[piece_id::zoom].slider );
}
