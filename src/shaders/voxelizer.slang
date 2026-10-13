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

float3 map_to_ndc(float3 value, float3 range_min, float3 range_max) {
  return ((value - range_min) / (range_max - range_min)) * 2.0 - 1.0;
}

struct vertex_stage_output_t {
  float4 sv_position : SV_Position;
  float3 ndc;
  float2 uv;
  float3 world_normal;
};

[shader("vertex")]
vertex_stage_output_t vertex_main(uint32_t id : SV_VertexID) {
  vertex_t v = pc.mesh_data.vertices[pc.mesh_data.indices[id]];
  vertex_stage_output_t o;
  float3 world = mul(float4(v.position, 1), pc.mesh_data.model_mat).xyz;
  float3 ndc   = map_to_ndc(world, pc.grid_min, pc.grid_max);
  o.ndc = ndc;
  float2 screen_xy = ndc.xy;
  if (pc.axis == 0) screen_xy = ndc.yz;
  if (pc.axis == 1) screen_xy = ndc.xz;
  if (pc.axis == 2) screen_xy = ndc.xy;
  o.sv_position = float4(screen_xy, 0.5, 1.0);
  o.uv = v.uv;
  o.world_normal = mul(float4(v.normal, 0), pc.mesh_data.model_mat).xyz;
  return o;
}

[shader("fragment")]
void fragment_main(vertex_stage_output_t i) {
  float3 uvw = i.ndc * 0.5 + 0.5;
  int3 e = int3(pc.extent.xxx);
  int3 voxel_pos = clamp(int3(uvw * float3(e)), int3(0), e - int3(1));
  float3 axis = pc.axis == 0   ? float3(1, 0, 0)
             : pc.axis == 1   ? float3(0, 1, 0)
                                     : float3(0, 0, 1);
  float shade = 0.75 + 0.25 * abs(dot(normalize(i.world_normal), axis));

  float3 albedo = textures[pc.mesh_data.diffuse]
                      .Sample(samplers[pc.s], i.uv)
                      .rgb;
  float3 color = albedo * shade;
  uint3 u = uint3(asuint(color.r), asuint(color.g), asuint(color.b));
  InterlockedMax(storage_images[pc.r][voxel_pos], u.r);
  InterlockedMax(storage_images[pc.g][voxel_pos], u.g);
  InterlockedMax(storage_images[pc.b][voxel_pos], u.b);
}
