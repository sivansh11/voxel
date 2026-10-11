#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <GLFW/glfw3.h>

#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/window.hpp"
#include "math/math.hpp"

class editor_camera_t : public core::camera_t {
public:
  editor_camera_t(core::window_t &window) : _window(window) {
    inv_view[3] = math::vec4{-1, 0, 0, 0};
    auto [width, height] = _window.dimensions();
    update_projection(float(width) / float(height));
  }

  void update_projection(float aspect_ratio) {
    static float s_aspect_ratio = 0;
    if (s_aspect_ratio != aspect_ratio) {
      projection =
          glm::perspective(glm::radians(fov), aspect_ratio, near, far) *
          math::scale(math::mat4{1.f}, math::vec3{1.f, -1.f, 1.f});
      s_aspect_ratio = aspect_ratio;
    }
  }

  void update(float dt, float width, float height) {
    update_projection(float(width) / float(height));

    double curX, curY;
    glfwGetCursorPos(_window.window(), &curX, &curY);

    float velocity = _mouse_speed * dt * camera_speed_multiplyer;

    glm::vec3 position = core::camera_t::position();

    if (glfwGetKey(_window.window(), GLFW_KEY_W))
      position += _front * velocity;
    if (glfwGetKey(_window.window(), GLFW_KEY_S))
      position -= _front * velocity;
    if (glfwGetKey(_window.window(), GLFW_KEY_D))
      position += _right * velocity;
    if (glfwGetKey(_window.window(), GLFW_KEY_A))
      position -= _right * velocity;
    if (glfwGetKey(_window.window(), GLFW_KEY_SPACE))
      position += _up * velocity;
    if (glfwGetKey(_window.window(), GLFW_KEY_LEFT_SHIFT))
      position -= _up * velocity;

    glm::vec2 mouse{curX, curY};
    glm::vec2 difference = mouse - _initial_mouse;
    _initial_mouse = mouse;

    if (glfwGetMouseButton(_window.window(), GLFW_MOUSE_BUTTON_1)) {
      difference.x = difference.x / float(width);
      difference.y = -(difference.y / float(height));

      _yaw += difference.x * _mouse_sensitivity;
      _pitch += difference.y * _mouse_sensitivity;

      if (_pitch > 89.0f)
        _pitch = 89.0f;
      if (_pitch < -89.0f)
        _pitch = -89.0f;
    }

    glm::vec3 front;
    front.x = glm::cos(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch));
    front.y = glm::sin(glm::radians(_pitch));
    front.z = glm::sin(glm::radians(_yaw)) * glm::cos(glm::radians(_pitch));
    _front = front;
    _right = glm::normalize(glm::cross(_front, glm::vec3{0, 1, 0}));
    _up = glm::normalize(glm::cross(_right, _front));

    view = glm::lookAt(position, position + _front, glm::vec3{0, 1, 0});

    core::camera_t::update();
  }

  float fov{45.0f};
  float camera_speed_multiplyer{1.0f};
  float far{10000.0f};
  float near{0.1f};

private:
  core::window_t &_window;

  glm::vec3 _front{0.0f};
  glm::vec3 _up{0.0f, 1.0f, 0.0f};
  glm::vec3 _right{0.0f};

  glm::vec2 _initial_mouse{};

  float _yaw{0.0f};
  float _pitch{0.0f};
  float _mouse_speed{0.005f};
  float _mouse_sensitivity{100.0f};
};

#endif
