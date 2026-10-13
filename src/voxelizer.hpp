#ifndef VOXELIZER_HPP
#define VOXELIZER_HPP

#include <algorithm>
#include <filesystem>
#include <vector>
#include "horizon/core/logger.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"
#include "horizon/gfx/types.hpp"
#include "horizon/gfx/helper.hpp"

#include "math/math.hpp"
#include "model/model.hpp"

struct voxel_t {
  math::ivec3 pos;
  uint32_t    val;
};

struct mesh_data_t {
  math::mat4                   model_mat;
  model::vertex_t             *vertices;
  uint32_t                    *indices;
  uint32_t                     index_count;
  gfx::handle_bindless_image_t diffuse;
};

struct mesh_t {
  math::mat4               model_mat;
  gfx::handle_buffer_t     vertices;
  gfx::handle_buffer_t     indices;
  uint32_t                 index_count;
  gfx::handle_image_t      diffuse;
  gfx::handle_image_view_t diffuse_view;
  gfx::handle_buffer_t     mesh_data;
};

struct push_constant_t {
  mesh_data_t                   *mesh_data;
  math::vec3                     grid_min;
  float                          pad1;
  math::vec3                     grid_max;
  float                          pad2;
  gfx::handle_bindless_image_t   r;
  gfx::handle_bindless_image_t   g;
  gfx::handle_bindless_image_t   b;
  gfx::handle_bindless_sampler_t s;
  uint32_t                       axis;
  uint32_t                       extent;
};

inline gfx::handle_image_t create_image_from_color(core::ref<gfx::base_t> base,
                                                   math::vec4 color) {
  gfx::config_buffer_t cb{};
  cb.vk_size               = 4;
  cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  cb.vma_allocation_create_flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  gfx::handle_buffer_t staging = base->create_buffer(cb);
  uint32_t             pixel   = uint8_t(color.x * 255.f) |        //
                                 uint8_t(color.y * 255.f) << 8 |   //
                                 uint8_t(color.z * 255.f) << 16 |  //
                                 uint8_t(color.w * 255.f) << 24;
  std::memcpy(base->map_buffer(staging), &pixel, sizeof(pixel));

  gfx::config_image_t ci{};
  ci.vk_width  = 1;
  ci.vk_height = 1;
  ci.vk_depth  = 1;
  ci.vk_type   = VK_IMAGE_TYPE_2D;
  ci.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
  ci.vk_usage  = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ci.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  ci.debug_name                  = "default";
  gfx::handle_image_t image      = base->create_image(ci);

  gfx::handle_commandbuffer_t cmd =
      gfx::helper::begin_single_use_commandbuffer(*base);
  gfx::helper::cmd_transition_image_layout(
      *base,                      //
      cmd,                        //
      image,                      //
      VK_IMAGE_LAYOUT_UNDEFINED,  //
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  // TODO: add custom VkBufferImageCopy with defaults
  base->cmd_copy_buffer_to_image(
      cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      {
          .imageSubresource =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .layerCount = 1,
              },
          .imageExtent = {1, 1, 1},
      });
  gfx::helper::cmd_transition_image_layout(
      *base,                                 //
      cmd,                                   //
      image,                                 //
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  //
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  gfx::helper::end_single_use_command_buffer(*base, cmd);
  return image;
}

inline gfx::handle_image_t load_diffuse_image(
    core::ref<gfx::base_t> base, const model::raw_mesh_t &raw_mesh) {
  {
    const auto itr = std::find_if(
        raw_mesh.material_description.texture_infos.begin(),
        raw_mesh.material_description.texture_infos.end(),
        [&](const model::texture_info_t &info) {
          return info.texture_type == model::texture_type_t::e_diffuse_map;
        });
    if (itr != raw_mesh.material_description.texture_infos.end()) {
      return gfx::helper::load_image_from_path_instant(*base, itr->file_path,
                                                       VK_FORMAT_R8G8B8A8_SRGB);
    }
  }
  {
    const auto itr = std::find_if(
        raw_mesh.material_description.texture_infos.begin(),
        raw_mesh.material_description.texture_infos.end(),
        [&](const model::texture_info_t &info) {
          return info.texture_type == model::texture_type_t::e_diffuse_color;
        });
    if (itr != raw_mesh.material_description.texture_infos.end()) {
      return create_image_from_color(base, itr->diffuse_color);
    }
  }
  return create_image_from_color(base, math::vec4{1.f});
}

inline std::vector<voxel_t> voxelize(core::ref<gfx::base_t>       base,
                                     const std::filesystem::path &path,
                                     uint32_t                     extent) {
  auto vs =
      gfx::helper::create_slang_shader(*base,                            //
                                       "./src/shaders/voxelizer.slang",  //
                                       gfx::shader_type_t::e_vertex);
  auto fs =
      gfx::helper::create_slang_shader(*base,                            //
                                       "./src/shaders/voxelizer.slang",  //
                                       gfx::shader_type_t::e_fragment);
  auto cs = gfx::helper::create_slang_shader(*base,                        //
                                             "./src/shaders/clear.slang",  //
                                             gfx::shader_type_t::e_compute);

  gfx::config_pipeline_layout_t cpl{};
  cpl.add_descriptor_set_layout(base->_bindless_descriptor_set_layout);
  cpl.add_push_constant(sizeof(push_constant_t), VK_SHADER_STAGE_ALL);
  gfx::handle_pipeline_layout_t pl = base->create_pipeline_layout(cpl);

  gfx::config_pipeline_t cp{};
  cp.handle_pipeline_layout                   = pl;
  cp.vk_pipeline_rasterization_state.cullMode = VK_CULL_MODE_NONE;
  cp.add_shader(vs);
  cp.add_shader(fs);
  gfx::handle_pipeline_t voxelizer = base->create_graphics_pipeline(cp);

  gfx::config_pipeline_t clear_cp{};
  clear_cp.handle_pipeline_layout = pl;
  clear_cp.add_shader(cs);
  gfx::handle_pipeline_t clear = base->create_compute_pipeline(clear_cp);

  gfx::config_image_t ci{};
  ci.vk_width   = extent;
  ci.vk_height  = extent;
  ci.vk_depth   = extent;
  ci.vk_type    = VK_IMAGE_TYPE_3D;
  ci.vk_format  = VK_FORMAT_R32_UINT;
  ci.vk_usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ci.vk_mips    = 1;
  ci.vk_tiling  = VK_IMAGE_TILING_OPTIMAL;
  ci.debug_name = "voxel_r";
  auto ir       = base->create_image(ci);
  auto ivr      = base->create_image_view({ir});
  ci.debug_name = "voxel_g";
  auto ig       = base->create_image(ci);
  auto ivg      = base->create_image_view({ig});
  ci.debug_name = "voxel_b";
  auto ib       = base->create_image(ci);
  auto ivb      = base->create_image_view({ib});

  auto b_r = base->new_bindless_storage_image();
  auto b_g = base->new_bindless_storage_image();
  auto b_b = base->new_bindless_storage_image();
  base->set_bindless_storage_image(b_r, ivr);
  base->set_bindless_storage_image(b_g, ivg);
  base->set_bindless_storage_image(b_b, ivb);

  model::raw_model_t raw_model = model::load_model_from_path(path);
  math::aabb_t       model_aabb{};
  for (const auto &raw_mesh : raw_model.meshes) {
    if (raw_mesh.aabb.is_valid()) model_aabb.grow(raw_mesh.aabb);
  }
  math::vec3 center   = model_aabb.center();
  math::vec3 grid_min = center - math::vec3{model_aabb.largest_extent() * 0.5f};
  math::vec3 grid_max = center + math::vec3{model_aabb.largest_extent() * 0.5f};

  uint32_t             count = extent * extent * extent;
  gfx::config_buffer_t cb{};
  cb.vk_size                     = 3 * count * sizeof(uint32_t);
  cb.vk_buffer_usage_flags       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  cb.debug_name                  = "buffer";
  auto b                         = base->create_buffer(cb);

  gfx::handle_sampler_t s;
  {
    gfx::config_sampler_t cs{};
    cs.vk_mag_filter = VK_FILTER_LINEAR;
    cs.vk_min_filter = VK_FILTER_LINEAR;
    cs.vk_max_lod    = 0.f;
    cs.debug_name    = "voxel sampler";
    s                = base->create_sampler(cs);
  }
  auto b_s = base->new_bindless_sampler();
  base->set_bindless_sampler(b_s, s);

  std::vector<mesh_data_t> meshes_data{};
  std::vector<mesh_t>      meshes{};
  for (const auto &raw_mesh : raw_model.meshes) {
    mesh_t &mesh     = meshes.emplace_back();
    mesh.index_count = raw_mesh.indices.size();

    gfx::config_buffer_t cb{};
    cb.vk_buffer_usage_flags       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    cb.vk_size    = raw_mesh.vertices.size() * sizeof(model::vertex_t);
    cb.debug_name = "vertices";
    mesh.vertices = gfx::helper::create_buffer_staged(
        *base, cb, raw_mesh.vertices.data(), cb.vk_size);

    cb.vk_size    = raw_mesh.indices.size() * sizeof(uint32_t);
    cb.debug_name = "indices";
    mesh.indices  = gfx::helper::create_buffer_staged(
        *base, cb, raw_mesh.indices.data(), cb.vk_size);

    mesh.diffuse      = load_diffuse_image(base, raw_mesh);
    mesh.diffuse_view = base->create_image_view({mesh.diffuse});
    auto b_diffuse    = base->new_bindless_image();
    base->set_bindless_image(b_diffuse, mesh.diffuse_view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    mesh_data_t mesh_data{};
    mesh_data.model_mat = math::mat4{1.f};
    mesh_data.vertices  = gfx::to<model::vertex_t *>(
        base->get_buffer_device_address(mesh.vertices));
    mesh_data.indices =
        gfx::to<uint32_t *>(base->get_buffer_device_address(mesh.indices));
    mesh_data.index_count = mesh.index_count;
    mesh_data.diffuse     = b_diffuse;

    cb.vk_size     = sizeof(mesh_data_t);
    cb.debug_name  = "mesh_data";
    mesh.mesh_data = gfx::helper::create_buffer_staged(*base, cb, &mesh_data,
                                                       sizeof(mesh_data_t));
    meshes_data.emplace_back(mesh_data);
  }

  push_constant_t pc{};
  // axis and mesh_data
  pc.grid_min = grid_min;
  pc.grid_max = grid_max;
  pc.r        = b_r.val;
  pc.g        = b_g.val;
  pc.b        = b_b.val;
  pc.s        = b_s.val;
  pc.extent   = extent;

  auto cmd = gfx::helper::begin_single_use_commandbuffer(*base);

  gfx::rendergraph_t rg{};

  rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
      base->cmd_bind_pipeline(cmd, clear);
      base->cmd_bind_descriptor_sets(cmd, clear, 0,
                                     {base->_bindless_descriptor_set});
      base->cmd_push_constants(cmd, clear, VK_SHADER_STAGE_ALL, 0, sizeof(pc),
                               &pc);
      base->cmd_dispatch(cmd, (extent + 7) / 8, (extent + 7) / 8,
                         (extent + 7) / 8);
    })
      .add_write_image(ir, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_IMAGE_LAYOUT_GENERAL)
      .add_write_image(ig, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_IMAGE_LAYOUT_GENERAL)
      .add_write_image(ib, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_IMAGE_LAYOUT_GENERAL);

  rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
      base->cmd_begin_rendering(cmd, {}, {}, {{}, {extent, extent}});
      auto [vk_viewport, vk_scissor] =
          gfx::helper::fill_viewport_and_scissor_structs(extent, extent);
      base->cmd_set_viewport_and_scissor(cmd, vk_viewport, vk_scissor);
      base->cmd_bind_pipeline(cmd, voxelizer);
      base->cmd_bind_descriptor_sets(cmd, voxelizer, 0,
                                     {base->_bindless_descriptor_set});
      for (uint32_t axis = 0; axis < 3; axis++) {
        pc.axis = axis;
        for (const auto &mesh : meshes) {
          pc.mesh_data = gfx::to<mesh_data_t *>(
              base->get_buffer_device_address(mesh.mesh_data));
          base->cmd_push_constants(cmd, voxelizer, VK_SHADER_STAGE_ALL, 0,
                                   sizeof(pc), &pc);
          base->cmd_draw(cmd, mesh.index_count, 1, 0, 0);
        }
      }
      base->cmd_end_rendering(cmd);
    })
      .add_write_image(
          ir, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL)
      .add_write_image(
          ig, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL)
      .add_write_image(
          ib, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);

  rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
      for (uint32_t c = 0; c < 3; c++) {
        VkBufferImageCopy copy{};
        copy.bufferOffset      = VkDeviceSize(c) * count * sizeof(uint32_t);
        copy.bufferRowLength   = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel       = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount     = 1;
        copy.imageOffset                     = {0, 0, 0};
        copy.imageExtent                     = {extent, extent, extent};
        base->cmd_copy_image_to_buffer(cmd,
                                       c == 0   ? ir
                                       : c == 1 ? ig
                                                : ib,
                                       b, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       copy);
      }
    })
      .add_read_image(ir, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      .add_read_image(ig, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      .add_read_image(ib, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      .add_write_buffer(b, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);

  base->render_rendergraph(rg, cmd);

  gfx::helper::end_single_use_command_buffer(*base, cmd);

  const auto *data = reinterpret_cast<const uint32_t *>(base->map_buffer(b));

  std::vector<voxel_t> voxels{};
  for (uint32_t x = 0; x < extent; x++) {
    for (uint32_t y = 0; y < extent; y++) {
      for (uint32_t z = 0; z < extent; z++) {
        const uint32_t i = x + extent * (y + extent * z);
        const uint32_t r = data[i];
        const uint32_t g = data[count + i];
        const uint32_t b = data[2 * count + i];
        if (!(r | g | b)) continue;

        const auto to_float = [](uint32_t u) {
          float f;
          std::memcpy(&f, &u, sizeof(f));
          return f;
        };
        const auto to_u8 = [](float v) {
          return uint32_t(math::clamp(v, 0.f, 1.f) * 255.f);
        };
        const math::vec3 color = math::clamp(
            math::vec3{to_float(r), to_float(g), to_float(b)}, 0.f, 1.f);

        voxels.push_back(
            voxel_t{math::ivec3{int32_t(x), int32_t(y), int32_t(z)},  //
                    0xff000000u | (to_u8(color.x) << 0) |             //
                        (to_u8(color.y) << 8) | (to_u8(color.z) << 16)});
      }
    }
  }

  for (auto &mesh : meshes) {
    base->destroy_image(mesh.diffuse);
    base->destroy_image_view(mesh.diffuse_view);
    base->destroy_buffer(mesh.vertices);
    base->destroy_buffer(mesh.indices);
    base->destroy_buffer(mesh.mesh_data);
  }
  base->destroy_sampler(s);
  base->destroy_buffer(b);
  base->destroy_image(ib);
  base->destroy_image(ig);
  base->destroy_image(ir);
  base->destroy_image_view(ivb);
  base->destroy_image_view(ivg);
  base->destroy_image_view(ivr);
  base->destroy_pipeline(clear);
  base->destroy_pipeline(voxelizer);
  base->destroy_pipeline_layout(pl);
  base->destroy_shader(cs);
  base->destroy_shader(fs);
  base->destroy_shader(vs);

  return voxels;
}

#endif
