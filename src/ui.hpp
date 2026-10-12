#ifndef UI_HPP
#define UI_HPP

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

namespace ui {

struct context_t {
  VkDescriptorSet vk_ds;
  uint32_t        viewport_width;
  uint32_t        viewport_height;
  float           scale        = 0.125f;
};

void draw_dockspace(context_t &ctx);
void draw_viewport(context_t &ctx);
void draw_debug(context_t &ctx);

}  // namespace ui

#endif
