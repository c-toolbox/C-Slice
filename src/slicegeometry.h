/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICEGEOMETRY_H
#define CSLICE_SLICEGEOMETRY_H

#include <sgct/opengl.h>

#include <vector>

namespace CSlice {

class Mesh {
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();

    void reset();
    void draw() const;

protected:
    void upload(const std::vector<float> &vertices, const std::vector<unsigned int> &indices);

private:
    GLuint m_vao = 0;
    GLuint m_vertexBuffer = 0;
    GLuint m_indexBuffer = 0;
    GLsizei m_indexCount = 0;
};

class ScreenQuad final : public Mesh {
public:
    ScreenQuad();
};

} // namespace CSlice

#endif // CSLICE_SLICEGEOMETRY_H
