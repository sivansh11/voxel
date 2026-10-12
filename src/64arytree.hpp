#ifndef _64ARY_TREE_HPP
#define _64ARY_TREE_HPP

#include <cstdint>
#include <limits>
#include <vector>

#include "math/math.hpp"
#include "math/random.hpp"

#include "helper.hpp"
#include "ray.hpp"
#include "aabb.hpp"

struct node_t {
  uint64_t mask  = 0;
  uint32_t first = 0;
  // for future
  uint32_t meta = 0;
};

struct tree64_t {
  static constexpr uint32_t num_levels = 5;
  static constexpr uint32_t extent     = 1u << (num_levels * 2);
  // Note: 0 node_id is invalid
  std::vector<node_t> nodes;
  // indices contains both indices and data for leaf nodes
  std::vector<uint32_t> indices;

  static constexpr uint32_t root_id = 1;

  tree64_t() {
    alloc_node();  // 0
    alloc_node();  // 1
  }

  uint32_t alloc_node() {
    uint32_t node_id = nodes.size();
    node_t   node{};
    node.mask  = 0;
    node.first = indices.size();
    node.meta  = 0;
    nodes.emplace_back(node);
    for (uint32_t i = 0; i < 64; i++) indices.emplace_back(0);
    return node_id;
  }

  uint8_t local_index(const math::ivec3 &pos, uint32_t level) const {
    uint32_t mask = 0b11u << (level * 2);
    uint32_t x    = (uint32_t(pos.x) & mask) >> (level * 2);
    uint32_t y    = (uint32_t(pos.y) & mask) >> (level * 2);
    uint32_t z    = (uint32_t(pos.z) & mask) >> (level * 2);
    return uint8_t(x + y * 4 + z * 16);
  }

  bool contains(const math::ivec3 &pos) const {
    return pos.x >= 0 && pos.x < int32_t(extent) &&  //
           pos.y >= 0 && pos.y < int32_t(extent) &&  //
           pos.z >= 0 && pos.z < int32_t(extent);
  }

  bool set(const math::ivec3 &pos, uint32_t value) {
    if (!contains(pos)) return false;
    uint32_t node_id = root_id;
    for (int32_t level = num_levels - 1; level > 0; level--) {
      uint8_t i = local_index(pos, level);
      if (!((nodes[node_id].mask >> i) & 0b1)) {
        const uint32_t new_node_id        = alloc_node();
        indices[nodes[node_id].first + i] = new_node_id;
        nodes[node_id].mask |= 1ull << i;
      }
      node_id = indices[nodes[node_id].first + i];
      if (!node_id) return false;
    }
    uint8_t i                         = local_index(pos, 0);
    indices[nodes[node_id].first + i] = value;
    nodes[node_id].mask |= 1ull << i;
    return true;
  }
};

struct dda_tree64_t {
  math::ivec3 current;  // current 3d pos of the ray
  math::ivec3 step;     // +1, -1, 0
  math::vec3  tdelta;   // how much in t to move 1.0 unit
  math::vec3  tmax;  // weird naming, but presents how much to move in t to hit
                     // the next boundary
  int32_t current_level;  // current_level used for advancing
};

inline dda_tree64_t create_dda_tree64(const ray_t &ray) {
  dda_tree64_t dda{};
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

inline float dda_advance(dda_tree64_t &dda, const ray_t &ray) {
  const int32_t current_extent = 1 << (dda.current_level * 2);

  if (dda.step.x > 0) {
    dda.tmax.x =
        (float(dda.current.x + current_extent) - ray.origin.x) * dda.tdelta.x;
  } else if (dda.step.x < 0) {
    dda.tmax.x = (ray.origin.x - float(dda.current.x)) * dda.tdelta.x;
  } else {
    dda.tmax.x = std::numeric_limits<float>::max();
  }
  if (dda.step.y > 0) {
    dda.tmax.y =
        (float(dda.current.y + current_extent) - ray.origin.y) * dda.tdelta.y;
  } else if (dda.step.y < 0) {
    dda.tmax.y = (ray.origin.y - float(dda.current.y)) * dda.tdelta.y;
  } else {
    dda.tmax.y = std::numeric_limits<float>::max();
  }
  if (dda.step.z > 0) {
    dda.tmax.z =
        (float(dda.current.z + current_extent) - ray.origin.z) * dda.tdelta.z;
  } else if (dda.step.z < 0) {
    dda.tmax.z = (ray.origin.z - float(dda.current.z)) * dda.tdelta.z;
  } else {
    dda.tmax.z = std::numeric_limits<float>::max();
  }

  const int cmp_xy = dda.tmax.x < dda.tmax.y;
  const int cmp_xz = dda.tmax.x < dda.tmax.z;
  const int cmp_yz = dda.tmax.y < dda.tmax.z;

  const int mask_x = cmp_xy & cmp_xz;
  const int mask_y = (1 - cmp_xy) & cmp_yz;
  const int mask_z = (1 - cmp_xz) & (1 - cmp_yz);

  // dda.current.x += mask_x * dda.step.x;
  // dda.current.y += mask_y * dda.step.y;
  // dda.current.z += mask_z * dda.step.z;

  const float t = mask_x * dda.tmax.x +  //
                  mask_y * dda.tmax.y +  //
                  mask_z * dda.tmax.z;

  // dda.tmax.x += mask_x * dda.tdelta.x;
  // dda.tmax.y += mask_y * dda.tdelta.y;
  // dda.tmax.z += mask_z * dda.tdelta.z;

  int32_t axis = mask_y + (mask_z << 1);

  const int32_t old_current_on_axis = dda.current[axis];
  const int32_t new_current_on_axis =
      old_current_on_axis + dda.step[axis] * current_extent;

  // goes up the tree till old and new level masked bits are same
  int32_t level = dda.current_level;
  while (level < int32_t(tree64_t::num_levels) - 1 &&
         (old_current_on_axis >> ((level + 1) * 2)) !=
             (new_current_on_axis >> ((level + 1) * 2))) {
    level++;
  }

  if (level == dda.current_level) {
    dda.current[axis] = new_current_on_axis;
  } else {
    // went up, recompute dda_tree64_t
    dda.current_level   = level;
    const int32_t shift = level * 2;
    dda.current         = (dda.current >> shift) << shift;
    dda.current[axis]   = (new_current_on_axis >> shift) << shift;
  }

  return t;
}

inline hit_t trace(ray_t           ray,  //
                   const tree64_t &tree64) {
  hit_t hit{};

  math::vec3 tree64_min{0.f};
  math::vec3 tree64_max{float(tree64.extent)};
  aabb_hit_t aabb_hit = intersect_aabb(tree64_min, tree64_max, ray);
  if (!aabb_hit.did_intersect()) return hit;

  if (aabb_hit.tmin > 0.f) {
    ray.origin = ray.origin + ray.direction * aabb_hit.tmin;
    ray.origin += ray.direction * 0.001f;  // nudging a bit
  }

  dda_tree64_t dda  = create_dda_tree64(ray);
  dda.current_level = tree64.num_levels - 1;

  {
    const int32_t shift = (tree64.num_levels - 1) * 2;
    dda.current         = (dda.current >> shift) << shift;
  }

  // num_levels is constexpr
  uint32_t stack[tree64_t::num_levels];
  stack[dda.current_level] = tree64.root_id;

  float entry = 0.f;

  while (tree64.contains(dda.current)) {
    const uint8_t i    = tree64.local_index(dda.current, dda.current_level);
    const node_t &node = tree64.nodes[stack[dda.current_level]];

    if ((node.mask >> i) & 0b1) {
      if (dda.current_level == 0) {
        hit.did_hit  = true;
        hit.value    = tree64.indices[node.first + i];
        hit.position = dda.current;
        hit.t        = aabb_hit.tmin + entry;
        return hit;
      }
      stack[dda.current_level - 1] = tree64.indices[node.first + i];
      dda.current_level--;
      const int32_t current_extent = 1 << (dda.current_level * 2);
      math::vec3    pos            = ray.origin + ray.direction * entry;
      math::ivec3   child          = math::clamp(
          math::floor((pos - math::vec3{dda.current}) * (1.f / current_extent)),
          0.f, 3.f);
      dda.current += child << (dda.current_level * 2);
      continue;
    }
    entry = dda_advance(dda, ray);
  }
  return hit;
}

inline void generate(tree64_t &tree64, uint32_t seed = 1337) {
  math::random_t rng{};
  rng.generator.seed(seed);

  const float e = float(tree64.extent);
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
          if (!tree64.contains(cell)) continue;
          const math::vec3 delta = math::vec3{cell} - center;
          const float      d     = math::length(delta);
          const float      lump  = 1.f + 0.35f * math::sin(d * 0.35f);
          if (d > radius * lump) continue;

          const auto to_u8 = [](float v) {
            return uint32_t(math::clamp(v, 0.f, 1.f) * 255.f);
          };
          const math::vec3 off_white{0.94f, 0.93f, 0.9f};
          const float      variation = rng.random_float(0.92f, 1.f);
          tree64.set(cell, 0xff000000u | (to_u8(off_white.x * variation) << 0) |
                               (to_u8(off_white.y * variation) << 8) |
                               (to_u8(off_white.z * variation) << 16));
        }
      }
    }
  }
}

#endif
