// PYROSCOPE_WINDOWS_TODO: force-included into every Windows TU via MSBuild ForcedIncludeFiles.
// Ensures <winsock2.h> is pulled in before <windows.h> (via anything) so that windows.h's
// legacy <winsock.h> include is suppressed. Needed because cpp-httplib (fork-only dep) uses
// winsock2 and mixing with windows.h otherwise causes hundreds of C2375/C2011 redefinitions.
#pragma once

#if defined(_WIN32) && !defined(PYROSCOPE_WINSOCK2_INCLUDED)
#define PYROSCOPE_WINSOCK2_INCLUDED
#include <winsock2.h>
#endif
