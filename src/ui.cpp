#include "ui.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace ui {

void draw_dockspace(context_t &ctx) {
  ImGuiDockNodeFlags dockspaceFlags =
      ImGuiDockNodeFlags_None & ~ImGuiDockNodeFlags_PassthruCentralNode;
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking |              //
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
  ImGui::DockSpace(dockspaceID, ImGui::GetContentRegionAvail(), dockspaceFlags);
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("menu")) {
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void draw_viewport(context_t &ctx) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
  ImGui::SetNextWindowClass(&window_class);
  ImGuiWindowFlags viewPortFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration;
  ImGui::Begin("viewport", nullptr, viewPortFlags);

  auto vp = ImGui::GetWindowSize();
  if (ctx.viewport_width != vp.x || ctx.viewport_height != vp.y) {
    ctx.viewport_width  = vp.x;
    ctx.viewport_height = vp.y;
  }
  ImGui::Image(
      reinterpret_cast<ImTextureID>(reinterpret_cast<void *>(ctx.vk_ds)),
      ImGui::GetContentRegionAvail());
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void draw_debug(context_t &ctx) {
  ImGui::Begin("debug");
  ImGui::Text("%f framerate", ImGui::GetIO().Framerate);
  ImGui::End();
}

}  // namespace ui
