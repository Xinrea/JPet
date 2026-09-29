#pragma once

#include "LAppTextureManager.hpp"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

enum class MenuSelect {
 None = 0,
 UP = 1,
 RIGHT = 2,
 DOWN = 3,
 LEFT = 4
};

/**
 * @brief スプライトを実装するクラス。
 *
 * テクスチャID、Rectの管理。
 *
 */
class MenuSprite {
 public:
  MenuSprite();

  void Show() { enabled_ = true; }
  void Hide() {
    enabled_ = false;
    selected = MenuSelect::None;
  }

  /**
   * @brief デストラクタ
   */
  ~MenuSprite();

  /**
   * @brief 描画する
   *
   */
  void Render();

  void Update(double x, double y);

  MenuSelect GetSelected() {
   return selected;
  }

 private:
  bool enabled_ = false;
  GLuint vao_, vbo_, ebo_;
  GLuint shaderProgram_;
  GLuint scaleLoc, tranLoc;
  GLuint texRotLoc;
  GLuint texLoc;
  GLfloat transitions[4][2] = {{0, 0.4f}, {0.4f, 0}, {0, -0.4f}, {-0.4f, 0}};
  GLfloat scaleMatrix[4] = {1.0f, 0, 0, 1.0f};
  GLfloat dRotateMatrix[4] = {1.0f, 0, 0, 1.0f};
  GLfloat texRotateMatrixs[4][4] = {{1.0f, 0, 0, 1.0f},
                                    {0.0f, -1.0f, 1.0f, 0.0f},
                                    {-1.0f, 0, 0, -1.0f},
                                    {0.0f, 1.0f, -1.0f, 0.0f}};
  MenuSelect selected = MenuSelect::None;
  LAppTextureManager texture_manager_;
  LAppTextureManager::TextureInfo* base_texture_;
  LAppTextureManager::TextureInfo* mask_texture_;
  LAppTextureManager::TextureInfo* icons_texture_[4];
#ifdef __APPLE__
  const char *vertexShaderSource = "#version 120\n"
                                   "attribute vec2 aPos;\n"
                                   "attribute vec2 aTexCoord;\n"
#else
  const char *vertexShaderSource = "#version 330 core\n"
                                   "layout (location = 0) in vec2 aPos;\n"
                                   "layout (location = 1) in vec2 aTexCoord;\n"
#endif
                                   "uniform mat2 uScale;\n"
                                   "uniform vec2 uTransition;\n"
                                   "uniform mat2 uTexRotate;\n"
#ifdef __APPLE__
                                   "varying vec2 TexCoord;\n"
#else
                                   "out vec2 TexCoord;\n"
#endif
                                   "void main() {\n"
                                   "   vec2 new_pos = uScale * aPos + uTransition;"
                                   "   gl_Position = vec4(new_pos, 0.0, 1.0);\n"
                                   "   TexCoord = uTexRotate * aTexCoord;\n"
                                   "}\0";
  const char *fragmentShaderSource =
#ifdef __APPLE__
      "#version 120\n"
      "varying vec2 TexCoord;\n"
#else
      "#version 330 core\n"
      "in vec2 TexCoord;\n"
      "out vec4 FragColor;\n"
#endif
      "uniform sampler2D texture1;\n"
      "void main() {\n"
#ifdef __APPLE__
      "   gl_FragColor = texture2D(texture1, TexCoord);\n"
#else
      "   FragColor = texture(texture1, TexCoord);\n"
#endif
      "}\0";

  LAppTextureManager::TextureInfo* load(std::string filename);
  void renderBg();
  void renderItems();
  void renderMask();

  void updateScale(float s) {
    scaleMatrix[0] = s;
    scaleMatrix[3] = s;
  }

  GLfloat *getRotateMatrix() {
    switch (selected) {
    case MenuSelect::None: {
      return nullptr;
    }
    case MenuSelect::LEFT: {
      return texRotateMatrixs[0];
    }
    case MenuSelect::UP: {
      return texRotateMatrixs[1];
    }
    case MenuSelect::RIGHT: {
      return texRotateMatrixs[2];
    }
    case MenuSelect::DOWN: {
      return texRotateMatrixs[3];
    }
    default: {
     return nullptr;
    }
    }
  }
};
