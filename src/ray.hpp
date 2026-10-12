#ifndef RAY_HPP
#define RAY_HPP

#include "helper.hpp"
#include "math/math.hpp"

struct ray_t {
  static ray_t create(math::vec3 origin, math::vec3 direction) {
    ray_t ray;
    ray.origin            = origin;
    ray.direction         = direction;
    ray.inverse_direction = math::vec3(safe_inverse(direction.x),  //
                                       safe_inverse(direction.y),  //
                                       safe_inverse(direction.z));
    ray.tmax              = 1e30;
    ray.tmin              = 0.0001;
    return ray;
  }

  static ray_t create(const math::vec2 &uv,              //
                      const math::mat4 &inv_projection,  //
                      const math::mat4 &inv_view) {
    const math::vec3 origin = math::vec3{inv_view[3]};
    const math::vec2 xy     = uv * 2.f - 1.f;  // [-1, 1]
    math::vec4       view   = inv_projection * math::vec4(xy, 0.f, 1.f);
    view /= view.w;
    view                       = math::vec4{view.x, view.y, -1.f, 0.f};
    const math::vec3 direction = math::normalize(math::vec3{inv_view * view});
    return ray_t::create(origin, direction);
  }

  math::vec3 origin, direction, inverse_direction;
  float      tmin, tmax;
};

struct hit_t {
  bool        did_hit{false};
  float       t{0.f};
  math::ivec3 position{0};
  uint32_t    value{0};
};

#endif
