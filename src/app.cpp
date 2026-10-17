#include "app.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "horizon/gfx/base.hpp"
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

  bindless_image = base->new_bindless_storage_image();

  create_image(1, 1);
  create_pipeline();

  {
    gfx::config_buffer_t cb{};
    cb.vk_size               = sizeof(core::camera_t);
    cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cb.vma_allocation_create_flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    cb.debug_name = "camera";
    camera_buffer =
        base->create_buffer(gfx::resource_update_policy_t::e_every_frame, cb);
  }

  auto voxels = voxelize(base, argv[1], std::stoi(argv[2]));
  world       = world_t{voxels};

  upload_tree();
}

app_t::~app_t() {
  base->wait_idle();
  destroy_pipeline();
  destroy_tree();
  base->destroy_buffer(camera_buffer);
  gfx::helper::imgui_shutdown();
}

struct tree64_gpu_t {
  VkDeviceAddress nodes;
  VkDeviceAddress indices;

  uint32_t num_levels;
  uint32_t extent;
};

struct trace_push_constant_t {
  VkDeviceAddress camera;
  VkDeviceAddress tree64;

  uint32_t width;
  uint32_t height;
  uint32_t bindless_image;
};
static_assert(sizeof(trace_push_constant_t) <= 128,
              "sizeof(trace_push_constant_t) not <= 128");

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

    if (tree_dirty) {
      base->wait_idle();
      upload_tree();
    }

    base->begin();

    std::memcpy(base->map_buffer(camera_buffer),  //
                static_cast<core::camera_t *>(&camera_controller),
                sizeof(core::camera_t));

    auto [width, height] = image_dims();

    gfx::rendergraph_t rg{};
    rg.add_pass([&](gfx::handle_commandbuffer_t) {})
        .add_write_buffer(base->buffer(camera_buffer), VK_ACCESS_HOST_WRITE_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT)
        .add_write_buffer(nodes_buffer, VK_ACCESS_HOST_WRITE_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT)
        .add_write_buffer(indices_buffer, VK_ACCESS_HOST_WRITE_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT)
        .add_write_buffer(tree64_buffer, VK_ACCESS_HOST_WRITE_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT);
    rg.add_pass([&](gfx::handle_commandbuffer_t cmd) {
        base->cmd_begin_timer(cmd, base->timer(trace_timer));
        base->cmd_bind_pipeline(cmd, trace_pipeline);
        base->cmd_bind_descriptor_sets(cmd, trace_pipeline, 0,
                                       {base->_bindless_descriptor_set});

        trace_push_constant_t pc{};
        pc.camera         = base->get_buffer_device_address(camera_buffer);
        pc.tree64         = base->get_buffer_device_address(tree64_buffer);
        pc.width          = width;
        pc.height         = height;
        pc.bindless_image = bindless_image.val;

        base->cmd_push_constants(cmd, trace_pipeline, VK_SHADER_STAGE_ALL, 0,
                                 sizeof(pc), &pc);
        base->cmd_dispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        base->cmd_end_timer(cmd, base->timer(trace_timer));
      })
        .add_read_buffer(base->buffer(camera_buffer), VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .add_read_buffer(nodes_buffer, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .add_read_buffer(indices_buffer, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .add_read_buffer(tree64_buffer, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .add_write_image(image, VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_IMAGE_LAYOUT_GENERAL);
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
            tree_dirty = true;
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
            tree_dirty = true;
          }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);

        // debug
        ImGui::Begin("debug");
        if (auto t = base->_context->timer_get_time(base->timer(trace_timer))) {
          ImGui::Text("draw took %fms", *t);
        } else {
          ImGui::Text("unable to get draw timings");
        }
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
                  VK_IMAGE_USAGE_STORAGE_BIT;
  ci.vk_tiling  = VK_IMAGE_TILING_OPTIMAL;
  ci.debug_name = "image";
  image         = base->create_image(ci);
  image_view    = base->create_image_view(
      {.handle_image = image, .debug_name = "image_view"});

  base->set_bindless_storage_image(bindless_image, image_view);

  base->update_descriptor_set(imgui_ds)
      .push_image_write(
          0, {.handle_sampler    = sampler,
              .handle_image_view = image_view,
              .vk_image_layout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
      .commit();
}

std::pair<uint32_t, uint32_t> app_t::image_dims() {
  const auto &cfg = ctx->get_image(image).config;
  return {cfg.vk_width, cfg.vk_height};
}

void app_t::destroy_image() {
  base->destroy_image_view(image_view);
  base->destroy_image(image);
}

void app_t::create_pipeline() {
  trace_shader = gfx::helper::create_slang_shader(
      *base, "./src/shaders/trace.slang", gfx::shader_type_t::e_compute);

  gfx::config_pipeline_layout_t cpl{};
  cpl.add_descriptor_set_layout(base->_bindless_descriptor_set_layout);
  cpl.add_push_constant(sizeof(trace_push_constant_t), VK_SHADER_STAGE_ALL);
  trace_pl = base->create_pipeline_layout(cpl);

  gfx::config_pipeline_t cp{};
  cp.handle_pipeline_layout = trace_pl;
  cp.add_shader(trace_shader);
  trace_pipeline = base->create_compute_pipeline(cp);
  trace_timer =
      base->create_timer(gfx::resource_update_policy_t::e_every_frame, {});
}

void app_t::destroy_pipeline() {
  base->destroy_pipeline(trace_pipeline);
  base->destroy_pipeline_layout(trace_pl);
  base->destroy_shader(trace_shader);
}

void app_t::upload_tree() {
  destroy_tree();

  gfx::config_buffer_t cb{};
  cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  cb.vma_allocation_create_flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  cb.debug_name = "nodes";
  cb.vk_size    = world.tree.nodes.size() * sizeof(world.tree.nodes[0]);
  nodes_buffer  = base->create_buffer(cb);
  std::memcpy(base->map_buffer(nodes_buffer), world.tree.nodes.data(),
              cb.vk_size);

  cb.debug_name  = "indices";
  cb.vk_size     = world.tree.indices.size() * sizeof(world.tree.indices[0]);
  indices_buffer = base->create_buffer(cb);
  std::memcpy(base->map_buffer(indices_buffer), world.tree.indices.data(),
              cb.vk_size);

  tree64_gpu_t tree64_gpu{};
  tree64_gpu.nodes      = base->get_buffer_device_address(nodes_buffer);
  tree64_gpu.indices    = base->get_buffer_device_address(indices_buffer);
  tree64_gpu.num_levels = tree64_t::num_levels;
  tree64_gpu.extent     = tree64_t::extent;

  cb.debug_name = "tree64";
  cb.vk_size    = sizeof(tree64_gpu_t);
  tree64_buffer = base->create_buffer(cb);
  std::memcpy(base->map_buffer(tree64_buffer), &tree64_gpu,
              sizeof(tree64_gpu_t));

  tree_dirty = false;
}

void app_t::destroy_tree() {
  if (nodes_buffer != core::null_handle) {
    base->destroy_buffer(nodes_buffer);
    nodes_buffer = core::null_handle;
  }
  if (indices_buffer != core::null_handle) {
    base->destroy_buffer(indices_buffer);
    indices_buffer = core::null_handle;
  }
  if (tree64_buffer != core::null_handle) {
    base->destroy_buffer(tree64_buffer);
    tree64_buffer = core::null_handle;
  }
}
