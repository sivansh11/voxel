#include "app.hpp"

#include <stdexcept>

#include "imgui.h"

#include <GLFW/glfw3.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include "horizon/core/components.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/core.hpp"
#include "horizon/gfx/helper.hpp"

#include "voxelizer.hpp"
#include "tree64.hpp"

app_t::app_t(int argc, char **argv) {
  if (argc != 3) throw std::runtime_error("[voxel] [path] [extent]");

  win  = core::make_ref<core::window_t>("voxel", 1200, 800);
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

  auto voxels = voxelize(base, argv[1], std::stoi(argv[2]));
  world       = world_t{voxels};
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

struct ui_context_t {
  VkDescriptorSet vk_ds;
  uint32_t        viewport_width;
  uint32_t        viewport_height;
  float           scale = 0.125f;
};

struct camera_controller_t : core::camera_t {
  explicit camera_controller_t(core::window_t &window) : _window(window) {
    _front = {glm::cos(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch)),
              glm::sin(glm::radians(_pitch)),
              glm::sin(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch))};
    update_view({256.f, 256.f, 256.f});
  }

  bool captured() const { return _captured; }

  void capture() {
    if (_captured) return;
    _captured      = true;
    _just_captured = true;
    glfwSetInputMode(_window.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }

  void release() {
    if (!_captured) return;
    _captured = false;
    glfwSetInputMode(_window.window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }

  void update_view(const math::vec3 &position) {
    _position = position;
    view      = glm::lookAt(_position, _position + _front, glm::vec3{0, 1, 0});
    core::camera_t::update();
  }

  void update_projection(float aspect_ratio) {
    static float s_aspect_ratio = 0;
    if (s_aspect_ratio != aspect_ratio) {
      projection =
          glm::perspective(glm::radians(_fov), aspect_ratio, _near, _far) *
          math::scale(math::mat4{1.f}, math::vec3{1.f, -1.f, 1.f});
      s_aspect_ratio = aspect_ratio;
    }
  }

  void update(float dt, float width, float height) {
    update_projection(width / height);

    if (_captured) {
      ImGuiIO &io = ImGui::GetIO();

      if (_just_captured) {
        _just_captured = false;
      } else {
        const glm::vec2 diff{io.MouseDelta.x / width,
                             -io.MouseDelta.y / height};
        _yaw += diff.x * _mouse_sensitivity;
        _pitch += diff.y * _mouse_sensitivity;
        _pitch = glm::clamp(_pitch, -89.0f, 89.0f);
      }

      const float velocity = _mouse_speed * dt * _speed_multiplyer;
      glm::vec3   position = _position;

      if (ImGui::IsKeyDown(ImGuiKey_W)) position += _front * velocity;
      if (ImGui::IsKeyDown(ImGuiKey_S)) position -= _front * velocity;
      if (ImGui::IsKeyDown(ImGuiKey_D)) position += _right * velocity;
      if (ImGui::IsKeyDown(ImGuiKey_A)) position -= _right * velocity;
      if (ImGui::IsKeyDown(ImGuiKey_Space)) position += _up * velocity;
      if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) position -= _up * velocity;

      _position = position;
    }

    _front.x = glm::cos(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch));
    _front.y = glm::sin(glm::radians(_pitch));
    _front.z = glm::sin(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch));
    _right   = glm::normalize(glm::cross(_front, glm::vec3{0, 1, 0}));
    _up      = glm::normalize(glm::cross(_right, _front));

    view = glm::lookAt(_position, _position + _front, glm::vec3{0, 1, 0});
    core::camera_t::update();
  }

  float _fov{90.0f};
  float _speed_multiplyer{10.0f};
  float _far{10000.0f};
  float _near{0.1f};

 private:
  core::window_t &_window;

  bool _captured{false};
  bool _just_captured{false};

  glm::vec3 _position{256.f, 256.f, 256.f};
  glm::vec3 _front{0.0f};
  glm::vec3 _up{0.0f, 1.0f, 0.0f};
  glm::vec3 _right{0.0f};

  float _yaw{90.0f};
  float _pitch{0.0f};
  float _mouse_speed{0.005f};
  float _mouse_sensitivity{30.0f};
};

void app_t::run() {
  ui_context_t ui_ctx{ctx->get_descriptor_set(imgui_ds).vk_descriptor_set,
                      1,   //
                      1};  //
  core::frame_timer_t frame_timer{60.f};

  camera_controller_t camera_controller{*win};
  {
    core::timer::scope_timer_t timer{[&](auto duration) {
      horizon_info("tree64 in {}ms and used {}MB ({})", duration.count(),
                   world.tree.memory_used() / (1024 * 1024),
                   world.tree.memory_used());
    }};
    // generate(tree64);
  }

  while (!win->should_close()) {
    core::clear_frame_function_times();
    win->poll_events();
    if (win->get_key_pressed(core::key_t::e_escape)) break;

    core::timer::duration_t dt = frame_timer.update();

    base->begin();

    static float draw_duration = 0;
    {
      core::timer::scope_timer_t _{
          [](auto duration) { draw_duration = duration.count(); }};
      auto *pixels = reinterpret_cast<uint32_t *>(base->map_buffer(buffer));
      auto [width, height] = image_dims();
      float inv_width      = 1.f / width;
      float inv_height     = 1.f / height;

      jobs.clear();
      jobs.reserve(height);

      for (uint32_t y = 0; y < height; ++y) {
        jobs.emplace_back(thread_pool.enqueue([&, y] {
          const uint32_t row = y * width;
          for (uint32_t x = 0; x < width; ++x) {
            const math::vec2 uv = {(float(x) + 0.5f) * inv_width,
                                   (float(y) + 0.5f) * inv_height};
            const ray_t      ray =
                ray_t::create(uv, camera_controller.inv_projection,
                              camera_controller.inv_view);
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

        // dockspace
        ImGuiDockNodeFlags dockspaceFlags =
            ImGuiDockNodeFlags_None & ~ImGuiDockNodeFlags_PassthruCentralNode;
        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDocking |              //
            ImGuiWindowFlags_NoTitleBar |             //
            ImGuiWindowFlags_NoCollapse |             //
            ImGuiWindowFlags_NoResize |               //
            ImGuiWindowFlags_NoMove |                 //
            ImGuiWindowFlags_NoBringToFrontOnFocus |  //
            ImGuiWindowFlags_NoNavFocus |             //
            ImGuiWindowFlags_NoBackground |           //
            ImGuiWindowFlags_NoDecoration;

        bool dockSpace = true;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        auto mainViewPort = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewPort->WorkPos);
        ImGui::SetNextWindowSize(mainViewPort->WorkSize);
        ImGui::SetNextWindowViewport(mainViewPort->ID);

        ImGui::Begin("DockSpace", &dockSpace, windowFlags);
        ImGuiID dockspaceID = ImGui::GetID("DockSpace");
        ImGui::DockSpace(dockspaceID, ImGui::GetContentRegionAvail(),
                         dockspaceFlags);
        if (ImGui::BeginMainMenuBar()) {
          if (ImGui::BeginMenu("menu")) {
            ImGui::EndMenu();
          }
          ImGui::EndMainMenuBar();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // viewport
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
        if (!camera_controller.captured() && ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          camera_controller.capture();
        }

        if (camera_controller.captured()) {
          float wheel = ImGui::GetIO().MouseWheel;
          if (wheel) camera_controller._speed_multiplyer += wheel;
        }
        camera_controller.update(dt.count(), ui_ctx.viewport_width,
                                 ui_ctx.viewport_height);

        if (camera_controller.captured()) {
          if (ImGui::IsKeyPressed(ImGuiKey_Q)) camera_controller.release();
        }

        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          ImVec2     mouse_pos = ImGui::GetMousePos();
          ImVec2     item_pos  = ImGui::GetItemRectMin();
          ImVec2     item_size = ImGui::GetItemRectSize();
          math::vec2 uv        = {(mouse_pos.x - item_pos.x) / item_size.x,
                                  (mouse_pos.y - item_pos.y) / item_size.y};
          uv = math::clamp(uv, math::vec2(0.0f), math::vec2(1.0f));
          horizon_info("{}", math::to_string(uv));

          const ray_t ray = ray_t::create(uv, camera_controller.inv_projection,
                                          camera_controller.inv_view);
          hit_t       hit = trace(ray, world.tree);
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

          const ray_t ray = ray_t::create(uv, camera_controller.inv_projection,
                                          camera_controller.inv_view);
          hit_t       hit = trace(ray, world.tree);
          if (hit.did_hit) {
            world.tree.set(hit.position + hit.normal, 0xff0000ffu);
          }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);

        // debug
        ImGui::Begin("debug");
        ImGui::Text("draw took %fms", draw_duration);
        ImGui::Text("%f framerate", ImGui::GetIO().Framerate);
        ImGui::Text("current dims: %u, %u", ui_ctx.viewport_width,
                    ui_ctx.viewport_height);
        ImGui::SliderFloat("scale", &ui_ctx.scale, 0.01f, 1.f);
        ImGui::SliderFloat("camera speed", &camera_controller._speed_multiplyer,
                           1.f, 1000.f);
        ImGui::End();

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
