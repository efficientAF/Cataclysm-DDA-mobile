#include <string>
#include <vector>

#include "cata_catch.h"
#include "mobile_prompt.h"

TEST_CASE( "mobile_prompt_labels_from_hint_text", "[mobile_prompt]" )
{
    using mobile_prompt::label_from_hint;

    CHECK( label_from_hint( " to go back." ) == "Go back" );
    CHECK( label_from_hint( " to move anyway." ) == "Move anyway" );
    CHECK( label_from_hint( " to accept your fate\xe2\x80\xa6" ) == "Accept your fate" );
    CHECK( label_from_hint( "Cancel: " ) == "Cancel" );
    CHECK( label_from_hint( " | Press " ) == "Press" );
    CHECK( label_from_hint( " . " ).empty() );
    CHECK( label_from_hint( "" ).empty() );
}

TEST_CASE( "mobile_prompt_finds_key_hints", "[mobile_prompt]" )
{
    using mobile_prompt::bound_action;
    using mobile_prompt::hint_result;
    using mobile_prompt::parse_key_hints;

    const std::vector<bound_action> look = {
        { "QUIT", "ESC, q, Q, or SPACE", "Cancel" },
        { "CONFIRM", "RETURN or KEYPAD_ENTER", "Confirm" },
        { "TRAVEL_TO", "t", "Travel to destination" },
    };

    SECTION( "the safe mode note" ) {
        const hint_result res = parse_key_hints(
                                    "ESC, q, Q, or SPACE to go back.\nRETURN or KEYPAD_ENTER to move anyway.", look );
        REQUIRE( res.hints.size() == 2 );
        CHECK( res.hints[0].action == "QUIT" );
        CHECK( res.hints[0].label == "Go back" );
        CHECK( res.hints[1].action == "CONFIRM" );
        CHECK( res.hints[1].label == "Move anyway" );
        CHECK( res.remaining.empty() );
    }
    SECTION( "the death camera note, with its spinner" ) {
        const hint_result res = parse_key_hints(
                                    " | Press ESC, q, Q, or SPACE to accept your fate\xe2\x80\xa6", look );
        REQUIRE( res.hints.size() == 1 );
        CHECK( res.hints[0].action == "QUIT" );
        CHECK( res.hints[0].label == "Accept your fate" );
        CHECK( res.remaining.empty() );
    }
    SECTION( "lines without keys stay in the note" ) {
        const hint_result res = parse_key_hints( "Spotted a zombie!\nPress t to run.", look );
        REQUIRE( res.hints.size() == 1 );
        CHECK( res.hints[0].action == "TRAVEL_TO" );
        CHECK( res.hints[0].label == "Run" );
        CHECK( res.remaining == "Spotted a zombie!" );
    }
    SECTION( "keys only count as whole words" ) {
        const hint_result res = parse_key_hints( "Wait a moment, traveler.", look );
        CHECK( res.hints.empty() );
        CHECK( res.remaining == "Wait a moment, traveler." );
    }
    SECTION( "two hints on one line" ) {
        const hint_result res = parse_key_hints( "RETURN or KEYPAD_ENTER to continue, t to stop.", look );
        REQUIRE( res.hints.size() == 2 );
        CHECK( res.hints[0].label == "Continue" );
        CHECK( res.hints[1].label == "Stop" );
    }
    SECTION( "a hint with no text falls back to the action's name" ) {
        const hint_result res = parse_key_hints( "t", look );
        REQUIRE( res.hints.size() == 1 );
        CHECK( res.hints[0].label == "Travel to destination" );
    }
    SECTION( "the label can come before the keys" ) {
        const hint_result res = parse_key_hints( "Cancel: t", look );
        REQUIRE( res.hints.size() == 1 );
        CHECK( res.hints[0].label == "Cancel" );
    }
}
