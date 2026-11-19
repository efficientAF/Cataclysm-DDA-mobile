#include <string>

#include "cata_catch.h"
#include "mobile_pad.h"

using mobile_pad::button;

static mobile_pad::layout test_layout()
{
    mobile_pad::layout l;
    l.of( button::a ) = { "plain", "", "", "" };
    l.of( button::x ) = { "press_x", "long_x", "", "" };
    l.of( button::l2 ) = { "press_l2", "ignored_long", "Left", "Right" };
    l.of( button::up ) = { "dpad", "", "", "" };
    l.of( button::down ) = { "dpad_press", "dpad_long", "", "" };
    l.of( button::r2 ) = { mobile_pad::action_step, "", "", "" };
    l.menus.push_back( { "Left", { { "n", "ne", "e", "se", "s", "sw", "w", "" } } } );
    l.menus.push_back( { "Right", { { "rn", "", "", "", "rs", "", "", "" } } } );
    return l;
}

TEST_CASE( "mobile_pad_slot_from_stick", "[mobile_pad]" )
{
    CHECK( mobile_pad::slot_at( 0.0f, -1.0f ) == 0 );
    CHECK( mobile_pad::slot_at( 0.7f, -0.7f ) == 1 );
    CHECK( mobile_pad::slot_at( 1.0f, 0.0f ) == 2 );
    CHECK( mobile_pad::slot_at( 0.7f, 0.7f ) == 3 );
    CHECK( mobile_pad::slot_at( 0.0f, 1.0f ) == 4 );
    CHECK( mobile_pad::slot_at( -0.7f, 0.7f ) == 5 );
    CHECK( mobile_pad::slot_at( -1.0f, 0.0f ) == 6 );
    CHECK( mobile_pad::slot_at( -0.7f, -0.7f ) == 7 );
    CHECK( mobile_pad::slot_at( -0.1f, -1.0f ) == 0 );
}

TEST_CASE( "mobile_pad_plain_button_acts_on_press", "[mobile_pad]" )
{
    const mobile_pad::layout l = test_layout();
    mobile_pad::machine m;
    mobile_pad::outcome out = m.button_event( l, button::a, true, 1000 );
    CHECK( out.action == "plain" );
    CHECK_FALSE( out.repeat );
    CHECK_FALSE( m.holding() );
    CHECK( m.button_event( l, button::a, false, 1500 ).action.empty() );
    // the d-pad and the step repeat
    CHECK( m.button_event( l, button::up, true, 2000 ).repeat );
    CHECK( m.button_event( l, button::r2, true, 2100 ).repeat );
    // a d-pad direction with a long press does not
    out = m.button_event( l, button::down, true, 3000 );
    CHECK( out.action.empty() );
    CHECK( m.button_event( l, button::down, false, 3100 ).action == "dpad_press" );
}

TEST_CASE( "mobile_pad_long_press", "[mobile_pad]" )
{
    const mobile_pad::layout l = test_layout();
    mobile_pad::machine m;
    // let go early: the press, on release
    CHECK( m.button_event( l, button::x, true, 1000 ).action.empty() );
    CHECK( m.tick( l, 1200 ).action.empty() );
    CHECK( m.button_event( l, button::x, false, 1249 ).action == "press_x" );
    // held: the long press at the hold time, nothing on release
    CHECK( m.button_event( l, button::x, true, 2000 ).action.empty() );
    CHECK( m.tick( l, 2249 ).action.empty() );
    CHECK( m.tick( l, 2250 ).action == "long_x" );
    CHECK( m.tick( l, 2300 ).action.empty() );
    CHECK( m.button_event( l, button::x, false, 2600 ).action.empty() );
    CHECK_FALSE( m.holding() );
    // let go after the hold time before any tick: still the long press
    CHECK( m.button_event( l, button::x, true, 3000 ).action.empty() );
    CHECK( m.button_event( l, button::x, false, 3400 ).action == "long_x" );
    // the hold time is a setting
    m.hold_time = 500;
    CHECK( m.button_event( l, button::x, true, 4000 ).action.empty() );
    CHECK( m.button_event( l, button::x, false, 4400 ).action == "press_x" );
}

TEST_CASE( "mobile_pad_menu_button", "[mobile_pad]" )
{
    const mobile_pad::layout l = test_layout();
    CHECK( l.has_menus( button::l2 ) );
    // menus win over a long press
    CHECK_FALSE( l.has_long_press( button::l2 ) );
    mobile_pad::machine m;

    SECTION( "a quick press runs the press action, a stick or not" ) {
        CHECK( m.button_event( l, button::l2, true, 1000 ).action.empty() );
        CHECK( m.stick_event( l, 0, 0.0f, -1.0f, 1050 ).action.empty() );
        CHECK_FALSE( m.ring_open() );
        CHECK( m.button_event( l, button::l2, false, 1100 ).action == "press_l2" );
    }
    SECTION( "held without a stick runs nothing" ) {
        m.button_event( l, button::l2, true, 1000 );
        CHECK( m.tick( l, 1300 ).action.empty() );
        CHECK_FALSE( m.ring_open() );
        CHECK( m.button_event( l, button::l2, false, 1500 ).action.empty() );
    }
    SECTION( "letting go of the button with the stick pushed runs the slot" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        mobile_pad::outcome out = m.stick_event( l, 0, 1.0f, 0.0f, 1400 );
        CHECK( out.changed );
        CHECK( m.ring_open() );
        CHECK( m.ring_stick_index() == 0 );
        CHECK( m.ring_slot() == 2 );
        // rolled round to another slot
        m.stick_event( l, 0, 0.0f, 1.0f, 1500 );
        CHECK( m.ring_slot() == 4 );
        out = m.button_event( l, button::l2, false, 1600 );
        CHECK( out.action == "s" );
        CHECK( out.changed );
        CHECK_FALSE( m.ring_open() );
    }
    SECTION( "the stick already pushed when the hold time passes opens the ring" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.stick_event( l, 1, 0.0f, -1.0f, 1100 );
        CHECK_FALSE( m.ring_open() );
        CHECK( m.tick( l, 1250 ).changed );
        CHECK( m.ring_open() );
        CHECK( m.ring_stick_index() == 1 );
        CHECK( m.button_event( l, button::l2, false, 1300 ).action == "rn" );
    }
    SECTION( "a flick runs the slot and the button can go on" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, 0.0f, -1.0f, 1400 );
        // snaps back: high, then the centre 40 ms later
        m.stick_event( l, 0, 0.0f, -0.6f, 2000 );
        mobile_pad::outcome out = m.stick_event( l, 0, 0.0f, 0.0f, 2040 );
        CHECK( out.action == "n" );
        CHECK_FALSE( m.ring_open() );
        CHECK( m.holding() );
        // again, in one jump
        m.stick_event( l, 0, 1.0f, 0.0f, 2500 );
        CHECK( m.ring_open() );
        CHECK( m.stick_event( l, 0, 0.0f, 0.0f, 2900 ).action == "e" );
        CHECK( m.button_event( l, button::l2, false, 3000 ).action.empty() );
    }
    SECTION( "easing the stick back runs nothing" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, 0.0f, -1.0f, 1400 );
        m.stick_event( l, 0, 0.0f, -0.7f, 2000 );
        m.stick_event( l, 0, 0.0f, -0.4f, 2100 );
        mobile_pad::outcome out = m.stick_event( l, 0, 0.0f, -0.1f, 2200 );
        CHECK( out.action.empty() );
        CHECK( m.ring_slot() == -1 );
        // and letting go of the button then runs nothing either
        CHECK( m.button_event( l, button::l2, false, 2300 ).action.empty() );
    }
    SECTION( "the flick time is the line" ) {
        m.flick_time = 150;
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, 0.0f, -1.0f, 1400 );
        m.stick_event( l, 0, 0.0f, -0.6f, 2000 );
        CHECK( m.stick_event( l, 0, 0.0f, 0.0f, 2150 ).action == "n" );
        m.stick_event( l, 0, 0.0f, -1.0f, 3000 );
        m.stick_event( l, 0, 0.0f, -0.6f, 3100 );
        CHECK( m.stick_event( l, 0, 0.0f, 0.0f, 3251 ).action.empty() );
    }
    SECTION( "a shallow push is timed from where it got to" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, 0.6f, 0.0f, 1400 );
        CHECK( m.ring_slot() == 2 );
        m.stick_event( l, 0, 0.4f, 0.0f, 2000 );
        CHECK( m.stick_event( l, 0, 0.0f, 0.0f, 2050 ).action == "e" );
    }
    SECTION( "B backs out" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, 1.0f, 0.0f, 1400 );
        mobile_pad::outcome out = m.button_event( l, button::b, true, 1500 );
        CHECK( out.action.empty() );
        CHECK( out.changed );
        CHECK_FALSE( m.ring_open() );
        // the stick does nothing more until the button is pressed again
        m.stick_event( l, 0, 0.0f, 1.0f, 1600 );
        CHECK_FALSE( m.ring_open() );
        CHECK( m.button_event( l, button::l2, false, 1700 ).action.empty() );
    }
    SECTION( "an empty slot runs nothing" ) {
        m.button_event( l, button::l2, true, 1000 );
        m.tick( l, 1300 );
        m.stick_event( l, 0, -0.7f, -0.7f, 1400 );
        CHECK( m.ring_slot() == 7 );
        CHECK( m.button_event( l, button::l2, false, 1500 ).action.empty() );
    }
    SECTION( "another button while one is held acts at once" ) {
        m.button_event( l, button::l2, true, 1000 );
        CHECK( m.button_event( l, button::x, true, 1100 ).action == "press_x" );
        CHECK( m.held_button() == button::l2 );
    }
}

TEST_CASE( "mobile_pad_layout_menus", "[mobile_pad]" )
{
    mobile_pad::layout l = mobile_pad::default_layout();
    CHECK( l.find_menu( "Activities" ) != nullptr );
    CHECK( l.menu_on( button::l2, 0 ) == l.find_menu( "Activities" ) );
    CHECK( l.menu_on( button::l2, 1 ) == l.find_menu( "Character" ) );
    CHECK( l.has_long_press( button::a ) );
    l.rename_menu( "Activities", "Doing" );
    CHECK( l.of( button::l2 ).menu_left == "Doing" );
    CHECK( l.free_menu_name( "Doing" ) == "Doing 2" );
    l.remove_menu( "Doing" );
    CHECK( l.of( button::l2 ).menu_left.empty() );
    CHECK( l.menu_on( button::l2, 0 ) == nullptr );
    // a menu that is gone leaves the button its long press
    l.of( button::l1 ).menu_left = "No such menu";
    l.of( button::l1 ).long_press = "x";
    CHECK( l.has_long_press( button::l1 ) );
}
