// SC3's port on BozoEngine: what every port file includes. The context helpers (bozo/context.h), the generated
// functions by name (sc3_functions.h, made by bozo-recomp from SLUS_212.16 with SC3's externals) and the port API
// (bozo/port.h). Each subsystem registers its overrides from its own install(), called by applySc3 (sc3_port.cpp).
#pragma once

#include "bozo/context.h"
#include "bozo/port.h"
#include "bozo/recomp.h"
#include "sc3_functions.h"
#include "sc3_region.h"

#include <string>
#include <vector>

using PS2Runtime = Runtime;

namespace sc3
{
    using Registry = bozo::port::Registry;

    // An address of SLUS_212.16, where it lives in the executable this build is for (sc3_region.h, made by
    // scripts/sc3_region_map.py header from every at() of bozo/).
    consteval uint32_t at(uint32_t ntsc)
    {
        if (!region::kMoved)
            return ntsc;
        for (const region::Placed &placed : region::kPlaced)
            if (placed.ntsc == ntsc)
                return placed.here;
        throw "sc3::at: an address sc3_region.h does not place";
    }
    // The generated function at a SLUS_212.16 function's address, in the executable this build is for.
    template <uint32_t ntsc>
    inline constexpr GuestFunction recompiled = Guest<ntsc>::function;

    constexpr uint32_t kEntry = 0x100008u;

    namespace frame
    {
        void install(Registry &runtime);
        void state(bozo::state::Archive &a);
    }
    namespace clock
    {
        void install(Registry &runtime);
        void state(bozo::state::Archive &a);
        // The horizontal blanks so far, 16 bits; reading the same count again charges a line (a poll takes time).
        uint32_t lines(PS2Runtime *runtime, R5900Context *ctx);
        uint32_t linesSinceVblank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
        // A field in EE cycles and in seconds, at the rate the game set the display to (PAL 50 Hz, NTSC and PAL60
        // 59.94 Hz).
        uint64_t fieldCycles(PS2Runtime *runtime);
        double fieldSeconds(PS2Runtime *runtime);
    }
    namespace packet
    {
        void install(Registry &runtime);
        void copyScratchpad(PS2Runtime *runtime, uint32_t ram, uint32_t scratchpad, uint32_t quadwords, bool toScratchpad);
        void closeDepth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
        void closeFixed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    }
    namespace ot
    {
        void install(Registry &runtime);
        void kick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
        void dropFrame(uint8_t *rdram);
        void stateLoaded();
        // A frame with no kick (a movie's): presents what the renderer holds.
        void presentAlone();
        void holdStill();
        bool hidden(uint32_t address);
        void shutdown();
    }
    namespace twoD { void install(Registry &runtime); void shutdown(); }
    namespace meshes { void install(Registry &runtime); void shutdown(); }
    namespace textures { void install(Registry &runtime); void shutdown(); }
    namespace vu0 { void install(Registry &runtime); }
    namespace movies
    {
        void install(Registry &runtime);
        // At every frame end: the movies' clocks, pictures and soundtracks.
        void tick(PS2Runtime *runtime);
        // A smaller movie's current picture by Sofdec handle, for the 2D builders: RGBA rows (alpha 0x80 = 1.0), nothing copied while `known` is
        // its number.
        bool picture(PS2Runtime *runtime, uint32_t handle, uint32_t known, std::vector<uint32_t> &rgba, uint32_t &width, uint32_t &height, uint32_t &number);
        void shutdown();
        std::string unsaveable();
        void stateLoaded();
    }
    namespace audio { void install(Registry &runtime); }
    void statsLine(const char *format, ...) __attribute__((format(printf, 1, 2)));
    // The console language the game reads from the OSD settings: SC3_LANGUAGE (en, fr, de, es, it; the PAL disc has all
    // five), English when unset. Set before the game's first instruction.
    void applyLanguage(PS2Runtime *runtime);
    // After the game's last frame, while the renderer still exists: players stopped, every texture and draw the port keeps released.
    void shutdown();
    void state(bozo::state::Archive &a, Runtime *runtime);
    std::string unsaveable();
}
