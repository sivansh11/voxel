#ifndef APP_HPP
#define APP_HPP

#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/context.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/types.hpp"

#include "ThreadPool.hpp"

#include "world.hpp"

struct app_t {
  app_t(int argc, char **argv);
  ~app_t();

  void run();

 private:
  void                          create_image(uint32_t width, uint32_t height);
  std::pair<uint32_t, uint32_t> image_dims();
  void                          destroy_image();

  void create_buffer(uint32_t width, uint32_t height);
  void destroy_buffer();

  void create_pipeline();
  void destroy_pipeline();

  void upload_tree();
  void destroy_tree();

  core::ref<core::window_t> win;
  core::ref<gfx::context_t> ctx;
  core::ref<gfx::base_t>    base;

  gfx::handle_sampler_t                sampler;
  gfx::handle_descriptor_set_layout_t  imgui_dsl;
  gfx::handle_descriptor_set_t         imgui_ds;
  gfx::handle_image_t                  image;
  gfx::handle_image_view_t             image_view;
  gfx::handle_bindless_storage_image_t bindless_image;
  gfx::handle_managed_buffer_t         buffer;

  ThreadPool thread_pool{std::thread::hardware_concurrency()};

  std::vector<std::future<void>> jobs;

  gfx::handle_pipeline_layout_t trace_pl;
  gfx::handle_shader_t          trace_shader;
  gfx::handle_pipeline_t        trace_pipeline;
  gfx::handle_managed_timer_t   trace_timer;

  gfx::handle_managed_buffer_t camera_buffer;
  gfx::handle_buffer_t         nodes_buffer{core::null_handle};
  gfx::handle_buffer_t         indices_buffer{core::null_handle};
  gfx::handle_buffer_t         tree64_buffer{core::null_handle};

  bool tree_dirty = true;

  world_t world;
};

#endif
