// A frame on BozoEngine with no GS: the kick 0x10F098 in C++ (the draws go to the renderer, the state the GS handler
// 0x10E658 leaves behind is written at once), the game's DMA driver answered at its own functions so no chain is ever
// linked or sent, and the frame ends in the main loop's Timer 3 wait.
#include "sc3_port.h"

#include "bozo/app/frame_pacer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sc3::frame
{
    namespace
    {
        // Where recomp/sc3_patches.txt points the game's CSR FIELD reads: the kernel's unused pages, below the image.
        constexpr uint32_t kFieldWord = 0x000FFFF0u;
        // And the handler's read of the lines.
        constexpr uint32_t kVblankLinesWord = 0x000FFFF8u;

        bool g_frameOpen = false;
        bozo::app::FramePacer g_pacer;
        uint64_t g_lastFrameEnd = 0;

        template <typename T>
        T get(const uint8_t *rdram, uint32_t address)
        {
            T v;
            std::memcpy(&v, rdram + address, sizeof(T));
            return v;
        }

        template <typename T>
        void put(uint8_t *rdram, uint32_t address, T v) { std::memcpy(rdram + address, &v, sizeof(T)); }

        void answered(uint8_t *, R5900Context *ctx, PS2Runtime *) { ctx->pc = getRegU32(ctx, 31); }

        // The intermittent fault at 0x117808 (STATUS.md, not reproduced on demand): a file with no entries says who passed it.
        void fileUpload(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t file = getRegU32(ctx, 5);
            if (file >= 0x100000u && file < 0x2000000u && rdram[file + 5u] == 0u && rdram[file + 7u] == 0u)
                std::fprintf(stderr, "[sc3:frame] 0x1177E0 file 0x%x has no entries: index %u, packet 0x%x, ra 0x%x, frame %u, words %08x %08x %08x %08x\n", file,
                             getRegU32(ctx, 6), getRegU32(ctx, 4), getRegU32(ctx, 31), get<uint32_t>(rdram, addr::FrameNumber), get<uint32_t>(rdram, file),
                             get<uint32_t>(rdram, file + 4u), get<uint32_t>(rdram, file + 8u), get<uint32_t>(rdram, file + 12u));
            fn::FileUpload(rdram, ctx, runtime);
        }

        void noteField(uint8_t *rdram, PS2Runtime *runtime)
        {
            put<uint64_t>(rdram, kFieldWord, (bozo::port::vblankCount(runtime) & 1u) << 13);
        }

        void vblankHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            noteField(rdram, runtime);
            put<uint32_t>(rdram, kVblankLinesWord, clock::lines(runtime, ctx));
            fn::VblankHandler(rdram, ctx, runtime);
        }

        void endOfFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            movies::tick(runtime);
            const double field = clock::fieldSeconds(runtime);
            g_pacer.setCap(static_cast<uint32_t>(std::lround(1.0 / field)));
            g_pacer.setPaced(bozo::port::realTime(runtime));
            g_pacer.frameEnd();
            ot::holdStill();
            // The host fills a frame with no wait of its own (a movie's, in ADXM's sleep) up to an NTSC field; a PAL50 field
            // is longer, and the rest is charged here.
            const uint64_t fieldCycles = clock::fieldCycles(runtime), elapsed = bozo::port::now(runtime) - g_lastFrameEnd;
            if (fieldCycles > clock::kNtscFieldCycles && elapsed < fieldCycles)
                bozo::port::accountCycles(runtime, ctx, fieldCycles - elapsed);
            g_lastFrameEnd = bozo::port::now(runtime);
            bozo::port::frameEnd(runtime, ctx);
            noteField(rdram, runtime);
        }

        // The rest of _InitSys is the kernel's and the timer library's, native. Its first call makes the two library locks
        // that guest code goes on using (0x5792F8, 0x5792FC: the stdio list at 0x42B358).
        void initSys(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t returnTo = getRegU32(ctx, 31);
            applyLanguage(runtime);
            fn::InitSysRest(rdram, ctx, runtime);
            setReturnS32(ctx, 0);
            ctx->pc = returnTo;
        }

        void adxmVsyncSleep(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t caller = *reinterpret_cast<uint32_t *>(rdram + (getRegU32(ctx, 29) & 0x1FFFFFFu));
            if (getRegU32(ctx, 31) == addr::AdxmVsyncSleepReturn && caller == addr::MovieFrameWaitReturn)
            {
                ot::presentAlone();
                endOfFrame(rdram, ctx, runtime);
                ctx->pc = addr::AdxmVsyncSleepReturn;
                return;
            }
            fn::AdxmVsyncSleep(rdram, ctx, runtime);
        }

        void gsDrainWait(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            setReturnS32(ctx, 0);
            ctx->pc = getRegU32(ctx, 31);
        }

        void padSetCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            auto *current = reinterpret_cast<uint32_t *>(rdram + addr::PadCallback);
            setReturnU32(ctx, *current);
            *current = getRegU32(ctx, 4);
            ctx->pc = getRegU32(ctx, 31);
        }

        void fromScratchpad(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
        {
            packet::copyScratchpad(runtime, getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6) & 0xFFFFu, false);
            ctx->pc = getRegU32(ctx, 31);
        }

        void toScratchpad(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
        {
            packet::copyScratchpad(runtime, getRegU32(ctx, 5), getRegU32(ctx, 4), getRegU32(ctx, 6) & 0xFFFFu, true);
            ctx->pc = getRegU32(ctx, 31);
        }

        void frameKick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const auto overBudget = [&](uint32_t limit, uint32_t used) {
                return get<uint32_t>(rdram, addr::FrameLists + limit) < get<uint32_t>(rdram, addr::Renderer + used);
            };
            const uint32_t counts = addr::Renderer + 0x80u + (get<uint32_t>(rdram, addr::Renderer + 0x38u) ^ 1u);
            const uint8_t signalsA = rdram[counts + 0x16u];
            const uint8_t signalsB = rdram[counts + 0x18u];
            g_frameOpen = true;
            if (overBudget(0x28u, 0x68u) || overBudget(0x2Cu, 0x6Cu) || overBudget(0x34u, 0x74u) || overBudget(0x30u, 0x70u) ||
                get<uint16_t>(rdram, addr::FrameLists + 0x3Eu) != 0u || rdram[addr::OverBudget] != 0u)
            {
                rdram[addr::FrameSkipped] = 1u;
                put<uint16_t>(rdram, addr::FrameLists + 0x3Eu, 0u);
                rdram[addr::Renderer + 0x93u] = signalsB;
                rdram[addr::FinishFlag] = 1u;
                rdram[addr::Renderer + 0x92u] = signalsA;
                put<uint32_t>(rdram, addr::KickedFrameNumber, 0u);
                put<uint16_t>(rdram, addr::Renderer + 0x82u, 1u);
                ot::dropFrame(rdram);
                ctx->pc = getRegU32(ctx, 31);
                return;
            }
            rdram[addr::FrameSkipped] = 0u;
            put<uint32_t>(rdram, addr::KickedFrameNumber, get<uint32_t>(rdram, addr::FrameNumber));
            put<uint16_t>(rdram, addr::Renderer + 0x90u, 0u);
            ot::kick(rdram, ctx, runtime);
            rdram[addr::Renderer + 0x93u] = signalsB;
            rdram[addr::Renderer + 0x92u] = signalsA;
            rdram[addr::FinishFlag] = 1u;
            put<uint32_t>(rdram, addr::FinishCount, get<uint32_t>(rdram, addr::FinishCount) + 1u);
            put<uint16_t>(rdram, addr::Renderer + 0x82u, 1u);
            put<uint32_t>(rdram, addr::Signals, 0u);
            ctx->pc = getRegU32(ctx, 31);
        }

        void elapsedLines(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (g_frameOpen && getRegU32(ctx, 31) == addr::FrameEndReturn)
            {
                g_frameOpen = false;
                endOfFrame(rdram, ctx, runtime);
            }
            setReturnU32(ctx, clock::linesSinceVblank(rdram, ctx, runtime));
            ctx->pc = getRegU32(ctx, 31);
        }
    }

    void state(bozo::state::Archive &a) { bozo::state::fields(a, g_frameOpen, g_lastFrameEnd); }

    void install(Registry &runtime)
    {
        runtime.registerFunction(addr::InitSys, &initSys);
        runtime.registerFunction(addr::GsDrainWait, &gsDrainWait);
        runtime.registerFunction(addr::PadSetCallback, &padSetCallback);
        runtime.registerFunction(addr::GsExecStoreImage, &gsDrainWait);
        runtime.registerFunction(addr::FrameKick, &frameKick);
        runtime.registerFunction(addr::OtWalk, &answered);
        runtime.registerFunction(addr::OtWalkByGroup, &answered);
        runtime.registerFunction(addr::DmaInit, &answered);
        for (uint32_t address : addr::ChannelDriver)
            runtime.registerFunction(address, &answered);
        runtime.registerFunction(addr::FromScratchpad, &fromScratchpad);
        runtime.registerFunction(addr::FromScratchpadStop, &answered);
        runtime.registerFunction(addr::ToScratchpadWait, &answered);
        runtime.registerFunction(addr::ToScratchpad, &toScratchpad);
        runtime.registerFunction(addr::ElapsedLines, &elapsedLines);
        runtime.registerFunction(addr::SetDisplay, &answered);
        runtime.registerFunction(addr::SetDisplayFields, &answered);
        runtime.registerFunction(addr::VblankHandler, &vblankHandler);
        runtime.registerFunction(addr::FileUpload, &fileUpload);
        runtime.registerFunction(addr::AdxmVsyncSleep, &adxmVsyncSleep);
    }
}
