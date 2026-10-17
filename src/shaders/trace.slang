struct camera_t {
  float4x4 view;
  float4x4 inv_view;
  float4x4 projection;
  float4x4 inv_projection;
};

struct node_t {
  uint64_t mask  = 0;
  uint32_t first = 0;
  // for future
  uint32_t meta = 0;
};

struct tree64_t {
  // Note: 0 node_id is invalid
  node_t *nodes;
  // indices contains both indices and data for leaf nodes
  uint32_t *indices;

  uint32_t num_levels;
  uint32_t extent;

  bool contains(const int3 pos) {
    return pos.x >= 0 && pos.x < int32_t(extent) &&  //
           pos.y >= 0 && pos.y < int32_t(extent) &&  //
           pos.z >= 0 && pos.z < int32_t(extent);
  }

  uint8_t local_index(const int3 pos, uint32_t level) {
    uint32_t mask = 0b11u << (level * 2);
    uint32_t x    = (uint32_t(pos.x) & mask) >> (level * 2);
    uint32_t y    = (uint32_t(pos.y) & mask) >> (level * 2);
    uint32_t z    = (uint32_t(pos.z) & mask) >> (level * 2);
    return uint8_t(x + y * 4 + z * 16);
  }
};

struct aabb_hit_t {
  bool did_intersect() {
    return tmin <= tmax;
  }
  float tmin, tmax;
  float3 normal;
};

struct hit_t {
  bool did_intersect() { return did_hit; }

  bool        did_hit;
  float       t;
  int3        position;
  int3        normal;
  uint32_t    value;
#ifdef DEBUG_HIT
// TODO: add
#endif
};

float safe_inverse(float x) {
  const float epsilon = 0.0001;
  const float abs_x = abs(x);
  const float safe_abs_x = max(abs_x, epsilon);
  const float sign_x = x >= 0.f ? 1.f : -1.f;
  return sign_x / safe_abs_x;
}

struct ray_t {
  static ray_t create(float3 origin, float3 direction) {
    ray_t ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.inverse_direction = float3(
      safe_inverse(direction.x),
      safe_inverse(direction.y),
      safe_inverse(direction.z),
    );
    ray.tmax = 1e30f;
    ray.tmin = 0.0001;
    return ray;
  }
  static ray_t create(const float2 uv, 
                      const float4x4 inv_projection, 
                      const float4x4 inv_view) {
    const float3 origin = inv_view[3].xyz;
    const float2 xy = uv * 2.f - 1.f; // [-1, 1]
    const float4 clip = float4(xy, -1, 1);
    float4 view = mul(clip, inv_projection);
    view = float4(view.xy, -1, 0);
    const float3 dir = normalize(mul(view, inv_view).xyz);
    return ray_t::create(origin, dir);
  }
  float3 origin, direction, inverse_direction;
  float tmax, tmin;
};

struct push_constant_t {
  camera_t *camera;
  tree64_t *tree64;

  uint32_t width;
  uint32_t height;
  uint32_t bindless_image;
};

[vk::push_constant]
push_constant_t pc;

[vk::binding(0, 0)]
uniform Texture2D textures[1000];
[vk::binding(1, 0)]
uniform SamplerState samplers[1000];
[vk::binding(2, 0)]
uniform RWTexture2D rwtextures[1000];

struct dda_tree64_t {
  int3    current;
  int3    step;
  float3  tdelta;
  float3  tmax;
  int3    mask;
  int     current_level;
};

inline float3 inverse(float3 vec) {
  return float3(
      safe_inverse(vec.x),  //
      safe_inverse(vec.y),  //
      safe_inverse(vec.z),
  );
}

inline dda_tree64_t create_dda_tree64(const ray_t ray) {
  dda_tree64_t dda;
  dda.current = int3(floor(ray.origin));
  dda.step    = sign(ray.direction);
  dda.tdelta  = abs(inverse(ray.direction));
  if (dda.step.x > 0) {
    // ray is moving right
    dda.tmax.x = (dda.current.x + 1.f - ray.origin.x) * dda.tdelta.x;
  } else if (dda.step.x < 0) {
    // ray is moving left
    dda.tmax.x = (ray.origin.x - dda.current.x) * dda.tdelta.x;
  } else {
    dda.tmax.x = 1e30f;
  }
  if (dda.step.y > 0) {
    dda.tmax.y = (dda.current.y + 1.f - ray.origin.y) * dda.tdelta.y;
  } else if (dda.step.y < 0) {
    dda.tmax.y = (ray.origin.y - dda.current.y) * dda.tdelta.y;
  } else {
    dda.tmax.y = 1e30f;
  }
  if (dda.step.z > 0) {
    dda.tmax.z = (dda.current.z + 1.f - ray.origin.z) * dda.tdelta.z;
  } else if (dda.step.z < 0) {
    dda.tmax.z = (ray.origin.z - dda.current.z) * dda.tdelta.z;
  } else {
    dda.tmax.z = 1e30f;
  }
  return dda;
}

inline float dda_advance(inout dda_tree64_t dda, const ray_t ray) {
  const int32_t current_extent = 1 << (dda.current_level * 2);

  if (dda.step.x > 0) {
    dda.tmax.x =
        (float(dda.current.x + current_extent) - ray.origin.x) * dda.tdelta.x;
  } else if (dda.step.x < 0) {
    dda.tmax.x = (ray.origin.x - float(dda.current.x)) * dda.tdelta.x;
  } else {
    dda.tmax.x = 1e30f;
  }
  if (dda.step.y > 0) {
    dda.tmax.y =
        (float(dda.current.y + current_extent) - ray.origin.y) * dda.tdelta.y;
  } else if (dda.step.y < 0) {
    dda.tmax.y = (ray.origin.y - float(dda.current.y)) * dda.tdelta.y;
  } else {
    dda.tmax.y = 1e30f;
  }
  if (dda.step.z > 0) {
    dda.tmax.z =
        (float(dda.current.z + current_extent) - ray.origin.z) * dda.tdelta.z;
  } else if (dda.step.z < 0) {
    dda.tmax.z = (ray.origin.z - float(dda.current.z)) * dda.tdelta.z;
  } else {
    dda.tmax.z = 1e30f;
  }

  const int cmp_xy = dda.tmax.x < dda.tmax.y;
  const int cmp_xz = dda.tmax.x < dda.tmax.z;
  const int cmp_yz = dda.tmax.y < dda.tmax.z;

  const int mask_x = cmp_xy & cmp_xz;
  const int mask_y = (1 - cmp_xy) & cmp_yz;
  const int mask_z = (1 - cmp_xz) & (1 - cmp_yz);

  dda.mask = int3(mask_x, mask_y, mask_z);

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
  while (level < int32_t(pc.tree64->num_levels) - 1 &&
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

inline aabb_hit_t intersect_aabb(const float3 _min, const float3 _max,
                                 const ray_t ray) {
  float3       tmin     = (_min - ray.origin) * ray.inverse_direction;
  float3       tmax     = (_max - ray.origin) * ray.inverse_direction;
  const float3 old_tmin = tmin;
  const float3 old_tmax = tmax;
  tmin                      = min(old_tmin, old_tmax);
  tmax                      = max(old_tmin, old_tmax);
  float _tmin =
      max(tmin[0], max(tmin[1], max(tmin[2], ray.tmin)));
  float _tmax =
      min(tmax[0], min(tmax[1], min(tmax[2], ray.tmax)));
  aabb_hit_t hit = aabb_hit_t(_tmin, _tmax);

  hit.normal =
      int3(int(tmin.x == _tmin) * int(ray.direction.x < 0.f ? 1.f : -1.f),
                  int(tmin.y == _tmin) * int(ray.direction.y < 0.f ? 1.f : -1.f),
                  int(tmin.z == _tmin) * int(ray.direction.z < 0.f ? 1.f : -1.f));

  return hit;
}

inline hit_t trace(ray_t     ray,  //
                   tree64_t *tree64) {
  hit_t hit = hit_t();

  float3 tree64_min = 0;
  float3 tree64_max = float(pc.tree64->extent);
  aabb_hit_t aabb_hit = intersect_aabb(tree64_min, tree64_max, ray);
  if (!aabb_hit.did_intersect()) return hit;

  float3 normal = aabb_hit.normal;

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
  // uint32_t stack[pc.num_levels];
  // TODO: get num_levels from shader constant ?
  uint32_t stack[8];
  stack[dda.current_level] = 1; // root_id is always 1

  float entry = 0.f;

  while (tree64.contains(dda.current)) {
    const uint8_t i    = tree64.local_index(dda.current, dda.current_level);
    const node_t node = tree64.nodes[stack[dda.current_level]];

    if (bool((node.mask >> i) & 0b1)) {
      if (dda.current_level == 0) {
        hit.did_hit  = true;
        hit.value    = tree64.indices[node.first + i];
        hit.position = dda.current;
        hit.t        = aabb_hit.tmin + entry;

        if (all(dda.mask == int3(0))) {
          hit.normal = int3(normal);
        } else {
          hit.normal = int3(
              -int(dda.mask.x * dda.step.x),  //
              -int(dda.mask.y * dda.step.y),  //
              -int(dda.mask.z * dda.step.z)   //
            );
        }
        return hit;
      }
      stack[dda.current_level - 1] = tree64.indices[node.first + i];
      dda.current_level--;
      const int32_t current_extent = 1 << (dda.current_level * 2);
      float3    pos            = ray.origin + ray.direction * entry;
      int3   child          = int3(clamp(
          floor((pos - float3(dda.current)) * (1.f / current_extent)),
          0.f, 3.f));
      dda.current += child << (dda.current_level * 2);
      continue;
    }
    entry = dda_advance(dda, ray);
  }
  return hit;
}


inline float3 sky_color(const float3 direction) {
  const float  t          = clamp(direction.y * 0.5f + 0.5f, 0.f, 1.f);
  const float3 sky_top    = float3(0.25f, 0.45f, 0.85f);
  const float3 sky_bottom = float3(0.65f, 0.70f, 0.78f);
  return lerp(sky_bottom, sky_top, t);
}

inline float3 unpack_color(const uint32_t value) {
  return float3(float((value >> 0) & 0xffu),   //
                float((value >> 8) & 0xffu),   //
                float((value >> 16) & 0xffu)) /
         255.f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex) {
  if (dispatch_thread_id.x >= pc.width || dispatch_thread_id.y >= pc.height) return;
  const float u = (float(dispatch_thread_id.x) + 0.5f) / float(pc.width);
  const float v = (float(dispatch_thread_id.y) + 0.5f) / float(pc.height);

  ray_t ray = ray_t::create(float2(u, v), pc.camera->inv_projection, pc.camera->inv_view);

  hit_t hit = trace(ray, pc.tree64);

  float3 color;
  if (hit.did_intersect()) {
    color = unpack_color(hit.value);
  } else {
    color = sky_color(ray.direction);
  }
  rwtextures[pc.bindless_image][uint2(dispatch_thread_id.xy)] = float4(color, 1.f);
}
