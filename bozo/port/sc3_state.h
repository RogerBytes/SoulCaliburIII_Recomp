// Save states: what SC3's subsystems keep the same way.
#pragma once

#include "bozo/render/mesh_pack.h"
#include "bozo/state.h"

#include <vector>

namespace sc3
{
    inline void imagesState(bozo::state::Archive &a, bozo::render::packs::LoadedImages &loaded, const bozo::render::packs::MeshPack &pack)
    {
        std::vector<bozo::render::packs::LoadedImages::Image> images;
        if (!a.loading())
            images = loaded.images();
        const uint64_t n = bozo::state::count(a, images.size());
        if (a.loading())
            images.assign(n, {});
        for (auto &image : images)
        {
            bool file = image.file != nullptr;
            bozo::state::fields(a, image.base, image.size, image.index, file);
            if (a.loading())
            {
                const auto &files = pack.files();
                if (file && image.index >= files.size())
                    throw bozo::state::Error("save state: a loaded file the pack does not have");
                image.file = file ? &files[image.index] : nullptr;
            }
        }
        if (a.loading())
            loaded.restore(std::move(images));
    }

    template <class Leaf>
    void leavesState(bozo::state::Archive &a, std::vector<Leaf> &leaves)
    {
        if (bozo::state::count(a, leaves.size()) != leaves.size())
            throw bozo::state::Error("save state: another ROOT.OLK leaf list");
        for (Leaf &leaf : leaves)
            bozo::state::fields(a, leaf.base, leaf.covered, leaf.bound);
    }
}
