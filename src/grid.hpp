#ifndef GRID_HPP
#define GRID_HPP

#include <cstdint>
#include <limits>
#include <vector>

#include "math/math.hpp"

#include "helper.hpp"
#include "ray.hpp"

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

struct dda_t {
  math::ivec3 current;  // current 3d pos of the ray
  math::ivec3 step;     // +1, -1, 0
  math::vec3  tdelta;   // how much in t to move 1.0 unit
  math::vec3  tmax;  // weird naming, but presents how much to move in t to hit
                     // the next boundary
};

inline dda_t create_dda(const ray_t &ray) {
  dda_t dda{};
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

inline math::ivec3 dda_advance(dda_t &dda) {
  const int cmp_xy = dda.tmax.x < dda.tmax.y;
  const int cmp_xz = dda.tmax.x < dda.tmax.z;
  const int cmp_yz = dda.tmax.y < dda.tmax.z;

  const int mask_x = cmp_xy & cmp_xz;
  const int mask_y = (1 - cmp_xy) & cmp_yz;
  const int mask_z = (1 - cmp_xz) & (1 - cmp_yz);

  dda.current.x += mask_x * dda.step.x;
  dda.current.y += mask_y * dda.step.y;
  dda.current.z += mask_z * dda.step.z;

  dda.tmax.x += mask_x * dda.tdelta.x;
  dda.tmax.y += mask_y * dda.tdelta.y;
  dda.tmax.z += mask_z * dda.tdelta.z;

  return {mask_x, mask_y, mask_z};
}

struct hit_t {
  bool        did_hit{false};
  float       t{0.f};
  math::ivec3 position{0};
  uint32_t    value{0};
};

struct aabb_hit_t {
  bool  did_intersect() { return tmin <= tmax; }
  float tmin, tmax;
};

inline aabb_hit_t intersect_aabb(const math::vec3 _min, const math::vec3 _max,
                                 const ray_t ray) {
  math::vec3       tmin     = (_min - ray.origin) * ray.inverse_direction;
  math::vec3       tmax     = (_max - ray.origin) * ray.inverse_direction;
  const math::vec3 old_tmin = tmin;
  const math::vec3 old_tmax = tmax;
  tmin                      = min(old_tmin, old_tmax);
  tmax                      = max(old_tmin, old_tmax);
  float _tmin =
      math::max(tmin[0], math::max(tmin[1], math::max(tmin[2], ray.tmin)));
  float _tmax =
      math::min(tmax[0], math::min(tmax[1], math::min(tmax[2], ray.tmax)));
  aabb_hit_t hit = aabb_hit_t(_tmin, _tmax);
  return hit;
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

  dda_t dda = create_dda(ray);

  math::ivec3 mask{0, 0, 0};

  while (grid.contains(dda.current)) {
    uint32_t value = grid.get(dda.current);
    if (value) {
      hit.did_hit  = true;
      hit.value    = value;
      hit.position = dda.current;

      if (mask.x != 0 || mask.y != 0 || mask.z != 0) {
        float t = mask.x * (dda.tmax.x - dda.tdelta.x) +
                  mask.y * (dda.tmax.y - dda.tdelta.y) +
                  mask.z * (dda.tmax.z - dda.tdelta.z);
        hit.t   = aabb_hit.tmin + t;
      } else {
        hit.t = aabb_hit.tmin;
      }
      return hit;
    }
    mask = dda_advance(dda);
  }

  return hit;
}

#endif
