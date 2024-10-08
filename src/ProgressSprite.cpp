﻿#include "ProgressSprite.hpp"
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
  glLinkProgram(shaderProgram_);

  float vertices[(FULL_SEGMENTS + 1) * 4]; // Enough for a full circle plus two extra points
  int index = 0;
  for (int i = 0; i <= FULL_SEGMENTS; i++) {
    float angle = 2.0f * M_PI * i / FULL_SEGMENTS + M_PI / 2;
    // Inner vertex
    vertices[index++] = cos(angle) * inner + x;
    vertices[index++] = sin(angle) * inner + y;
    // Outer vertex
    vertices[index++] = cos(angle) * outer + x;
    vertices[index++] = sin(angle) * outer + y;
  }

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  var_color_ = glGetUniformLocation(shaderProgram_, "globalColor");
}

ProgressSprite::~ProgressSprite() {
  glDeleteVertexArrays(1, &vao_);
  glDeleteBuffers(1, &vbo_);
}

void ProgressSprite::Render() {
  if (current_alpha_ < target_alpha_) {
    current_alpha_ += 5;
  }
  if (current_alpha_ > target_alpha_) {
    current_alpha_ -= 5;
  }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(shaderProgram_);
  if (progress_ >= 1.0f) {
    glUniform4f(var_color_, 1, 0.51, 0.18, float(current_alpha_) / 100.0f);
  } else {
    glUniform4f(var_color_, 0.47, 0.79, 0.18, float(current_alpha_) / 100.0f);
  }
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, (FULL_SEGMENTS + 1) * 2 * progress_);
  glBindVertexArray(0);
}
