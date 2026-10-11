#ifndef HELPER_HPP
#define HELPER_HPP

#include "math/math.hpp"

inline float safe_inverse(float x) {
  const float epsilon    = 0.0001;
  const float abs_x      = math::abs(x);
  const float safe_abs_x = math::max(abs_x, epsilon);
  const float sign_x     = x >= 0.f ? 1.f : -1.f;
  return sign_x / safe_abs_x;
}

// maybe should be named hamrad inverse or some shi ?
inline math::vec3 inverse(math::vec3 vec) {
  return math::vec3{
      safe_inverse(vec.x),  //
      safe_inverse(vec.y),  //
      safe_inverse(vec.z),
  };
}

#endif
