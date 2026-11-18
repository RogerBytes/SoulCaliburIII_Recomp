#include "sc3_draw.h"
#include "sc3_port.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace
{
    void applySc3(sc3::Registry &runtime)
    {
        sc3::frame::install(runtime);
        sc3::clock::install(runtime);
        sc3::packet::install(runtime);
        sc3::ot::install(runtime);
        sc3::twoD::install(runtime);
        sc3::meshes::install(runtime);
        sc3::textures::install(runtime);
        sc3::vu0::install(runtime);
        sc3::movies::install(runtime);
        sc3::audio::install(runtime);
    }
}

void sc3::statsLine(const char *format, ...)
{
    static const bool enabled = [] { const char *v = std::getenv("SC3_STATS"); return v != nullptr && *v != '\0' && *v != '0'; }();
    if (!enabled)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

void sc3::shutdown()
{
    movies::shutdown();
    ot::shutdown();
    draw::releaseKeyed();
    twoD::shutdown();
    meshes::shutdown();
    textures::shutdown();
}

BOZO_PORT(sc3::region::kTitle, sc3::region::kBootElf, sc3::kEntry, &applySc3)
