#ifndef APP_HPP
#define APP_HPP

#include "horizon/core/core.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/context.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/types.hpp"

#include "ThreadPool.hpp"

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
  void draw();

  core::ref<core::window_t> win;
  core::ref<gfx::context_t> ctx;
  core::ref<gfx::base_t>    base;

  gfx::handle_sampler_t               sampler;
  gfx::handle_descriptor_set_layout_t imgui_dsl;
  gfx::handle_descriptor_set_t        imgui_ds;
  gfx::handle_image_t                 image;
  gfx::handle_image_view_t            image_view;
  gfx::handle_managed_buffer_t        buffer;

  ThreadPool thread_pool{2 + std::thread::hardware_concurrency()};

  std::vector<std::future<void>> jobs;
};

#endif
