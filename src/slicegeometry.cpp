/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicegeometry.h"

#include <utility>

namespace {

constexpr int VertexStride = 5;

void appendVertex(std::vector<float> &vertices, float x, float y, float z, float u, float v)
{
    vertices.push_back(x);
    vertices.push_back(y);
    vertices.push_back(z);
    vertices.push_back(u);
    vertices.push_back(v);
}

} // namespace

namespace CSlice {

Mesh::Mesh(Mesh&& other) noexcept
{
    *this = std::move(other);
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    m_vao = std::exchange(other.m_vao, 0);
    m_vertexBuffer = std::exchange(other.m_vertexBuffer, 0);
    m_indexBuffer = std::exchange(other.m_indexBuffer, 0);
    m_indexCount = std::exchange(other.m_indexCount, 0);
    return *this;
}

Mesh::~Mesh()
{
    reset();
}

void Mesh::reset()
{
    if (m_indexBuffer != 0) {
        glDeleteBuffers(1, &m_indexBuffer);
        m_indexBuffer = 0;
    }
    if (m_vertexBuffer != 0) {
        glDeleteBuffers(1, &m_vertexBuffer);
        m_vertexBuffer = 0;
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_indexCount = 0;
}

void Mesh::upload(const std::vector<float> &vertices, const std::vector<unsigned int> &indices)
{
    reset();

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VertexStride * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, VertexStride * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));

    glGenBuffers(1, &m_indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
        indices.data(),
        GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    m_indexCount = static_cast<GLsizei>(indices.size());
}

void Mesh::draw() const
{
    if (m_vao == 0 || m_indexCount == 0) {
        return;
    }

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

ScreenQuad::ScreenQuad()
{
    std::vector<float> vertices;
    appendVertex(vertices, -1.f, -1.f, 0.f, 0.f, 0.f);
    appendVertex(vertices,  1.f, -1.f, 0.f, 1.f, 0.f);
    appendVertex(vertices, -1.f,  1.f, 0.f, 0.f, 1.f);
    appendVertex(vertices,  1.f,  1.f, 0.f, 1.f, 1.f);
    upload(vertices, { 0, 1, 2, 2, 1, 3 });
}

} // namespace CSlice
