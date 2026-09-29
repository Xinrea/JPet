#pragma once

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

/**
 * @brief スプライトを実装するクラス。
 *
 * テクスチャID、Rectの管理。
 *
 */
class ProgressSprite {
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
  ProgressSprite(float x, float y, float width, float height);

  void UpdateProgress(float p) {
    progress_ = p;  
  }
 
  void Show() { target_alpha_ = 100; }
  void Hide() { target_alpha_ = 0; }

  /**
   * @brief デストラクタ
   */
  ~ProgressSprite();

  /**
   * @brief 描画する
   *
   */
  void Render();


 private:
  const static int FULL_SEGMENTS = 360;
  GLuint vbo_;      ///< テクスチャID
  GLuint vao_;             ///< 矩形
  GLuint var_color_;
  int current_alpha_ = 0;
  int target_alpha_ = 0;
  float progress_ = 0;
  GLuint shaderProgram_;
#ifdef __APPLE__
  const char *vertexShaderSource = "#version 120\n"
                                   "attribute vec2 aPos;\n"
#else
  const char *vertexShaderSource = "#version 330 core\n"
                                   "layout (location = 0) in vec2 aPos;\n"
#endif
                                   "void main() {\n"
                                   "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
                                   "}\0";
  const char *fragmentShaderSource =
#ifdef __APPLE__
      "#version 120\n"
#else
      "#version 330 core\n"
#endif
      "uniform vec4 globalColor;"
#ifdef __APPLE__
      "varying vec4 FragColor;\n"
#else
      "out vec4 FragColor;\n"
#endif
      "void main() {\n"
#ifdef __APPLE__
      "   gl_FragColor = globalColor;\n"
#else
      "   FragColor = globalColor;\n"
#endif
      "}\0";
};
