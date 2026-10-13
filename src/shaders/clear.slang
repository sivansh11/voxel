struct vertex_t {
  float3 position;
  float3 normal;
  float2 uv;
  float3 tangent;
  float3 bi_tangent;
};

struct mesh_data_t {
  float4x4  model_mat;
  vertex_t *vertices;
  uint32_t *indices;
  uint32_t  index_count;
  uint32_t  diffuse;
};

struct push_constant_t {
  mesh_data_t *mesh_data;
  float3       grid_min;
  float        pad1;
  float3       grid_max;
  float        pad2;
  uint32_t     r;
  uint32_t     g;
  uint32_t     b;
  uint32_t     s;
  uint32_t     axis;
  uint32_t     extent;
};

[vk::push_constant]
push_constant_t pc;

[vk::binding(0, 0)]
uniform Texture2D textures[1000];
[vk::binding(1, 0)]
uniform SamplerState samplers[1000];
[vk::binding(2, 0)]
RWTexture3D<uint> storage_images[1000];

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 tid : SV_DispatchThreadID) {
  storage_images[pc.r][int3(tid)] = 0u;
  storage_images[pc.g][int3(tid)] = 0u;
  storage_images[pc.b][int3(tid)] = 0u;
}
