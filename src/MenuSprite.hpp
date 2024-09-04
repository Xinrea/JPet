#pragma once

#include "LAppTextureManager.hpp"
#include <GL/glew.h>
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
  const char *vertexShaderSource = "#version 330 core\n"
                                   "layout (location = 0) in vec2 aPos;\n"
                                   "layout (location = 1) in vec2 aTexCoord;\n"
                                   "uniform mat2 uScale;\n"
                                   "uniform vec2 uTransition;\n"
                                   "uniform mat2 uTexRotate;\n"
                                   "out vec2 TexCoord;\n"
                                   "void main() {\n"
                                   "   vec2 new_pos = uScale * aPos + uTransition;"
                                   "   gl_Position = vec4(new_pos, 0.0, 1.0);\n"
                                   "   TexCoord = uTexRotate * aTexCoord;\n"
                                   "}\0";
  const char *fragmentShaderSource =
      "#version 330 core\n"
      "in vec2 TexCoord;\n"
      "out vec4 FragColor;\n"
      "uniform sampler2D texture1;\n"
      "void main() {\n"
      "   FragColor = texture(texture1, TexCoord);\n"
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
