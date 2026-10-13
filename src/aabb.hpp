#ifndef AABB_HPP
#define AABB_HPP

#include "math/math.hpp"

#include "ray.hpp"

struct aabb_hit_t {
  bool       did_intersect() { return tmin <= tmax; }
  float      tmin, tmax;
  math::vec3 normal;
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

  hit.normal =
      math::ivec3{(tmin.x == _tmin) * (ray.direction.x < 0.f ? 1.f : -1.f),
                  (tmin.y == _tmin) * (ray.direction.y < 0.f ? 1.f : -1.f),
                  (tmin.z == _tmin) * (ray.direction.z < 0.f ? 1.f : -1.f)};

  return hit;
}

#endif
