/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2025 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

class Vec3f;
class Quatf;
struct AxisMapping;

#include <string.h>

class Matrix4f
{
public:
  Matrix4f() {}
  Matrix4f(const AxisMapping &axisMapping);
  //////////////////////////////////////////////////////
  // Matrix generation functions

  inline static Matrix4f Zero()
  {
    Matrix4f m;
    memset(&m, 0, sizeof(Matrix4f));
    return m;
  }

  inline static Matrix4f Identity()
  {
    Matrix4f m = Zero();
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
  }

  static Matrix4f Translation(const Vec3f &t);
  static Matrix4f RotationX(const float r);
  static Matrix4f RotationY(const float r);
  static Matrix4f RotationZ(const float r);
  static Matrix4f RotationXYZ(const Vec3f &rot);
  static Matrix4f RotationZYX(const Vec3f &rot);
  static Matrix4f Orthographic(const float nearplane, const float farplane);
  static Matrix4f Perspective(const float degfov, const float nearplane, const float farplane,
                              const float aspect);
  static Matrix4f ReversePerspective(const float degfov, const float nearplane, const float aspect);

  inline float operator[](const size_t i) const { return f[i]; }
  inline float &operator[](const size_t i) { return f[i]; }
  Matrix4f Transpose() const;
  float Determinant() const;
  Matrix4f Inverse() const;
  Matrix4f Mul(const Matrix4f &o) const;

  Vec3f Transform(const Vec3f &v, const float w = 1.0f) const;

  const float *Data() const { return &f[0]; }
  void SetFrom(float data[16]) { memcpy(f, data, sizeof(Matrix4f)); }
  const Vec3f GetPosition() const;
  const Vec3f GetForward() const;
  const Vec3f GetRight() const;
  const Vec3f GetUp() const;

private:
  Matrix4f(const float *d) { memcpy(f, d, sizeof(Matrix4f)); }
  friend class Quatf;

  inline size_t matIdx(const size_t x, const size_t y) const { return x + y * 4; }
  float f[16];
};

class Matrix3f
{
public:
  Matrix3f() {}
  inline float operator[](const size_t i) const { return f[i]; }
  inline float &operator[](const size_t i) { return f[i]; }
  Matrix3f Transpose() const;
  float Determinant() const;
  Matrix3f Inverse() const;

  const float *Data() const { return &f[0]; }
  void SetFrom(float data[9]) { memcpy(f, data, sizeof(Matrix3f)); }
private:
  inline size_t matIdx(const size_t x, const size_t y) const { return x + y * 3; }
  float f[9];
};

class Matrix2f
{
public:
  Matrix2f() {}
  Matrix2f(float a, float b, float c, float d)
  {
    f[0] = a;
    f[1] = b;
    f[2] = c;
    f[3] = d;
  }
  inline float operator[](const size_t i) const { return f[i]; }
  inline float &operator[](const size_t i) { return f[i]; }
  Matrix2f Transpose() const;
  float Determinant() const;
  Matrix2f Inverse() const;

  const float *Data() const { return &f[0]; }
  void SetFrom(float data[4]) { memcpy(f, data, sizeof(Matrix2f)); }
private:
  inline size_t matIdx(const size_t x, const size_t y) const { return x + y * 2; }
  float f[4];
};
