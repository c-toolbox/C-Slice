/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICERENDERER_H
#define CSLICE_SLICERENDERER_H

#include "slicewuffs.h"
#include "slicetypes.h"
#include <sgct/correctionmesh.h>
#include <sgct/sgct.h>
#include <utils/domegrid.h>
#include <utils/planegrid.h>
#include <utils/spheregrid.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace CSlice {

class SliceRenderer {
public:
    SliceRenderer();
    SliceRenderer(const SliceRenderer&) = delete;
    SliceRenderer& operator=(const SliceRenderer&) = delete;
    ~SliceRenderer();

    struct WarpViewportConfig {
        std::filesystem::path correctionMesh;
        std::filesystem::path blendMask;
        std::filesystem::path blackLevelMask;
        bool textureRenderMode = false;
    };

    struct WarpWindowConfig {
        int id = -1;
        std::string name;
        std::vector<WarpViewportConfig> viewports;
    };

    void initializeGL(double radius, double fov);
    void configureWarping(std::vector<WarpWindowConfig> windows);
    void updateMeshes(double radius, double fov);
    void renderLayer(const sgct::RenderData& data,
        const SliceLayer& layer,
        sgct::FrustumMode currentEye,
        float domeAngle,
        int gridModeOverride = -1);
    void renderLayerWithWarp(const sgct::RenderData& data,
        const SliceLayer& layer,
        sgct::FrustumMode currentEye,
        float domeAngle,
        int gridModeOverride = -1);

private:
    struct WarpTarget {
        unsigned int framebuffer = 0;
        unsigned int texture = 0;
        sgct::ivec2 size = sgct::ivec2{ 0, 0 };
    };

    struct WarpViewportState {
        WarpViewportConfig config;
        sgct::CorrectionMesh mesh;
        unsigned int blendMaskTexture = 0;
        unsigned int blackLevelMaskTexture = 0;
        bool loaded = false;
        bool loadFailed = false;
    };

    struct WarpWindowState {
        int id = -1;
        std::string name;
        std::vector<WarpViewportState> viewports;
    };

    bool ensureWarpTarget(int windowId, sgct::ivec2 size);
    WarpViewportState* warpViewportState(const sgct::RenderData& data);
    bool ensureWarpViewportLoaded(WarpViewportState& state, const sgct::Viewport& viewport);
    void destroyWarpTargets();
    void destroyWarpViewportResources();
    void renderScreenQuad(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye);
    void renderDome(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle);
    void renderSphereEqr(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle);
    void renderSphereEac(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle);
    void renderPlane(const sgct::RenderData& data, const SliceLayer& layer, sgct::FrustumMode currentEye, float domeAngle);
    void setMeshLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye);
    void setVideoLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye);
    void setEacLayerUniforms(const SliceLayer& layer, sgct::FrustumMode currentEye);
    bool ensurePlaneMesh(const SliceLayer& layer);

    double m_meshRadius = 0.0;
    double m_meshFov = 0.0;
    float m_planeWidth = 0.f;
    float m_planeHeight = 0.f;

    int m_videoAlphaLoc = -1;
    int m_videoEyeModeLoc = -1;
    int m_videoFlipYLoc = -1;
    int m_videoStereoscopicModeLoc = -1;
    int m_videoRoiLoc = -1;

    int m_meshAlphaLoc = -1;
    int m_meshEyeModeLoc = -1;
    int m_meshFlipYLoc = -1;
    int m_meshMatrixLoc = -1;
    int m_meshOutsideLoc = -1;
    int m_meshStereoscopicModeLoc = -1;
    int m_meshRoiLoc = -1;

    int m_eacAlphaLoc = -1;
    int m_eacFlipYLoc = -1;
    int m_eacMatrixLoc = -1;
    int m_eacScaleLoc = -1;
    int m_eacOutsideLoc = -1;
    int m_eacVideoWidthLoc = -1;
    int m_eacVideoHeightLoc = -1;
    int m_eacEyeModeLoc = -1;
    int m_eacStereoscopicModeLoc = -1;

    const sgct::ShaderProgram* m_videoProgram = nullptr;
    const sgct::ShaderProgram* m_meshProgram = nullptr;
    const sgct::ShaderProgram* m_eacProgram = nullptr;
    const sgct::ShaderProgram* m_warpProgram = nullptr;

    std::unique_ptr<DomeGrid> m_domeMesh;
    std::unique_ptr<SphereGrid> m_sphereMesh;
    std::unique_ptr<PlaneGrid> m_planeMesh;
    std::vector<WarpTarget> m_warpTargets;
    std::vector<WarpWindowState> m_warpWindows;
};

} // namespace CSlice

#endif // CSLICE_SLICERENDERER_H
