#include "app.hpp"

#include <atomic>
#include "horizon/core/logger.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include "horizon/core/core.hpp"
#include "horizon/gfx/helper.hpp"

#include "ui.hpp"
#include "camera.hpp"
#include "tree64.hpp"
#include "camera.hpp"

app_t::app_t(int argc, char **argv) {
  win  = core::make_ref<core::window_t>("peace", 1200, 800);
  ctx  = core::make_ref<gfx::context_t>(true);
  base = core::make_ref<gfx::base_t>(win, ctx);

  gfx::helper::imgui_init(
      *base, gfx::helper::image_format(*base, base->current_swapchain_image()));

  sampler = base->create_sampler({});

  gfx::config_descriptor_set_layout_t imgui_cdsl{};
  imgui_cdsl.add_layout_binding(0,                                          //
                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  //
                                VK_SHADER_STAGE_FRAGMENT_BIT);
  imgui_cdsl.use_bindless = false;
  imgui_dsl               = base->create_descriptor_set_layout(imgui_cdsl);
  imgui_ds                = base->allocate_descriptor_set(
      {.handle_descriptor_set_layout = imgui_dsl});

  create_image(1, 1);
}

app_t::~app_t() {
  base->wait_idle();
  gfx::helper::imgui_shutdown();
}

inline math::vec3 sky_color(const math::vec3 &direction) {
  const float      t = math::clamp(direction.y * 0.5f + 0.5f, 0.f, 1.f);
  const math::vec3 sky_top{0.25f, 0.45f, 0.85f};
  const math::vec3 sky_bottom{0.65f, 0.7f, 0.78f};
  return math::mix(sky_bottom, sky_top, t);
}

inline uint32_t to_pixel(const math::vec3 &color) {
  const auto to_u8 = [](float v) {
    return uint32_t(math::clamp(v, 0.f, 1.f) * 255.f);
  };
  return 0xff000000u | (to_u8(color.x) << 0) |  //
         (to_u8(color.y) << 8) |                //
         (to_u8(color.z) << 16);
}

void app_t::draw(const core::camera_t &camera) {
  auto *pixels         = reinterpret_cast<uint32_t *>(base->map_buffer(buffer));
  auto [width, height] = image_dims();
  float inv_width      = 1.f / width;
  float inv_height     = 1.f / height;

  jobs.clear();
  jobs.reserve(height);

  for (uint32_t y = 0; y < height; ++y) {
    jobs.emplace_back(thread_pool.enqueue([&, y] {
      const uint32_t row = y * width;
      for (uint32_t x = 0; x < width; ++x) {
        const math::vec2 uv = {float(x) * inv_width, float(y) * inv_height};
        const ray_t      ray =
            ray_t::create(uv, camera.inv_projection, camera.inv_view);
        hit_t hit = trace(ray, world.tree);
        if (hit.did_hit) {
          pixels[row + x] = hit.value;
        } else {
          pixels[row + x] = to_pixel(sky_color(ray.direction));
        }
      }
    }));
  }

  for (auto &f : jobs) f.get();
}

void app_t::run() {
  ui::context_t ui_ctx{ctx->get_descriptor_set(imgui_ds).vk_descriptor_set,
                       1,   //
                       1};  //
  core::frame_timer_t frame_timer{60.f};

  editor_camera_t camera{*win};
  {
    core::timer::scope_timer_t timer{[&](auto duration) {
      horizon_info("generated tree64 in {}ms and used {}MB", duration.count(),
                   world.tree.memory_used() / (1024 * 1024));
    }};
    // generate(tree64);
  }

  while (!win->should_close()) {
    core::clear_frame_function_times();
    win->poll_events();
    if (win->get_key_pressed(core::key_t::e_q) ||
        win->get_key_pressed(core::key_t::e_escape))
      break;

    core::timer::duration_t dt = frame_timer.update();
    camera.update(dt.count(), ui_ctx.viewport_width, ui_ctx.viewport_height);

    base->begin();

    draw(camera);

    auto [width, height] = image_dims();

    gfx::rendergraph_t rg{};
    rg.add_pass([&](gfx::handle_commandbuffer_t) {})
        .add_write_buffer(base->buffer(buffer), VK_ACCESS_HOST_WRITE_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT);
    rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
        VkBufferImageCopy copy{};
        copy.bufferOffset                    = 0;
        copy.bufferRowLength                 = 0;
        copy.bufferImageHeight               = 0;
        copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel       = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount     = 1;
        copy.imageOffset                     = {0, 0, 0};
        copy.imageExtent                     = {width, height, 1};
        base->cmd_copy_buffer_to_image(cmd, base->buffer(buffer), image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       copy);
      })
        .add_read_buffer(base->buffer(buffer), VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT)
        .add_write_image(image, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
        base->begin_swapchain_renderpass();
        gfx::helper::imgui_newframe();

        ui::draw_dockspace(ui_ctx);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGuiWindowClass window_class;
        window_class.DockNodeFlagsOverrideSet =
            ImGuiDockNodeFlags_AutoHideTabBar;
        ImGui::SetNextWindowClass(&window_class);
        ImGuiWindowFlags viewPortFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration;
        ImGui::Begin("viewport", nullptr, viewPortFlags);

        auto vp = ImGui::GetWindowSize();
        if (ui_ctx.viewport_width != vp.x * ui_ctx.scale ||
            ui_ctx.viewport_height != vp.y * ui_ctx.scale) {
          ui_ctx.viewport_width  = vp.x * ui_ctx.scale;
          ui_ctx.viewport_height = vp.y * ui_ctx.scale;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(
                         reinterpret_cast<void *>(ui_ctx.vk_ds)),
                     ImGui::GetContentRegionAvail());

        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          ImVec2     mouse_pos = ImGui::GetMousePos();
          ImVec2     item_pos  = ImGui::GetItemRectMin();
          ImVec2     item_size = ImGui::GetItemRectSize();
          math::vec2 uv        = {(mouse_pos.x - item_pos.x) / item_size.x,
                                  (mouse_pos.y - item_pos.y) / item_size.y};
          uv = math::clamp(uv, math::vec2(0.0f), math::vec2(1.0f));
          horizon_info("{}", math::to_string(uv));

          const ray_t ray =
              ray_t::create(uv, camera.inv_projection, camera.inv_view);
          hit_t hit = trace(ray, world.tree);
          if (hit.did_hit) {
            world.tree.remove(hit.position);
          }
        }

        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_T)) {
          ImVec2     mouse_pos = ImGui::GetMousePos();
          ImVec2     item_pos  = ImGui::GetItemRectMin();
          ImVec2     item_size = ImGui::GetItemRectSize();
          math::vec2 uv        = {(mouse_pos.x - item_pos.x) / item_size.x,
                                  (mouse_pos.y - item_pos.y) / item_size.y};
          uv = math::clamp(uv, math::vec2(0.0f), math::vec2(1.0f));
          horizon_info("{}", math::to_string(uv));

          const ray_t ray =
              ray_t::create(uv, camera.inv_projection, camera.inv_view);
          hit_t hit = trace(ray, world.tree);
          if (hit.did_hit) {
            world.tree.set(hit.position + hit.normal, 0xff0000ffu);
          }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        ui::draw_debug(ui_ctx);

        gfx::helper::imgui_endframe(*base, cmd);
        base->end_swapchain_renderpass();
      })
        .add_read_image(image, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .add_write_image(base->current_swapchain_image(),
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // empty pass to present
    rg.add_pass([&](gfx::handle_commandbuffer_t) {})
        .add_write_image(base->current_swapchain_image(), 0,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    auto cmd = base->current_commandbuffer();
    base->render_rendergraph(rg, cmd);
    base->end();

    if (ui_ctx.viewport_width != width || ui_ctx.viewport_height != height) {
      base->wait_idle();
      destroy_image();
      create_image(std::max(1u, ui_ctx.viewport_width),
                   std::max(1u, ui_ctx.viewport_height));
    }
  }
}

void app_t::create_image(uint32_t width, uint32_t height) {
  gfx::config_image_t ci{};
  ci.vk_width   = width;
  ci.vk_height  = height;
  ci.vk_depth   = 1;
  ci.vk_mips    = 1;
  ci.vk_type    = VK_IMAGE_TYPE_2D;
  ci.vk_format  = VK_FORMAT_R8G8B8A8_UNORM;
  ci.vk_usage   = VK_IMAGE_USAGE_SAMPLED_BIT |  //
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ci.vk_tiling  = VK_IMAGE_TILING_OPTIMAL;
  ci.debug_name = "image";
  image         = base->create_image(ci);
  image_view    = base->create_image_view(
      {.handle_image = image, .debug_name = "image_view"});

  create_buffer(width, height);

  base->update_descriptor_set(imgui_ds)
      .push_image_write(
          0, {.handle_sampler    = sampler,
              .handle_image_view = image_view,
              .vk_image_layout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
      .commit();
}

void app_t::create_buffer(uint32_t width, uint32_t height) {
  gfx::config_buffer_t cb{};
  cb.vk_size                     = VkDeviceSize(width) * height * 4;
  cb.vk_buffer_usage_flags       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  cb.debug_name                  = "pixels";
  buffer =
      base->create_buffer(gfx::resource_update_policy_t::e_every_frame, cb);
}

void app_t::destroy_buffer() { base->destroy_buffer(buffer); }

std::pair<uint32_t, uint32_t> app_t::image_dims() {
  const auto &cfg = ctx->get_image(image).config;
  return {cfg.vk_width, cfg.vk_height};
}

void app_t::destroy_image() {
  base->destroy_image_view(image_view);
  base->destroy_image(image);
  destroy_buffer();
}
