/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

/**
 * @brief スプライトを実装するクラス。
 *
 * テクスチャID、Rectの管理。
 *
 */
class LAppSprite {
 public:
  /**
   * @brief コンストラクタ
   *
   * @param[in]       x            x座標
   * @param[in]       y            y座標
   * @param[in]       width        横幅
   * @param[in]       height       高さ
   * @param[in]       textureId    テクスチャID
   * @param[in]       programId    シェーダID
   */
  LAppSprite(float x, float y, float width, float height);

  void UpdateProgress(float p) {
    progress_ = p;  
  }
 
  void Show() { target_alpha_ = 100; }
  void Hide() { target_alpha_ = 0; }

  /**
   * @brief デストラクタ
   */
  ~LAppSprite();

  /**
   * @brief 描画する
   *
   */
  void Render();


 private:
  const static int FULL_SEGMENTS = 360;
  GLuint vbo_;      ///< テクスチャID
  GLuint vao_;             ///< 矩形
  GLuint var_alpha_;
  int current_alpha_ = 0;
  int target_alpha_ = 0;
  float progress_ = 0;
  GLuint shaderProgram_;
  const char *vertexShaderSource = "#version 330 core\n"
                                   "layout (location = 0) in vec2 aPos;\n"
                                   "void main() {\n"
                                   "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
                                   "}\0";
  const char *fragmentShaderSource =
      "#version 330 core\n"
      "uniform float globalAlpha;"
      "out vec4 FragColor;\n"
      "void main() {\n"
      "   FragColor = vec4(0.47, 0.79, 0.18, globalAlpha);\n"
      "}\0";
};
