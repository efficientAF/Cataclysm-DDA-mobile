#include "mobile_default.h"

#include <cstdlib>
#include <string>

namespace mobile_default
{

static bool requested = false;

void request()
{
    requested = true;
}

static bool env_is( const char *const name, const std::string &value )
{
    const char *const found = std::getenv( name );
    return found != nullptr && value == found;
}

bool on()
{
#if defined(__ANDROID__) || defined(CATA_MOBILE_UI_DEFAULT)
    return true;
#else
    if( env_is( "CATA_MOBILE_UI", "0" ) ) {
        return false;
    }
    return requested || env_is( "CATA_MOBILE_UI", "1" ) || env_is( "SteamDeck", "1" );
#endif
}

} // namespace mobile_default
