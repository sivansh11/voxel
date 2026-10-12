#ifndef GRID_HPP
#define GRID_HPP

#include <cstdint>
#include <limits>
#include <vector>

#include "math/math.hpp"
#include "math/random.hpp"

#include "helper.hpp"
#include "ray.hpp"
#include "aabb.hpp"

struct grid_t {
  uint32_t extent;
  // value of 0 is air
  std::vector<uint32_t> values;

  grid_t(uint32_t extent) : extent(extent) {
    values.resize(extent * extent * extent);
  }

  bool contains(const math::ivec3 &pos) const {
    return pos.x >= 0 && pos.x < int32_t(extent) &&  //
           pos.y >= 0 && pos.y < int32_t(extent) &&  //
           pos.z >= 0 && pos.z < int32_t(extent);
  }

  uint32_t local_index(const math::ivec3 &pos) const {
    return pos.x + pos.y * extent + pos.z * extent * extent;
  }

  bool set(const math::ivec3 &pos, uint32_t value) {
    if (!contains(pos)) return false;
    uint32_t i = local_index(pos);
    values[i]  = value;
    return true;
  }

  uint32_t get(const math::ivec3 &pos) const {
    if constexpr (false) {
      if (!contains(pos)) return 0;
    }
    return values[local_index(pos)];
  }
};

struct dda_grid_t {
  math::ivec3 current;  // current 3d pos of the ray
  math::ivec3 step;     // +1, -1, 0
  math::vec3  tdelta;   // how much in t to move 1.0 unit
  math::vec3  tmax;  // weird naming, but presents how much to move in t to hit
                     // the next boundary
};

inline dda_grid_t create_dda_grid(const ray_t &ray) {
  dda_grid_t dda{};
  dda.current = math::ivec3{math::floor(ray.origin)};
  dda.step    = math::sign(ray.direction);
  dda.tdelta  = math::abs(inverse(ray.direction));
  if (dda.step.x > 0) {
    // ray is moving right
    dda.tmax.x = (dda.current.x + 1.f - ray.origin.x) * dda.tdelta.x;
  } else if (dda.step.x < 0) {
    // ray is moving left
    dda.tmax.x = (ray.origin.x - dda.current.x) * dda.tdelta.x;
  } else {
    dda.tmax.x = std::numeric_limits<float>::max();
  }
  if (dda.step.y > 0) {
    dda.tmax.y = (dda.current.y + 1.f - ray.origin.y) * dda.tdelta.y;
  } else if (dda.step.y < 0) {
    dda.tmax.y = (ray.origin.y - dda.current.y) * dda.tdelta.y;
  } else {
    dda.tmax.y = std::numeric_limits<float>::max();
  }
  if (dda.step.z > 0) {
    dda.tmax.z = (dda.current.z + 1.f - ray.origin.z) * dda.tdelta.z;
  } else if (dda.step.z < 0) {
    dda.tmax.z = (ray.origin.z - dda.current.z) * dda.tdelta.z;
  } else {
    dda.tmax.z = std::numeric_limits<float>::max();
  }
  return dda;
}

inline float dda_advance(dda_grid_t &dda) {
  const int cmp_xy = dda.tmax.x < dda.tmax.y;
  const int cmp_xz = dda.tmax.x < dda.tmax.z;
  const int cmp_yz = dda.tmax.y < dda.tmax.z;

  const int mask_x = cmp_xy & cmp_xz;
  const int mask_y = (1 - cmp_xy) & cmp_yz;
  const int mask_z = (1 - cmp_xz) & (1 - cmp_yz);

  const float t = mask_x * dda.tmax.x +  //
                  mask_y * dda.tmax.y +  //
                  mask_z * dda.tmax.z;

  dda.current.x += mask_x * dda.step.x;
  dda.current.y += mask_y * dda.step.y;
  dda.current.z += mask_z * dda.step.z;

  dda.tmax.x += mask_x * dda.tdelta.x;
  dda.tmax.y += mask_y * dda.tdelta.y;
  dda.tmax.z += mask_z * dda.tdelta.z;

  return t;
}

inline hit_t trace(ray_t         ray,  //
                   const grid_t &grid) {
  hit_t hit{};

  math::vec3 grid_min{0.f};
  math::vec3 grid_max{float(grid.extent)};
  aabb_hit_t aabb_hit = intersect_aabb(grid_min, grid_max, ray);
  if (!aabb_hit.did_intersect()) return hit;

  if (aabb_hit.tmin > 0.f) {
    ray.origin = ray.origin + ray.direction * aabb_hit.tmin;
    ray.origin += ray.direction * 0.0001f;  // nudging a bit
  }

  dda_grid_t dda = create_dda_grid(ray);

  float entry = 0.f;

  while (grid.contains(dda.current)) {
    uint32_t value = grid.get(dda.current);
    if (value) {
      hit.did_hit  = true;
      hit.value    = value;
      hit.position = dda.current;
      hit.t        = aabb_hit.tmin + entry;
      return hit;
    }
    entry = dda_advance(dda);
  }

  return hit;
}

inline void generate(grid_t &grid, uint32_t seed = 1337) {
  math::random_t rng{};
  rng.generator.seed(seed);

  const float e = float(grid.extent);
  for (int blob = 0; blob < 40; blob++) {
    const math::vec3 center = rng.random_vec<3>(0.15f * e, 0.85f * e);
    const float      radius = rng.random_float(0.02f * e, 0.06f * e);

    const float       max_radius = radius * 1.35f;
    const math::ivec3 lo{math::ivec3{math::floor(center - max_radius)}};
    const math::ivec3 hi{math::ivec3{math::ceil(center + max_radius)}};

    for (int32_t z = lo.z; z <= hi.z; z++) {
      for (int32_t y = lo.y; y <= hi.y; y++) {
        for (int32_t x = lo.x; x <= hi.x; x++) {
          const math::ivec3 cell{x, y, z};
          if (!grid.contains(cell)) continue;
          const math::vec3 delta = math::vec3{cell} - center;
          const float      d     = math::length(delta);
          const float      lump  = 1.f + 0.35f * math::sin(d * 0.35f);
          if (d > radius * lump) continue;

          const auto to_u8 = [](float v) {
            return uint32_t(math::clamp(v, 0.f, 1.f) * 255.f);
          };
          const math::vec3 off_white{0.94f, 0.93f, 0.9f};
          const float      variation = rng.random_float(0.92f, 1.f);
          grid.set(cell, 0xff000000u | (to_u8(off_white.x * variation) << 0) |
                             (to_u8(off_white.y * variation) << 8) |
                             (to_u8(off_white.z * variation) << 16));
        }
      }
    }
  }
}

#endif
