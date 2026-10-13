#ifndef WORLD_HPP
#define WORLD_HPP

#include <vector>

#include "horizon/core/logger.hpp"
#include "tree64.hpp"
#include "voxelizer.hpp"

struct world_t {
  world_t() {
    // math::random_t   rng{};
    // const math::vec3 off_white{0.94f, 0.93f, 0.9f};
    // const auto       to_u8 = [](float v) {
    //   return uint32_t(math::clamp(v, 0.f, 1.f) * 255.f);
    // };
    // auto e = 1024;
    // for (uint32_t i = 0; i < e; i++) {
    //   for (uint32_t j = 0; j < e; j++) {
    //     math::ivec3 pos       = {i, 0, j};
    //     const float variation = rng.random_float(0.5f, 1.f);
    //     tree.set(pos, 0xff000000u | (to_u8(variation) << 0) |
    //                       (to_u8(variation) << 8) |
    //                       (to_u8(variation) << 16));
    //   }
    // }
  }

  world_t(const std::vector<voxel_t> &voxels) {
    for (const voxel_t &voxel : voxels) {
      tree.set(voxel.pos, voxel.val);
    }
  }

  ~world_t() {}

  tree64_t tree;
};

#endif
