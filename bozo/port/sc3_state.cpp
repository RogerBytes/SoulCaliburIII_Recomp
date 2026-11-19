// Save states: SC3's own section (BozoEngine/docs/save-states.md).
#include "sc3_draw.h"
#include "sc3_meshes.h"
#include "sc3_state.h"
#include "sc3_textures.h"

namespace sc3
{
    void state(bozo::state::Archive &a, Runtime *runtime)
    {
        a.begin("clock");
        clock::state(a);
        a.end();
        a.begin("frame");
        frame::state(a);
        a.end();
        a.begin("textures");
        textures::state(a, runtime->memory.ram());
        a.end();
        a.begin("meshes");
        meshes::state(a);
        a.end();
        a.begin("2d");
        twoD::state(a, runtime);
        a.end();
        a.begin("offscreen");
        offscreen::state(a);
        a.end();
        if (!a.loading())
            return;
        ot::stateLoaded();
        movies::stateLoaded();
        draw::releaseKeyed();
    }

    std::string unsaveable() { return movies::unsaveable(); }
}
