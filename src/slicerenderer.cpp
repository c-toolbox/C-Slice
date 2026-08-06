/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicerenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <sgct/internalshaders.h>
#include <sgct/opengl.h>
#include <sgct/texturemanager.h>
#include <sgct/viewport.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {

constexpr std::string_view VideoVert = R"(
  #version 460 core

  layout (location = 0) in vec2 in_position;
  layout (location = 1) in vec2 in_texCoord;

  uniform int eye;
  uniform int stereoscopicMode;
  uniform vec4 roi;
  uniform bool flipY;

  out vec2 tr_uv;

  void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    tr_uv = flipY ? vec2(in_texCoord.x, 1.0-in_texCoord.y) : in_texCoord;
    tr_uv = (tr_uv * roi.zw) + roi.xy;

    if(eye==2) {
        if(stereoscopicMode==1) {
            tr_uv = (tr_uv * vec2(0.5, 1.0)) + vec2(0.5, 0.0);
        }
        else if(stereoscopicMode==2) {
            tr_uv = tr_uv * vec2(1.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            tr_uv = tr_uv * vec2(1.0, 0.5);
            tr_uv = vec2(1.0 - tr_uv.y, tr_uv.x);
        }
    }
    else {
        if(stereoscopicMode==1) {
            tr_uv = tr_uv * vec2(0.5, 1.0);
        }
        else if(stereoscopicMode==2) {
            tr_uv = (tr_uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            tr_uv = (tr_uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
            tr_uv = vec2(1.0 - tr_uv.y, tr_uv.x);
        }
    }
  }
)";

constexpr std::string_view MeshVert = R"(
  #version 460 core

  layout (location = 0) in vec2 in_texCoord;
  layout (location = 1) in vec3 in_normal;
  layout (location = 2) in vec3 in_position;

  uniform mat4 mvp;
  uniform int eye;
  uniform int stereoscopicMode;
  uniform vec4 roi;
  uniform bool flipY;

  out vec2 tr_uv;
  out vec3 tr_normals;

  void main() {
    gl_Position = mvp * vec4(in_position, 1.0);
    tr_uv = flipY ? vec2(in_texCoord.x, 1.0-in_texCoord.y) : in_texCoord;
    tr_uv = (tr_uv * roi.zw) + roi.xy;
    tr_normals = in_normal;

    if(eye==2) {
        if(stereoscopicMode==1) {
            tr_uv = (tr_uv * vec2(0.5, 1.0)) + vec2(0.5, 0.0);
        }
        else if(stereoscopicMode==2) {
            tr_uv = tr_uv * vec2(1.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            tr_uv = tr_uv * vec2(1.0, 0.5);
            tr_uv = vec2(1.0 - tr_uv.y, tr_uv.x);
        }
    }
    else {
        if(stereoscopicMode==1) {
            tr_uv = tr_uv * vec2(0.5, 1.0);
        }
        else if(stereoscopicMode==2) {
            tr_uv = (tr_uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            tr_uv = (tr_uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
            tr_uv = vec2(1.0 - tr_uv.y, tr_uv.x);
        }
    }
  }
)";

constexpr std::string_view VideoFrag = R"(
  #version 460 core

  uniform sampler2D tex;
  uniform float alpha;
  uniform bool outside;

  in vec2 tr_uv;
  out vec4 out_color;

  void main() {
    vec2 texCoods = tr_uv;
    if(outside){
        texCoods = vec2(1.0-tr_uv.x, tr_uv.y);
    }

    out_color = texture(tex, texCoods) * vec4(1.0, 1.0, 1.0, alpha);
  }
)";

constexpr std::string_view EACMeshVert = R"(
  #version 460 core

  layout (location = 0) in vec2 in_texCoord;
  layout (location = 1) in vec3 in_normal;
  layout (location = 2) in vec3 in_position;

  uniform mat4 mvp;
  uniform float scaleToUnitCube;
  uniform bool outside;

  out vec3 tr_position;
  out vec3 tr_normal;

  void main() {
    gl_Position = mvp * vec4(in_position, 1.0);
    tr_position = in_position * scaleToUnitCube;

    if(outside)
        tr_normal = -in_normal;
    else
        tr_normal = in_normal;
  }
)";

constexpr std::string_view EACVideoFrag = R"(
  #version 460 core

  uniform sampler2D tex;
  uniform int eye;
  uniform int stereoscopicMode;
  uniform float alpha;
  uniform int videoWidth;
  uniform int videoHeight;
  uniform bool flipY;

  in vec3 tr_position;
  in vec3 tr_normal;
  out vec4 out_color;

  const float M_PI_2 = 1.57079632679489661923;
  const float M_PI_4 = 0.785398163397448309616;
  const float M_2_PI = 0.636619772367581343076;
  const float M_PI = 3.14159265358979323846264338327950288;

  const int TOP_LEFT = 0;
  const int TOP_MIDDLE = 1;
  const int TOP_RIGHT = 2;
  const int BOTTOM_LEFT = 3;
  const int BOTTOM_MIDDLE = 4;
  const int BOTTOM_RIGHT = 5;

  const int RIGHT = 0;
  const int LEFT = 1;
  const int UP = 2;
  const int DOWN = 3;
  const int FRONT = 4;
  const int BACK = 5;

  const int ROT_0 = 0;
  const int ROT_90 = 1;
  const int ROT_180 = 2;
  const int ROT_270 = 3;

  vec2 rotate_cube_face(vec2 uv_in, int rotation)
  {
      vec2 uv_out;

      switch (rotation) {
          case ROT_0:
              uv_out = uv_in;
              break;
          case ROT_90:
              uv_out.x = -uv_in.y;
              uv_out.y = uv_in.x;
              break;
          case ROT_180:
              uv_out.x = -uv_in.x;
              uv_out.y = -uv_in.y;
              break;
          case ROT_270:
              uv_out.x = uv_in.y;
              uv_out.y = -uv_in.x;
              break;
      }

      return uv_out;
  }

  int xyz_to_direction(vec3 xyz)
  {
    int direction;
    float phi = atan(xyz.x, xyz.z);
    float theta = asin(xyz.y);
    float phi_norm, theta_threshold;

    if (phi >= -M_PI_4 && phi < M_PI_4) {
        direction = FRONT;
        phi_norm = phi;
    } else if (phi >= -(M_PI_2 + M_PI_4) && phi < -M_PI_4) {
        direction = LEFT;
        phi_norm = phi + M_PI_2;
    } else if (phi >= M_PI_4 && phi < M_PI_2 + M_PI_4) {
        direction = RIGHT;
        phi_norm = phi - M_PI_2;
    } else {
        direction = BACK;
        phi_norm = phi + ((phi > 0.f) ? -M_PI : M_PI);
    }

    theta_threshold = atan(cos(phi_norm));
    if (theta > theta_threshold) {
        direction = DOWN;
    } else if (theta < -theta_threshold) {
        direction = UP;
    }

    return direction;
  }

  vec2 xyz_to_eac(vec3 xyz, int width, int height)
  {
    float pixel_pad = 2;
    float u_pad = pixel_pad / width;
    float v_pad = pixel_pad / height;

    int in_cubemap_face_order[6];
    in_cubemap_face_order[RIGHT] = TOP_RIGHT;
    in_cubemap_face_order[LEFT] = TOP_LEFT;
    in_cubemap_face_order[UP] = BOTTOM_RIGHT;
    in_cubemap_face_order[DOWN] = BOTTOM_LEFT;
    in_cubemap_face_order[FRONT] = TOP_MIDDLE;
    in_cubemap_face_order[BACK] = BOTTOM_MIDDLE;

    int in_cubemap_face_rotation[6];
    in_cubemap_face_rotation[TOP_LEFT] = ROT_0;
    in_cubemap_face_rotation[TOP_MIDDLE] = ROT_0;
    in_cubemap_face_rotation[TOP_RIGHT] = ROT_0;
    in_cubemap_face_rotation[BOTTOM_LEFT] = ROT_270;
    in_cubemap_face_rotation[BOTTOM_MIDDLE] = ROT_90;
    in_cubemap_face_rotation[BOTTOM_RIGHT] = ROT_270;

    int direction = xyz_to_direction(xyz);

    vec2 uv = vec2(0.0, 0.0);
    switch (direction) {
        case LEFT:
            uv.x = -xyz.z / xyz.x;
            uv.y = xyz.y / xyz.x;
            break;
        case RIGHT:
            uv.x = -xyz.z / xyz.x;
            uv.y = -xyz.y / xyz.x;
            break;
        case DOWN:
            uv.x = -xyz.x / xyz.y;
            uv.y = -xyz.z / xyz.y;
            break;
        case UP:
            uv.x = xyz.x / xyz.y;
            uv.y = -xyz.z / xyz.y;
            break;
        case BACK:
            uv.x = -xyz.x / xyz.z;
            uv.y = -xyz.y / xyz.z;
            break;
        case FRONT:
            uv.x = xyz.x / xyz.z;
            uv.y = -xyz.y / xyz.z;
            break;
    }

    int face = in_cubemap_face_order[direction];
    uv = rotate_cube_face(uv, in_cubemap_face_rotation[face]);

    int u_face = face % 3;
    int v_face = face / 3;

    uv = M_2_PI * atan(uv) + 0.5;

    uv.x = (uv.x + u_face) * (1.0 - 2.0 * u_pad) / 3.0 + u_pad;
    uv.y = uv.y * (0.5 - (2.0 * v_pad)) + v_pad + (0.5 * v_face);

    return uv;
  }

  void main() {
    vec2 uv = xyz_to_eac(normalize(tr_normal), videoWidth, videoHeight);

    if(flipY) {
        uv.y = 1.0 - uv.y;
    }

    if(eye==2) {
        if(stereoscopicMode==1) {
            uv = (uv * vec2(0.5, 1.0)) + vec2(0.5, 0.0);
        }
        else if(stereoscopicMode==2) {
            uv = uv * vec2(1.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            uv = uv * vec2(1.0, 0.5);
            uv = vec2(1.0 - uv.y, uv.x);
        }
    }
    else {
        if(stereoscopicMode==1) {
            uv = uv * vec2(0.5, 1.0);
        }
        else if(stereoscopicMode==2) {
            uv = (uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
        }
        else if(stereoscopicMode==3) {
            uv = (uv * vec2(1.0, 0.5)) + vec2(0.0, 0.5);
            uv = vec2(1.0 - uv.y, uv.x);
        }
    }

    out_color = texture(tex, uv) * vec4(1.0, 1.0, 1.0, alpha);
  }
)";

void setStereoUniforms(int eyeLoc, int stereoLoc, const CSlice::SliceLayer& layer, sgct::FrustumMode currentEye)
{
    if (layer.stereoMode != CSlice::StereoMode::None) {
        glUniform1i(eyeLoc, static_cast<GLint>(currentEye));
        glUniform1i(stereoLoc, static_cast<GLint>(layer.stereoMode));
    }
    else {
        glUniform1i(eyeLoc, 0);
        glUniform1i(stereoLoc, 0);
    }
}

void setRoiUniform(int roiLoc, const CSlice::SliceLayer& layer)
{
    if (layer.roiEnabled) {
        glUniform4fv(roiLoc, 1, glm::value_ptr(layer.roi));
    }
    else {
        glUniform4f(roiLoc, 0.f, 0.f, 1.f, 1.f);
    }
}

} // namespace

namespace CSlice {

SliceRenderer::SliceRenderer() = default;

SliceRenderer::~SliceRenderer()
{
    destroyWarpTargets();
    destroyWarpViewportResources();
    m_planeMesh.reset();
    m_sphereMesh.reset();
    m_domeMesh.reset();
}

void SliceRenderer::configureWarping(std::vector<WarpWindowConfig> windows)
{
    destroyWarpViewportResources();
    m_warpWindows.clear();
    m_warpWindows.reserve(windows.size());

    for (WarpWindowConfig& windowConfig : windows) {
        WarpWindowState windowState;
        windowState.id = windowConfig.id;
        windowState.name = std::move(windowConfig.name);
        windowState.viewports.reserve(windowConfig.viewports.size());
        for (WarpViewportConfig& viewportConfig : windowConfig.viewports) {
            WarpViewportState viewportState;
            viewportState.config = std::move(viewportConfig);
            windowState.viewports.push_back(std::move(viewportState));
        }
        m_warpWindows.push_back(std::move(windowState));
    }
}

void SliceRenderer::initializeGL(double radius, double fov)
{
    auto& shaderManager = sgct::ShaderManager::instance();
    if (!shaderManager.shaderProgramExists("CSlice.mesh")) {
        shaderManager.addShaderProgram("CSlice.mesh", MeshVert, VideoFrag);
    }
    if (!shaderManager.shaderProgramExists("CSlice.eac")) {
        shaderManager.addShaderProgram("CSlice.eac", EACMeshVert, EACVideoFrag);
    }
    if (!shaderManager.shaderProgramExists("CSlice.video")) {
        shaderManager.addShaderProgram("CSlice.video", VideoVert, VideoFrag);
    }
    if (!shaderManager.shaderProgramExists("CSlice.warp")) {
        shaderManager.addShaderProgram("CSlice.warp", sgct::shaders::BaseVert, sgct::shaders::BaseFrag);
    }

    m_meshProgram = &shaderManager.shaderProgram("CSlice.mesh");
    if (m_meshProgram) {
        m_meshProgram->bind();
        glUniform1i(glGetUniformLocation(m_meshProgram->id(), "tex"), 0);
        m_meshMatrixLoc = glGetUniformLocation(m_meshProgram->id(), "mvp");
        m_meshEyeModeLoc = glGetUniformLocation(m_meshProgram->id(), "eye");
        m_meshFlipYLoc = glGetUniformLocation(m_meshProgram->id(), "flipY");
        m_meshStereoscopicModeLoc = glGetUniformLocation(m_meshProgram->id(), "stereoscopicMode");
        m_meshRoiLoc = glGetUniformLocation(m_meshProgram->id(), "roi");
        m_meshAlphaLoc = glGetUniformLocation(m_meshProgram->id(), "alpha");
        m_meshOutsideLoc = glGetUniformLocation(m_meshProgram->id(), "outside");
        m_meshProgram->unbind();
    }

    m_eacProgram = &shaderManager.shaderProgram("CSlice.eac");
    if (m_eacProgram) {
        m_eacProgram->bind();
        glUniform1i(glGetUniformLocation(m_eacProgram->id(), "tex"), 0);
        m_eacMatrixLoc = glGetUniformLocation(m_eacProgram->id(), "mvp");
        m_eacEyeModeLoc = glGetUniformLocation(m_eacProgram->id(), "eye");
        m_eacFlipYLoc = glGetUniformLocation(m_eacProgram->id(), "flipY");
        m_eacStereoscopicModeLoc = glGetUniformLocation(m_eacProgram->id(), "stereoscopicMode");
        m_eacAlphaLoc = glGetUniformLocation(m_eacProgram->id(), "alpha");
        m_eacOutsideLoc = glGetUniformLocation(m_eacProgram->id(), "outside");
        m_eacScaleLoc = glGetUniformLocation(m_eacProgram->id(), "scaleToUnitCube");
        m_eacVideoWidthLoc = glGetUniformLocation(m_eacProgram->id(), "videoWidth");
        m_eacVideoHeightLoc = glGetUniformLocation(m_eacProgram->id(), "videoHeight");
        m_eacProgram->unbind();
    }

    m_videoProgram = &shaderManager.shaderProgram("CSlice.video");
    if (m_videoProgram) {
        m_videoProgram->bind();
        glUniform1i(glGetUniformLocation(m_videoProgram->id(), "tex"), 0);
        m_videoEyeModeLoc = glGetUniformLocation(m_videoProgram->id(), "eye");
        m_videoFlipYLoc = glGetUniformLocation(m_videoProgram->id(), "flipY");
        m_videoStereoscopicModeLoc = glGetUniformLocation(m_videoProgram->id(), "stereoscopicMode");
        m_videoRoiLoc = glGetUniformLocation(m_videoProgram->id(), "roi");
        m_videoAlphaLoc = glGetUniformLocation(m_videoProgram->id(), "alpha");
        m_videoProgram->unbind();
    }

    m_warpProgram = &shaderManager.shaderProgram("CSlice.warp");
    if (m_warpProgram) {
        m_warpProgram->bind();
        glUniform1i(glGetUniformLocation(m_warpProgram->id(), "tex"), 0);
        glUniform1i(glGetUniformLocation(m_warpProgram->id(), "flipX"), 0);
        glUniform1i(glGetUniformLocation(m_warpProgram->id(), "flipY"), 0);
        m_warpProgram->unbind();
    }

    updateMeshes(radius, fov);
}

void SliceRenderer::updateMeshes(double radius, double fov)
{
    if (m_meshRadius == radius && m_meshFov == fov) {
        return;
    }

    m_meshRadius = radius;
    m_meshFov = fov;
    m_domeMesh = std::make_unique<DomeGrid>(static_cast<float>(m_meshRadius) / 100.f, static_cast<float>(m_meshFov), 256, 128);
    m_sphereMesh = std::make_unique<SphereGrid>(static_cast<float>(m_meshRadius) / 100.f, 256);
}

void SliceRenderer::renderLayer(const sgct::RenderData& data,
    const SliceLayer& layer,
    sgct::FrustumMode currentEye,
    float domeAngle,
    int gridModeOverride)
{
    const unsigned int texture = layer.textureId;
    if (texture == 0) {
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const int gridMode = gridModeOverride >= 0 ? gridModeOverride : static_cast<int>(layer.gridMode);
    switch (gridMode) {
        case static_cast<int>(GridMode::SphereEac):
            renderSphereEac(data, layer, currentEye, domeAngle);
            break;
        case static_cast<int>(GridMode::SphereEqr):
            renderSphereEqr(data, layer, currentEye, domeAngle);
            break;
        case static_cast<int>(GridMode::Dome):
            renderDome(data, layer, currentEye, domeAngle);
            break;
        case static_cast<int>(GridMode::Plane):
            renderPlane(data, layer, currentEye, domeAngle);
            break;
        case static_cast<int>(GridMode::None):
        default:
            renderScreenQuad(data, layer, currentEye);
            break;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void SliceRenderer::renderLayerWithWarp(const sgct::RenderData& data,
    const SliceLayer& layer,
    sgct::FrustumMode currentEye,
    float domeAngle,
    int gridModeOverride)
{
    const sgct::Viewport* viewport = dynamic_cast<const sgct::Viewport*>(&data.viewport);
    WarpViewportState* warpState = warpViewportState(data);
    if (!viewport || !warpState || !ensureWarpViewportLoaded(*warpState, *viewport) || !m_warpProgram || !ensureWarpTarget(data.window.id(), data.bufferSize)) {
        renderLayer(data, layer, currentEye, domeAngle, gridModeOverride);
        return;
    }

    WarpTarget& target = m_warpTargets[static_cast<std::size_t>(data.window.id())];

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLboolean previousScissorTest = GL_FALSE;
    GLint previousScissorBox[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    previousScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    renderLayer(data, layer, currentEye, domeAngle, gridModeOverride);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(0, 0, data.bufferSize.x, data.bufferSize.y);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_warpProgram->bind();
    glUniform1i(glGetUniformLocation(m_warpProgram->id(), "flipX"), 0);
    glUniform1i(glGetUniformLocation(m_warpProgram->id(), "flipY"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glDisable(GL_BLEND);
    warpState->mesh.renderWarpMesh();

    if (warpState->blendMaskTexture != 0 || warpState->blackLevelMaskTexture != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        if (warpState->blendMaskTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, warpState->blendMaskTexture);
            warpState->mesh.renderMaskMesh();
        }
        if (warpState->blackLevelMaskTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, warpState->blackLevelMaskTexture);
            warpState->mesh.renderMaskMesh();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_warpProgram->unbind();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);

    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (previousScissorTest) {
        glEnable(GL_SCISSOR_TEST);
    }
    else {
        glDisable(GL_SCISSOR_TEST);
    }
    glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
}

bool SliceRenderer::ensureWarpTarget(int windowId, sgct::ivec2 size)
{
    if (windowId < 0 || size.x <= 0 || size.y <= 0) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(windowId);
    if (m_warpTargets.size() <= index) {
        m_warpTargets.resize(index + 1);
    }

    WarpTarget& target = m_warpTargets[index];
    if (target.framebuffer != 0 && target.texture != 0 && target.size.x == size.x && target.size.y == size.y) {
        return true;
    }

    glDeleteFramebuffers(1, &target.framebuffer);
    glDeleteTextures(1, &target.texture);
    target = WarpTarget{};

    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        glDeleteFramebuffers(1, &target.framebuffer);
        glDeleteTextures(1, &target.texture);
        target = WarpTarget{};
        return false;
    }

    target.size = size;
    return true;
}

SliceRenderer::WarpViewportState* SliceRenderer::warpViewportState(const sgct::RenderData& data)
{
    const std::vector<std::unique_ptr<sgct::Viewport>>& viewports = data.window.viewports();
    const auto viewportIt = std::find_if(viewports.begin(), viewports.end(), [&data](const std::unique_ptr<sgct::Viewport>& viewport) {
        return viewport.get() == &data.viewport;
    });
    if (viewportIt == viewports.end()) {
        return nullptr;
    }

    const std::size_t viewportIndex = static_cast<std::size_t>(std::distance(viewports.begin(), viewportIt));
    const auto windowIt = std::find_if(m_warpWindows.begin(), m_warpWindows.end(), [&data](const WarpWindowState& window) {
        if (!window.name.empty() && window.name == data.window.name()) {
            return true;
        }
        return window.id == data.window.id();
    });
    if (windowIt == m_warpWindows.end() || viewportIndex >= windowIt->viewports.size()) {
        return nullptr;
    }

    return &windowIt->viewports[viewportIndex];
}

bool SliceRenderer::ensureWarpViewportLoaded(WarpViewportState& state, const sgct::Viewport& viewport)
{
    if (state.loaded) {
        return true;
    }
    if (state.loadFailed) {
        return false;
    }

    try {
        state.mesh.loadMesh(state.config.correctionMesh,
            const_cast<sgct::Viewport&>(viewport),
            !state.config.blendMask.empty() || !state.config.blackLevelMask.empty(),
            state.config.textureRenderMode);
        if (!state.config.blendMask.empty()) {
            state.blendMaskTexture = sgct::TextureManager::instance().loadTexture(state.config.blendMask, true, 1.f);
        }
        if (!state.config.blackLevelMask.empty()) {
            state.blackLevelMaskTexture = sgct::TextureManager::instance().loadTexture(state.config.blackLevelMask, true, 1.f);
        }
    }
    catch (const std::runtime_error&) {
        state.loadFailed = true;
        return false;
    }

    state.loaded = true;
    return true;
}

void SliceRenderer::destroyWarpTargets()
{
    for (WarpTarget& target : m_warpTargets) {
        glDeleteFramebuffers(1, &target.framebuffer);
        glDeleteTextures(1, &target.texture);
        target = WarpTarget{};
    }
    m_warpTargets.clear();
}

void SliceRenderer::destroyWarpViewportResources()
{
    for (WarpWindowState& window : m_warpWindows) {
        for (WarpViewportState& viewport : window.viewports) {
            if (viewport.blendMaskTexture != 0) {
                sgct::TextureManager::instance().removeTexture(viewport.blendMaskTexture);
                viewport.blendMaskTexture = 0;
            }
            if (viewport.blackLevelMaskTexture != 0) {
                sgct::TextureManager::instance().removeTexture(viewport.blackLevelMaskTexture);
                viewport.blackLevelMaskTexture = 0;
            }
        }
    }
}

void SliceRenderer::setMeshLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye)
{
    setStereoUniforms(m_meshEyeModeLoc, m_meshStereoscopicModeLoc, layer, currentEye);
    setRoiUniform(m_meshRoiLoc, layer);
    glUniform1f(m_meshAlphaLoc, layer.alpha);
    glUniform1i(m_meshFlipYLoc, layer.flipY ? 1 : 0);
}

void SliceRenderer::setVideoLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye)
{
    setStereoUniforms(m_videoEyeModeLoc, m_videoStereoscopicModeLoc, layer, currentEye);
    setRoiUniform(m_videoRoiLoc, layer);
    glUniform1f(m_videoAlphaLoc, layer.alpha);
    glUniform1i(m_videoFlipYLoc, layer.flipY ? 1 : 0);
}

void SliceRenderer::setEacLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye)
{
    setStereoUniforms(m_eacEyeModeLoc, m_eacStereoscopicModeLoc, layer, currentEye);
    glUniform1f(m_eacAlphaLoc, layer.alpha);
    glUniform1i(m_eacFlipYLoc, layer.flipY ? 1 : 0);
    glUniform1i(m_eacVideoWidthLoc, layer.width);
    glUniform1i(m_eacVideoHeightLoc, layer.height);
    glUniform1f(m_eacScaleLoc, m_meshRadius > 0.0 ? static_cast<float>(100.0 / m_meshRadius) : 1.f);
}

void SliceRenderer::renderScreenQuad(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye)
{
    if (!m_videoProgram) {
        return;
    }

    m_videoProgram->bind();
    setVideoLayerUniforms(layer, currentEye);
    data.window.renderScreenQuad();
    m_videoProgram->unbind();
}

void SliceRenderer::renderDome(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle)
{
    if (!m_meshProgram || !m_domeMesh) {
        return;
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    m_meshProgram->bind();
    setMeshLayerUniforms(layer, currentEye);

    const sgct::mat4 mvp = data.modelViewProjectionMatrix;
    glm::mat4 transform = glm::translate(glm::make_mat4(mvp.values.data()), layer.translate);
    transform = glm::rotate(transform, glm::radians(layer.rotate.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.x - domeAngle), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.y), glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(m_meshMatrixLoc, 1, GL_FALSE, &transform[0][0]);
    glUniform1i(m_meshOutsideLoc, 0);
    m_domeMesh->draw();

    m_meshProgram->unbind();
    glDisable(GL_CULL_FACE);
}

void SliceRenderer::renderSphereEqr(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float)
{
    if (!m_meshProgram || !m_sphereMesh) {
        return;
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    const sgct::mat4 mvp = data.modelViewProjectionMatrix;
    glm::mat4 base = glm::translate(glm::make_mat4(mvp.values.data()), layer.translate);

    m_meshProgram->bind();
    setMeshLayerUniforms(layer, currentEye);

    glm::mat4 transform = base;
    transform = glm::rotate(transform, glm::radians(layer.rotate.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.y - 90.f), glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(m_meshMatrixLoc, 1, GL_FALSE, &transform[0][0]);
    glUniform1i(m_meshOutsideLoc, 0);
    m_sphereMesh->draw();

    m_meshProgram->unbind();
    glDisable(GL_CULL_FACE);
}

void SliceRenderer::renderSphereEac(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float)
{
    if (!m_eacProgram || !m_sphereMesh) {
        return;
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    const sgct::mat4 mvp = data.modelViewProjectionMatrix;
    glm::mat4 base = glm::translate(glm::make_mat4(mvp.values.data()), layer.translate);

    m_eacProgram->bind();
    setEacLayerUniforms(layer, currentEye);

    glm::mat4 transform = base;
    transform = glm::rotate(transform, glm::radians(layer.rotate.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(layer.rotate.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(0.0f, 0.0f, 1.0f));
    glUniformMatrix4fv(m_eacMatrixLoc, 1, GL_FALSE, &transform[0][0]);
    glUniform1i(m_eacOutsideLoc, 0);
    m_sphereMesh->draw();

    m_eacProgram->unbind();
    glDisable(GL_CULL_FACE);
}

bool SliceRenderer::ensurePlaneMesh(const SliceLayer& layer)
{
    if (layer.width <= 0 || layer.height <= 0 || layer.planeWidth <= 0.0 || layer.planeHeight <= 0.0) {
        return false;
    }

    float width = static_cast<float>(layer.width);
    float height = static_cast<float>(layer.height);
    if (layer.roiEnabled) {
        width *= layer.roi.z;
        height *= layer.roi.w;
    }

    glm::vec2 planeSize(static_cast<float>(layer.planeWidth), static_cast<float>(layer.planeHeight));
    const StereoMode stereoMode = layer.stereoMode;
    float ratioMultiplier = 1.0f;
    if (stereoMode == StereoMode::SideBySide) {
        if (width / height >= 2.f) {
            ratioMultiplier = 0.5f;
        }
    }
    else if (stereoMode == StereoMode::TopBottom) {
        if (height / width >= 1.f) {
            ratioMultiplier = 2.0f;
        }
    }
    else if (stereoMode == StereoMode::TopBottomFlip) {
        ratioMultiplier = 2.0f;
    }

    if (layer.planeAspectRatio == 1) {
        float ratio = width / height;
        if (stereoMode == StereoMode::TopBottomFlip) {
            ratio = height / width;
        }
        planeSize.x = ratio * ratioMultiplier * planeSize.y;
    }
    else if (layer.planeAspectRatio == 2) {
        float ratio = height / width;
        if (stereoMode == StereoMode::TopBottomFlip) {
            ratio = width / height;
        }
        planeSize.y = ratio * ratioMultiplier * planeSize.x;
    }

    if (!m_planeMesh || planeSize.x != m_planeWidth || planeSize.y != m_planeHeight) {
        m_planeWidth = planeSize.x;
        m_planeHeight = planeSize.y;
        m_planeMesh = std::make_unique<PlaneGrid>(m_planeWidth / 100.f, m_planeHeight / 100.f);
    }
    return true;
}

void SliceRenderer::renderPlane(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle)
{
    if (!m_meshProgram || !ensurePlaneMesh(layer)) {
        return;
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_meshProgram->bind();
    setMeshLayerUniforms(layer, currentEye);

    const sgct::mat4 mvp = data.projectionMatrix * data.viewMatrix;
    glm::mat4 transform(1.0f);
    transform = glm::rotate(transform, glm::radians(-domeAngle), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(static_cast<float>(layer.planeAzimuth)), glm::vec3(0.0f, -1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(static_cast<float>(layer.planeElevation)), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(static_cast<float>(layer.planeRoll)), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::translate(transform, glm::vec3(
        static_cast<float>(layer.planeHorizontal) / 100.f,
        static_cast<float>(layer.planeVertical) / 100.f,
        static_cast<float>(-layer.planeDistance) / 100.f));

    transform = glm::make_mat4(mvp.values.data()) * transform;
    glUniformMatrix4fv(m_meshMatrixLoc, 1, GL_FALSE, &transform[0][0]);
    glUniform1i(m_meshOutsideLoc, 0);
    m_planeMesh->draw();

    m_meshProgram->unbind();
    glDisable(GL_CULL_FACE);
}

} // namespace CSlice
