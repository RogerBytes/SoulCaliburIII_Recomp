// What the game loads, told to the subsystems that find their data by where it was read from (ROOT.OLK's leaves): its first
// index request (0x150418: ROOT.OLK's sector), each sceCdRead, each memcpy of a model.
#include "sc3_draw.h"
#include "sc3_meshes.h"
#include "sc3_textures.h"

#include <cstdio>
#include <cstdlib>

namespace sc3::loads
{
    namespace
    {
        void fileRequest(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            textures::noteRequest(ctx);
            meshes::noteRequest(ctx);
            twoD::noteRequest(rdram, ctx, runtime);
            fn::FileRequest(rdram, ctx, runtime);
        }

        void cdRead(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            static const bool trace = std::getenv("SC3_TRACE_CD") != nullptr;
            if (trace)
                std::fprintf(stderr, "[sc3:cd] f%u %x %x %x\n", *reinterpret_cast<const uint32_t *>(rdram + addr::FrameNumber), getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6));
            const R5900Context arguments = *ctx;
            textures::noteReadBefore(rdram, ctx);
            meshes::noteReadBefore(rdram, ctx);
            twoD::noteReadBefore(rdram, ctx, runtime);
            bozo::port::callLibrary(runtime, ctx, addr::CdRead);
            meshes::noteReadAfter(rdram, const_cast<R5900Context *>(&arguments));
            twoD::noteReadAfter(rdram, const_cast<R5900Context *>(&arguments), runtime);
        }

        void copy(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            textures::noteCopy(rdram, ctx);
            meshes::noteCopy(rdram, ctx);
            fn::Copy(rdram, ctx, runtime);
        }
    }

    void install(Registry &runtime)
    {
        runtime.registerFunction(addr::FileRequest, &fileRequest);
        runtime.registerFunction(addr::CdRead, &cdRead);
        runtime.registerFunction(addr::Copy, &copy);
    }
}
