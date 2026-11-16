#include "cata_catch.h"
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
