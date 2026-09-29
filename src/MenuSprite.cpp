#include "MenuSprite.hpp"
#include "DataManager.hpp"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include "GL/gl.h"
#endif
#include "LAppDefine.hpp"
#include "LAppTextureManager.hpp"
#include "LAppPal.hpp"
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>

MenuSprite::MenuSprite() {
  auto shader_checker = [](GLuint shader) {
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        LAppPal::PrintLog(LogLevel::Error, "[MenuSprite]Shader error: %s", infoLog);
    }
  };
  auto link_checker = [](GLuint program) {
    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        LAppPal::PrintLog(LogLevel::Error, "[MenuSprite]Link error: %s", infoLog);
    }
  };
 
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);
  shader_checker(vertexShader);

  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);
  shader_checker(fragmentShader);

  shaderProgram_ = glCreateProgram();
  glAttachShader(shaderProgram_, vertexShader);
  glAttachShader(shaderProgram_, fragmentShader);
#ifdef __APPLE__
  glBindAttribLocation(shaderProgram_, 0, "aPos");
  glBindAttribLocation(shaderProgram_, 1, "aTexCoord");
#endif
  glLinkProgram(shaderProgram_);
  link_checker(shaderProgram_);

  
  texLoc = glGetUniformLocation(shaderProgram_, "texture1");
  texRotLoc = glGetUniformLocation(shaderProgram_, "uTexRotate");

  scaleLoc = glGetUniformLocation(shaderProgram_, "uScale");
  tranLoc = glGetUniformLocation(shaderProgram_, "uTransition");

#ifdef __APPLE__
  float vertices[] = {
      0.75f, 0.75f, 1.0f, 0.0f, 0.75f, -0.75f, 1.0f, 1.0f,
      -0.75f, 0.75f, 0.0f, 0.0f, 0.75f, -0.75f, 1.0f, 1.0f,
      -0.75f, -0.75f, 0.0f, 1.0f, -0.75f, 0.75f, 0.0f, 0.0f};
#else
  float vertices[] = {0.75f,  0.75f,  1.0f, 0.0f,
                      0.75f,  -0.75f, 1.0f, 1.0f,
                      -0.75f, -0.75f, 0.0f, 1.0f,
                      -0.75f, 0.75f,  0.0f, 0.0f};
#endif

  unsigned int indices[] = {
    0, 1, 3,
    1, 2, 3
  };

#ifndef __APPLE__
  GLint previousVAO;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);
  glGenVertexArrays(1, &vao_);
#endif
  glGenBuffers(1, &vbo_);
#ifndef __APPLE__
  glGenBuffers(1, &ebo_);
#endif

  // setup vao
#ifndef __APPLE__
  glBindVertexArray(vao_);
#endif

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
#ifndef __APPLE__
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
#endif

  // 位置属性
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // 纹理坐标属性
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // setup textures
  base_texture_ =
      load("/circle-menu/base.png");
  mask_texture_ =
      load("/circle-menu/mask.png");
  // 0: app, 1: folder, 2: web, 3: setting
  icons_texture_[0] =
      load("/circle-menu/type-app.png");
  icons_texture_[1] = load("/circle-menu/type-folder.png");
  icons_texture_[2] = load("/circle-menu/type-web.png");
  icons_texture_[3] = load("/circle-menu/type-setting.png");
  
  glActiveTexture(GL_TEXTURE10);
  glBindTexture(GL_TEXTURE_2D, base_texture_->id);

  glActiveTexture(GL_TEXTURE11);
  glBindTexture(GL_TEXTURE_2D, mask_texture_->id);

  for (size_t i = 0; i < 4; i++) {
    glActiveTexture(GL_TEXTURE12 + i);
    glBindTexture(GL_TEXTURE_2D, icons_texture_[i]->id);
  }
  
#ifndef __APPLE__
  glBindVertexArray(previousVAO);
#endif

  LAppPal::PrintLog(LogLevel::Debug, "[MenuSprite]initialized");
}

MenuSprite::~MenuSprite() {
  texture_manager_.ReleaseTextures();
#ifndef __APPLE__
  glDeleteVertexArrays(1, &vao_);
  glDeleteBuffers(1, &vbo_);
  glDeleteBuffers(1, &ebo_);
#else
  glDeleteBuffers(1, &vbo_);
#endif
}

LAppTextureManager::TextureInfo* MenuSprite::load(std::string filename) {
  return texture_manager_.CreateTextureFromPngFile(LAppDefine::ResourcesPath + filename);
}

void MenuSprite::Update(double x, double y) {
  double dis = sqrtf(x*x + y*y);
  if (dis < 0.16) {
    selected = MenuSelect::None;
    return;
  }
  if (y > 0 && y > x && y > -x) {
    // up
    selected = MenuSelect::UP;
    return;
  }
  if (x > 0 && x > y && x > -y) {
    //right
    selected = MenuSelect::RIGHT;
    return;
  }
  if (x < 0 && -x > y && -x > -y) {
    // left
    selected = MenuSelect::LEFT;
    return;
  }
  if (y < 0 && -y > x && -y > -x) {
    // down
    selected = MenuSelect::DOWN;
    return;
  }
  selected = MenuSelect::None;
}

void MenuSprite::Render() {
  if (!enabled_) {
    return;
  }

  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(shaderProgram_);

#ifdef __APPLE__
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
#else
  glBindVertexArray(vao_);
#endif
  
  renderBg();
  
  // item selected
  if (selected != MenuSelect::None) {
    renderMask();
  }
  
  renderItems();
}

void MenuSprite::renderBg() {
  updateScale(1.0f);
  glUniform1i(texLoc, 10);
  glUniform2f(tranLoc, 0, 0);
  glUniformMatrix2fv(scaleLoc, 1, GL_FALSE, scaleMatrix);
  glUniformMatrix2fv(texRotLoc, 1, GL_FALSE, dRotateMatrix);
#ifdef __APPLE__
  glDrawArrays(GL_TRIANGLES, 0, 6);
#else
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
#endif
}

void MenuSprite::renderItems() {
  auto dm = DataManager::GetInstance();
  updateScale(0.15f);
  glUniformMatrix2fv(scaleLoc, 1, GL_FALSE, scaleMatrix);
  glUniformMatrix2fv(texRotLoc, 1, GL_FALSE, dRotateMatrix);
  for (size_t i = 0; i < 4; i++) {
    int icon_t = dm->GetWithDefault("shortcut." + std::to_string(i)+".type", 3);
    // 4 is not enabled
    if (icon_t == 4) {
      continue;
    }
    glUniform2fv(tranLoc, 1, transitions[i]);
    glUniform1i(texLoc, 12 + icon_t);
#ifdef __APPLE__
    glDrawArrays(GL_TRIANGLES, 0, 6);
#else
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
#endif
  }
  
}

void MenuSprite::renderMask() {
  updateScale(1.0f);
  glUniform1i(texLoc, 11);
  glUniform2f(tranLoc, 0, 0);
  glUniformMatrix2fv(scaleLoc, 1, GL_FALSE, scaleMatrix);
  glUniformMatrix2fv(texRotLoc, 1, GL_FALSE, getRotateMatrix());
#ifdef __APPLE__
  glDrawArrays(GL_TRIANGLES, 0, 6);
#else
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
#endif
}
