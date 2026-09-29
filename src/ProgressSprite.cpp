#include "ProgressSprite.hpp"
#define _USE_MATH_DEFINES
#include <math.h>

ProgressSprite::ProgressSprite(float x, float y, float inner, float outer) {
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);
  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);
  shaderProgram_ = glCreateProgram();
  glAttachShader(shaderProgram_, vertexShader);
  glAttachShader(shaderProgram_, fragmentShader);
#ifdef __APPLE__
  glBindAttribLocation(shaderProgram_, 0, "aPos");
#endif
  glLinkProgram(shaderProgram_);

  float vertices[(FULL_SEGMENTS + 1) * 4];
  int index = 0;
  for (int i = 0; i <= FULL_SEGMENTS; i++) {
    float angle = 2.0f * M_PI * i / FULL_SEGMENTS + M_PI / 2;
    vertices[index++] = cos(angle) * inner + x;
    vertices[index++] = sin(angle) * inner + y;
    vertices[index++] = cos(angle) * outer + x;
    vertices[index++] = sin(angle) * outer + y;
  }

#ifndef __APPLE__
  glGenVertexArrays(1, &vao_);
#endif
  glGenBuffers(1, &vbo_);
#ifndef __APPLE__
  glBindVertexArray(vao_);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifndef __APPLE__
  glBindVertexArray(0);
#endif
  var_color_ = glGetUniformLocation(shaderProgram_, "globalColor");
}

ProgressSprite::~ProgressSprite() {
#ifndef __APPLE__
  glDeleteVertexArrays(1, &vao_);
#endif
  glDeleteBuffers(1, &vbo_);
}

void ProgressSprite::Render() {
  if (current_alpha_ < target_alpha_) current_alpha_ += 5;
  if (current_alpha_ > target_alpha_) current_alpha_ -= 5;
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(shaderProgram_);
  if (progress_ >= 1.0f)
    glUniform4f(var_color_, 1, 0.51, 0.18, float(current_alpha_) / 100.0f);
  else
    glUniform4f(var_color_, 0.47, 0.79, 0.18, float(current_alpha_) / 100.0f);
#ifndef __APPLE__
  glBindVertexArray(vao_);
#else
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
#endif
  glDrawArrays(GL_TRIANGLE_STRIP, 0,
               static_cast<GLsizei>((FULL_SEGMENTS + 1) * 2 * progress_));
#ifndef __APPLE__
  glBindVertexArray(0);
#endif
}
