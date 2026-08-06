/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.ctoolbox.cslice

Dialog {
    id: root

    property var controller

    title: qsTr("C-Slice Preferences")
    modal: true
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 8 : 560, 560)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 8 : 760, 760)
    anchors.centerIn: parent

    function loadFields() {
        config2DField.text = SliceSettings.configuration2D
        config3DField.text = SliceSettings.configuration3D
        firstLeftInputLocationField.text = SliceSettings.firstLeftInputLocation
        firstRightInputLocationField.text = SliceSettings.firstRightInputLocation
        firstOutputDirectoryLocationField.text = SliceSettings.firstOutputDirectoryLocation
        var decodingModeIndex = decodingModeCombo.model.indexOf(SliceSettings.videoDecodingMode)
        decodingModeCombo.currentIndex = decodingModeIndex >= 0 ? decodingModeIndex : 0
        inputFrameRateToleranceSpin.value = Math.round(SliceSettings.inputFrameRateTolerance * 100)
        var formatIndex = formatCombo.model.indexOf(SliceSettings.format)
        formatCombo.currentIndex = formatIndex >= 0 ? formatIndex : 0
        surfaceRadiusField.text = Number(SliceSettings.surfaceRadius).toFixed(1)
        surfaceFovField.text = Number(SliceSettings.surfaceFov).toFixed(1)
        layerPitchSpin.value = Math.round(SliceSettings.layerPitch * 10)
        layerYawSpin.value = Math.round(SliceSettings.layerYaw * 10)
        layerRollSpin.value = Math.round(SliceSettings.layerRoll * 10)
        planeAzimuthSpin.value = Math.round(SliceSettings.planeAzimuth * 10)
        planeElevationSpin.value = Math.round(SliceSettings.planeElevation * 10)
        planeRollSpin.value = Math.round(SliceSettings.planeRoll * 10)
        planeDistanceSpin.value = Math.round(SliceSettings.planeDistance)
        planeHorizontalSpin.value = Math.round(SliceSettings.planeHorizontal)
        planeVerticalSpin.value = Math.round(SliceSettings.planeVertical)
        planeWidthSpin.value = Math.round(SliceSettings.planeWidth)
        planeHeightSpin.value = Math.round(SliceSettings.planeHeight)
        var codecIndex = codecCombo.model.indexOf(SliceSettings.codec)
        codecCombo.currentIndex = codecIndex >= 0 ? codecIndex : 4
        crfSpin.value = SliceSettings.cRF
        cqSpin.value = SliceSettings.cQ
        qscaleSpin.value = SliceSettings.qScale
        frameRateNumSpin.value = SliceSettings.frameRateNum
        frameRateDenSpin.value = SliceSettings.frameRateDen
        frameRatePresetCombo.currentIndex = frameRatePresetIndex(frameRateNumSpin.value, frameRateDenSpin.value)
        var softwarePresetIndex = softwarePresetCombo.model.indexOf(SliceSettings.softwarePreset)
        softwarePresetCombo.currentIndex = softwarePresetIndex >= 0 ? softwarePresetIndex : 0
        var nvencPresetIndex = nvencPresetCombo.model.indexOf(SliceSettings.nvencPreset)
        nvencPresetCombo.currentIndex = nvencPresetIndex >= 0 ? nvencPresetIndex : 0
        var libxTuneIndex = libxTuneCombo.model.indexOf(SliceSettings.libxTune)
        libxTuneCombo.currentIndex = libxTuneIndex >= 0 ? libxTuneIndex : 0
        var nvencTuneIndex = nvencTuneCombo.model.indexOf(SliceSettings.nvencTune)
        nvencTuneCombo.currentIndex = nvencTuneIndex >= 0 ? nvencTuneIndex : 0
        nvencHardwareFramesCheck.checked = SliceSettings.nvencHardwareFrames
        bitDepthCombo.currentIndex = SliceSettings.encodingBitDepth >= 10 ? 1 : 0
        paramFileField.text = SliceSettings.paramFile
        preferNtscOutputFrameRatesCheck.checked = SliceSettings.preferNtscOutputFrameRates
        preferMatroskaCheck.checked = SliceSettings.preferMatroska
        encoderThreadsSpin.value = SliceSettings.maxEncoderThreads
        captureGpuSlotsSpin.value = SliceSettings.captureGpuSlots
        imageBufferingThreadsSpin.value = SliceSettings.imageBufferingThreadCount
        imageSizeWarningPercentSpin.value = SliceSettings.imageSizeWarningPercent
        useOnlyIframesCheck.checked = SliceSettings.useOnlyIframes
        gopSizeSpin.realValue = SliceSettings.gopSizeSeconds
        var imageErrorBehaviorIndex = imageErrorBehaviorCombo.behaviorValues.indexOf(SliceSettings.imageErrorBehavior)
        imageErrorBehaviorCombo.currentIndex = imageErrorBehaviorIndex >= 0 ? imageErrorBehaviorIndex : 2
    }

    function storeFields() {
        SliceSettings.configuration2D = config2DField.text
        SliceSettings.configuration3D = config3DField.text
        SliceSettings.firstLeftInputLocation = firstLeftInputLocationField.text
        SliceSettings.firstRightInputLocation = firstRightInputLocationField.text
        SliceSettings.firstOutputDirectoryLocation = firstOutputDirectoryLocationField.text
        SliceSettings.videoDecodingMode = decodingModeCombo.currentText
        SliceSettings.format = formatCombo.currentText
        SliceSettings.surfaceRadius = Number(surfaceRadiusField.text)
        SliceSettings.surfaceFov = Number(surfaceFovField.text)
        SliceSettings.layerPitch = layerPitchSpin.value / 10
        SliceSettings.layerYaw = layerYawSpin.value / 10
        SliceSettings.layerRoll = layerRollSpin.value / 10
        SliceSettings.planeAzimuth = planeAzimuthSpin.value / 10
        SliceSettings.planeElevation = planeElevationSpin.value / 10
        SliceSettings.planeRoll = planeRollSpin.value / 10
        SliceSettings.planeDistance = planeDistanceSpin.value
        SliceSettings.planeHorizontal = planeHorizontalSpin.value
        SliceSettings.planeVertical = planeVerticalSpin.value
        SliceSettings.planeWidth = planeWidthSpin.value
        SliceSettings.planeHeight = planeHeightSpin.value
        SliceSettings.codec = codecCombo.currentText
        SliceSettings.frameRateNum = frameRateNumSpin.value
        SliceSettings.frameRateDen = frameRateDenSpin.value
        SliceSettings.softwarePreset = softwarePresetCombo.currentText
        SliceSettings.nvencPreset = nvencPresetCombo.currentText
        SliceSettings.libxTune = libxTuneCombo.currentText
        SliceSettings.nvencTune = nvencTuneCombo.currentText
        SliceSettings.nvencHardwareFrames = nvencHardwareFramesCheck.checked
        SliceSettings.encodingBitDepth = bitDepthCombo.currentIndex === 1 ? 10 : 8
        SliceSettings.cRF = crfSpin.value
        SliceSettings.cQ = cqSpin.value
        SliceSettings.qScale = qscaleSpin.value
        SliceSettings.paramFile = paramFileField.text
        SliceSettings.preferNtscOutputFrameRates = preferNtscOutputFrameRatesCheck.checked
        SliceSettings.preferMatroska = preferMatroskaCheck.checked
        SliceSettings.maxEncoderThreads = encoderThreadsSpin.value
        SliceSettings.captureGpuSlots = captureGpuSlotsSpin.value
        SliceSettings.imageBufferingThreadCount = imageBufferingThreadsSpin.value
        SliceSettings.imageSizeWarningPercent = imageSizeWarningPercentSpin.value
        SliceSettings.useOnlyIframes = useOnlyIframesCheck.checked
        SliceSettings.gopSizeSeconds = gopSizeSpin.realValue
        SliceSettings.imageErrorBehavior = imageErrorBehaviorCombo.behaviorValues[imageErrorBehaviorCombo.currentIndex]
        SliceSettings.inputFrameRateTolerance = inputFrameRateToleranceSpin.value / 100
    }

    function setDefaults() {
        config2DField.text = SliceSettings.defaultConfiguration2DValue
        config3DField.text = SliceSettings.defaultConfiguration3DValue
        firstLeftInputLocationField.text = ""
        firstRightInputLocationField.text = ""
        firstOutputDirectoryLocationField.text = ""
        var defaultDecodingModeIndex = decodingModeCombo.model.indexOf(SliceSettings.defaultVideoDecodingModeValue)
        decodingModeCombo.currentIndex = defaultDecodingModeIndex >= 0 ? defaultDecodingModeIndex : 0
        var formatIndex = formatCombo.model.indexOf(SliceSettings.defaultFormatValue)
        formatCombo.currentIndex = formatIndex >= 0 ? formatIndex : 0
        surfaceRadiusField.text = Number(SliceSettings.defaultSurfaceRadiusValue).toFixed(1)
        surfaceFovField.text = Number(SliceSettings.defaultSurfaceFovValue).toFixed(1)
        layerPitchSpin.value = Math.round(SliceSettings.defaultLayerPitchValue * 10)
        layerYawSpin.value = Math.round(SliceSettings.defaultLayerYawValue * 10)
        layerRollSpin.value = Math.round(SliceSettings.defaultLayerRollValue * 10)
        planeAzimuthSpin.value = Math.round(SliceSettings.defaultPlaneAzimuthValue * 10)
        planeElevationSpin.value = Math.round(SliceSettings.defaultPlaneElevationValue * 10)
        planeRollSpin.value = Math.round(SliceSettings.defaultPlaneRollValue * 10)
        planeDistanceSpin.value = Math.round(SliceSettings.defaultPlaneDistanceValue)
        planeHorizontalSpin.value = Math.round(SliceSettings.defaultPlaneHorizontalValue)
        planeVerticalSpin.value = Math.round(SliceSettings.defaultPlaneVerticalValue)
        planeWidthSpin.value = Math.round(SliceSettings.defaultPlaneWidthValue)
        planeHeightSpin.value = Math.round(SliceSettings.defaultPlaneHeightValue)
        var codecIndex = codecCombo.model.indexOf(SliceSettings.defaultCodecValue)
        codecCombo.currentIndex = codecIndex >= 0 ? codecIndex : 0
        crfSpin.value = SliceSettings.defaultCRFValue
        cqSpin.value = SliceSettings.defaultCQValue
        qscaleSpin.value = SliceSettings.defaultQScaleValue
        frameRateNumSpin.value = SliceSettings.defaultFrameRateNumValue
        frameRateDenSpin.value = SliceSettings.defaultFrameRateDenValue
        frameRatePresetCombo.currentIndex = frameRatePresetIndex(frameRateNumSpin.value, frameRateDenSpin.value)
        var softwarePresetIndex = softwarePresetCombo.model.indexOf(SliceSettings.defaultSoftwarePresetValue)
        softwarePresetCombo.currentIndex = softwarePresetIndex >= 0 ? softwarePresetIndex : 0
        var nvencPresetIndex = nvencPresetCombo.model.indexOf(SliceSettings.defaultNvencPresetValue)
        nvencPresetCombo.currentIndex = nvencPresetIndex >= 0 ? nvencPresetIndex : 0
        var libxTuneIndex = libxTuneCombo.model.indexOf(SliceSettings.defaultLibxTuneValue)
        libxTuneCombo.currentIndex = libxTuneIndex >= 0 ? libxTuneIndex : 0
        var nvencTuneIndex = nvencTuneCombo.model.indexOf(SliceSettings.defaultNvencTuneValue)
        nvencTuneCombo.currentIndex = nvencTuneIndex >= 0 ? nvencTuneIndex : 0
        nvencHardwareFramesCheck.checked = SliceSettings.defaultNvencHardwareFramesValue
        bitDepthCombo.currentIndex = SliceSettings.defaultEncodingBitDepthValue >= 10 ? 1 : 0
        paramFileField.text = ""
        preferNtscOutputFrameRatesCheck.checked = SliceSettings.defaultPreferNtscOutputFrameRatesValue
        preferMatroskaCheck.checked = SliceSettings.defaultPreferMatroskaValue
        encoderThreadsSpin.value = SliceSettings.defaultMaxEncoderThreadsValue
        captureGpuSlotsSpin.value = SliceSettings.defaultCaptureGpuSlotsValue
        imageBufferingThreadsSpin.value = SliceSettings.defaultImageBufferingThreadCountValue
        imageSizeWarningPercentSpin.value = SliceSettings.defaultImageSizeWarningPercentValue
        var defaultImageErrorBehaviorIndex = imageErrorBehaviorCombo.behaviorValues.indexOf(SliceSettings.defaultImageErrorBehaviorValue)
        imageErrorBehaviorCombo.currentIndex = defaultImageErrorBehaviorIndex >= 0 ? defaultImageErrorBehaviorIndex : 2
        useOnlyIframesCheck.checked = SliceSettings.defaultUseOnlyIframesValue
        gopSizeSpin.value = SliceSettings.defaultGopSizeSecondsValue
        inputFrameRateToleranceSpin.value = Math.round(SliceSettings.defaultInputFrameRateToleranceValue * 100)
    }

    onOpened: loadFields()

    function loadRuntimeFields() {
        if (!root.controller) {
            return
        }
        config2DField.text = root.controller.configuration
        firstOutputDirectoryLocationField.text = root.controller.outputDirectory
        var formatIndex = formatCombo.model.indexOf(root.controller.mappingMode)
        formatCombo.currentIndex = formatIndex >= 0 ? formatIndex : 0
        surfaceRadiusField.text = Number(root.controller.surfaceRadius).toFixed(1)
        surfaceFovField.text = Number(root.controller.surfaceFov).toFixed(1)
        layerPitchSpin.value = Math.round(root.controller.layerPitch * 10)
        layerYawSpin.value = Math.round(root.controller.layerYaw * 10)
        layerRollSpin.value = Math.round(root.controller.layerRoll * 10)
        planeAzimuthSpin.value = Math.round(root.controller.planeAzimuth * 10)
        planeElevationSpin.value = Math.round(root.controller.planeElevation * 10)
        planeRollSpin.value = Math.round(root.controller.planeRoll * 10)
        planeDistanceSpin.value = Math.round(root.controller.planeDistance)
        planeHorizontalSpin.value = Math.round(root.controller.planeHorizontal)
        planeVerticalSpin.value = Math.round(root.controller.planeVertical)
        planeWidthSpin.value = Math.round(root.controller.planeWidth)
        planeHeightSpin.value = Math.round(root.controller.planeHeight)
        var codecIndex = codecCombo.model.indexOf(root.controller.codec)
        codecCombo.currentIndex = codecIndex >= 0 ? codecIndex : 0
        frameRateNumSpin.value = root.controller.frameRateNum
        frameRateDenSpin.value = root.controller.frameRateDen
        frameRatePresetCombo.currentIndex = frameRatePresetIndex(frameRateNumSpin.value, frameRateDenSpin.value)
        softwarePresetCombo.currentIndex = Math.max(0, softwarePresetCombo.model.indexOf(root.controller.softwarePreset))
        nvencPresetCombo.currentIndex = Math.max(0, nvencPresetCombo.model.indexOf(root.controller.nvencPreset))
        libxTuneCombo.currentIndex = Math.max(0, libxTuneCombo.model.indexOf(root.controller.libxTune))
        nvencTuneCombo.currentIndex = Math.max(0, nvencTuneCombo.model.indexOf(root.controller.nvencTune))
        nvencHardwareFramesCheck.checked = root.controller.nvencHardwareFrames
        bitDepthCombo.currentIndex = root.controller.encodingBitDepth >= 10 ? 1 : 0
        crfSpin.value = root.controller.crf
        cqSpin.value = root.controller.cq
        qscaleSpin.value = root.controller.qscale
        paramFileField.text = root.controller.parameterFile
        preferNtscOutputFrameRatesCheck.checked = root.controller.preferNtscOutputFrameRates
        preferMatroskaCheck.checked = root.controller.preferMatroska
        encoderThreadsSpin.value = root.controller.maxEncoderThreads
        captureGpuSlotsSpin.value = root.controller.captureGpuSlots
        imageBufferingThreadsSpin.value = root.controller.imageBufferingThreadCount
        imageSizeWarningPercentSpin.value = root.controller.imageSizeWarningPercent
        useOnlyIframesCheck.checked = root.controller.useOnlyIframes
        gopSizeSpin.value = root.controller.gopSizeSeconds * 10
        var imageErrorBehaviorIndex = imageErrorBehaviorCombo.behaviorValues.indexOf(root.controller.imageErrorBehavior)
        imageErrorBehaviorCombo.currentIndex = imageErrorBehaviorIndex >= 0 ? imageErrorBehaviorIndex : 2
    }

    function isNvencCodec(codec) {
        return codec === "H264 NVENC" || codec === "H265 NVENC"
    }

    function isSoftwarePresetCodec(codec) {
        return codec === "H264" || codec === "H265"
    }

    function isTuneCodec(codec) {
        return isSoftwarePresetCodec(codec) || isNvencCodec(codec)
    }

    function hasQuality(codec) {
        return codec !== "Hap"
    }

    function isQScaleCodec(codec) {
        return hasQuality(codec) && !isSoftwarePresetCodec(codec) && !isNvencCodec(codec)
    }

    function supportsEncodingBitDepth(codec) {
        return codec === "H264" || codec === "H265" || codec === "H264 NVENC" || codec === "H265 NVENC" || codec === "ProRes"
    }

    function frameRatePresetIndex(num, den) {
        if (num === 1 && den === 30) {
            return 0
        }
        if (num === 1 && den === 60) {
            return 1
        }
        if (num === 1001 && den === 30000) {
            return 2
        }
        if (num === 1001 && den === 60000) {
            return 3
        }
        return 4
    }

    function setFrameRatePreset(index) {
        if (index === 0) {
            frameRateNumSpin.value = 1
            frameRateDenSpin.value = 30
        }
        else if (index === 1) {
            frameRateNumSpin.value = 1
            frameRateDenSpin.value = 60
        }
        else if (index === 2) {
            frameRateNumSpin.value = 1001
            frameRateDenSpin.value = 30000
        }
        else if (index === 3) {
            frameRateNumSpin.value = 1001
            frameRateDenSpin.value = 60000
        }
    }

    function softwarePresetModel() {
        return [ "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" ]
    }

    function nvencPresetModel() {
        return [ qsTr("Fastest (P1)"), qsTr("Very fast (P2)"), qsTr("Faster (P3)"), qsTr("Balanced fast (P4)"), qsTr("Balanced quality (P5)"), qsTr("High quality (P6)"), qsTr("Best quality (P7)") ]
    }

    function libxTuneModel() {
        return [ qsTr("none"), "film", "animation", "grain", "stillimage", "psnr", "ssim", "fastdecode", "zerolatency" ]
    }

    function nvencTuneModel() {
        return [ qsTr("High quality"), qsTr("Low latency"), qsTr("Ultra low latency"), qsTr("Lossless") ]
    }

    function decimalSpinText(value, locale) {
        return Number(value / 10).toLocaleString(locale, "f", 1)
    }

    function decimalSpinValue(text, locale) {
        return Math.round(Number.fromLocaleString(locale, text) * 10)
    }

    function currentValues() {
        return {
            "Configuration2D": config2DField.text,
            "Configuration3D": config3DField.text,
            "FirstLeftInputLocation": firstLeftInputLocationField.text,
            "FirstRightInputLocation": firstRightInputLocationField.text,
            "FirstOutputDirectoryLocation": firstOutputDirectoryLocationField.text,
            "VideoDecodingMode": decodingModeCombo.currentText,
            "Format": formatCombo.currentText,
            "SurfaceRadius": surfaceRadiusField.text,
            "SurfaceFov": surfaceFovField.text,
            "LayerPitch": layerPitchSpin.value / 10,
            "LayerYaw": layerYawSpin.value / 10,
            "LayerRoll": layerRollSpin.value / 10,
            "PlaneAzimuth": planeAzimuthSpin.value / 10,
            "PlaneElevation": planeElevationSpin.value / 10,
            "PlaneRoll": planeRollSpin.value / 10,
            "PlaneDistance": planeDistanceSpin.value,
            "PlaneHorizontal": planeHorizontalSpin.value,
            "PlaneVertical": planeVerticalSpin.value,
            "PlaneWidth": planeWidthSpin.value,
            "PlaneHeight": planeHeightSpin.value,
            "Codec": codecCombo.currentText,
            "FrameRate": Math.max(1, Math.floor(frameRateDenSpin.value / Math.max(1, frameRateNumSpin.value))),
            "FrameRateNum": frameRateNumSpin.value,
            "FrameRateDen": frameRateDenSpin.value,
            "SoftwarePreset": softwarePresetCombo.currentText,
            "NvencPreset": nvencPresetCombo.currentText,
            "LibxTune": libxTuneCombo.currentText,
            "NvencTune": nvencTuneCombo.currentText,
            "NvencHardwareFrames": nvencHardwareFramesCheck.checked,
            "EncodingBitDepth": bitDepthCombo.currentIndex === 1 ? 10 : 8,
            "CRF": crfSpin.value,
            "CQ": cqSpin.value,
            "QScale": qscaleSpin.value,
            "ParamFile": paramFileField.text,
            "PreferNtscOutputFrameRates": preferNtscOutputFrameRatesCheck.checked,
            "PreferMatroska": preferMatroskaCheck.checked,
            "MaxEncoderThreads": encoderThreadsSpin.value,
            "CaptureGpuSlots": captureGpuSlotsSpin.value,
            "ImageBufferingThreadCount": imageBufferingThreadsSpin.value,
            "ImageSizeWarningPercent": imageSizeWarningPercentSpin.value,
            "UseOnlyIframes": useOnlyIframesCheck.checked,
            "GopSize": gopSizeSpin.value,
            "ImageErrorBehavior": imageErrorBehaviorCombo.behaviorValues[imageErrorBehaviorCombo.currentIndex]
        }
    }

    function applyValues(values) {
        if (!values || Object.keys(values).length === 0) {
            return
        }
        if (values.Configuration2D !== undefined) config2DField.text = values.Configuration2D
        if (values.Configuration3D !== undefined) config3DField.text = values.Configuration3D
        if (values.FirstLeftInputLocation !== undefined) firstLeftInputLocationField.text = values.FirstLeftInputLocation
        if (values.FirstRightInputLocation !== undefined) firstRightInputLocationField.text = values.FirstRightInputLocation
        if (values.FirstOutputDirectoryLocation !== undefined) firstOutputDirectoryLocationField.text = values.FirstOutputDirectoryLocation
        if (values.VideoDecodingMode !== undefined) decodingModeCombo.currentIndex = Math.max(0, decodingModeCombo.model.indexOf(values.VideoDecodingMode))
        if (values.Format !== undefined) formatCombo.currentIndex = Math.max(0, formatCombo.model.indexOf(values.Format))
        if (values.SurfaceRadius !== undefined) surfaceRadiusField.text = values.SurfaceRadius
        if (values.SurfaceFov !== undefined) surfaceFovField.text = values.SurfaceFov
        if (values.LayerPitch !== undefined) layerPitchSpin.value = Math.round(Number(values.LayerPitch) * 10)
        if (values.LayerYaw !== undefined) layerYawSpin.value = Math.round(Number(values.LayerYaw) * 10)
        if (values.LayerRoll !== undefined) layerRollSpin.value = Math.round(Number(values.LayerRoll) * 10)
        if (values.PlaneAzimuth !== undefined) planeAzimuthSpin.value = Math.round(Number(values.PlaneAzimuth) * 10)
        if (values.PlaneElevation !== undefined) planeElevationSpin.value = Math.round(Number(values.PlaneElevation) * 10)
        if (values.PlaneRoll !== undefined) planeRollSpin.value = Math.round(Number(values.PlaneRoll) * 10)
        if (values.PlaneDistance !== undefined) planeDistanceSpin.value = Number(values.PlaneDistance)
        if (values.PlaneHorizontal !== undefined) planeHorizontalSpin.value = Number(values.PlaneHorizontal)
        if (values.PlaneVertical !== undefined) planeVerticalSpin.value = Number(values.PlaneVertical)
        if (values.PlaneWidth !== undefined) planeWidthSpin.value = Number(values.PlaneWidth)
        if (values.PlaneHeight !== undefined) planeHeightSpin.value = Number(values.PlaneHeight)
        if (values.Codec !== undefined) codecCombo.currentIndex = Math.max(0, codecCombo.model.indexOf(values.Codec))
        if (values.SoftwarePreset !== undefined) softwarePresetCombo.currentIndex = Math.max(0, softwarePresetCombo.model.indexOf(values.SoftwarePreset))
        if (values.NvencPreset !== undefined) nvencPresetCombo.currentIndex = Math.max(0, nvencPresetCombo.model.indexOf(values.NvencPreset))
        if (values.Preset !== undefined) {
            if (values.SoftwarePreset === undefined) softwarePresetCombo.currentIndex = Math.max(0, softwarePresetCombo.model.indexOf(values.Preset))
            if (values.NvencPreset === undefined) nvencPresetCombo.currentIndex = Math.max(0, nvencPresetCombo.model.indexOf(values.Preset))
        }
        if (values.LibxTune !== undefined) libxTuneCombo.currentIndex = Math.max(0, libxTuneCombo.model.indexOf(values.LibxTune))
        if (values.NvencTune !== undefined) nvencTuneCombo.currentIndex = Math.max(0, nvencTuneCombo.model.indexOf(values.NvencTune))
        if (values.NvencHardwareFrames !== undefined) nvencHardwareFramesCheck.checked = values.NvencHardwareFrames === true || values.NvencHardwareFrames === "true" || values.NvencHardwareFrames === "1"
        if (values.EncodingBitDepth !== undefined) bitDepthCombo.currentIndex = Number(values.EncodingBitDepth) >= 10 ? 1 : 0
        if (values.CRF !== undefined) crfSpin.value = Number(values.CRF)
        if (values.CQ !== undefined) cqSpin.value = Number(values.CQ)
        if (values.QScale !== undefined) qscaleSpin.value = Number(values.QScale)
        if (values.FrameRate !== undefined) { frameRateNumSpin.value = 1; frameRateDenSpin.value = Number(values.FrameRate) }
        if (values.FrameRateNum !== undefined) frameRateNumSpin.value = Number(values.FrameRateNum)
        if (values.FrameRateDen !== undefined) frameRateDenSpin.value = Number(values.FrameRateDen)
        if (values.FrameRate !== undefined || values.FrameRateNum !== undefined || values.FrameRateDen !== undefined) frameRatePresetCombo.currentIndex = frameRatePresetIndex(frameRateNumSpin.value, frameRateDenSpin.value)
        if (values.ParamFile !== undefined) paramFileField.text = values.ParamFile
        if (values.PreferNtscOutputFrameRates !== undefined) preferNtscOutputFrameRatesCheck.checked = values.PreferNtscOutputFrameRates === true || values.PreferNtscOutputFrameRates === "true" || values.PreferNtscOutputFrameRates === "1"
        if (values.PreferMatroska !== undefined) preferMatroskaCheck.checked = values.PreferMatroska === true || values.PreferMatroska === "true" || values.PreferMatroska === "1"
        if (values.MaxEncoderThreads !== undefined) encoderThreadsSpin.value = Number(values.MaxEncoderThreads)
        if (values.CaptureGpuSlots !== undefined) captureGpuSlotsSpin.value = Number(values.CaptureGpuSlots)
        if (values.ImageBufferingThreadCount !== undefined) imageBufferingThreadsSpin.value = Number(values.ImageBufferingThreadCount)
        if (values.ImageSizeWarningPercent !== undefined) imageSizeWarningPercentSpin.value = Number(values.ImageSizeWarningPercent)
        if (values.UseOnlyIframes !== undefined) useOnlyIframesCheck.checked = values.UseOnlyIframes === true || values.UseOnlyIframes === "true" || values.UseOnlyIframes === "1"
        if (values.GopSize !== undefined) gopSizeSpin.value = Number(values.GopSize)
        if (values.ImageErrorBehavior !== undefined) imageErrorBehaviorCombo.currentIndex = Math.max(0, imageErrorBehaviorCombo.behaviorValues.indexOf(values.ImageErrorBehavior))
    }

    component FormLabel: Label {
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        horizontalAlignment: Text.AlignRight
        opacity: 0.86
    }

    FileDialog {
        id: config2DDialog
        parentWindow: root.Window.window
        title: qsTr("Choose mono/2D SGCT configuration")
        currentFolder: app.pathToUrl(app.sliceDataPath("configs"))
        nameFilters: [ qsTr("SGCT JSON configurations (*.json)"), qsTr("All files (*)") ]
        onAccepted: config2DField.text = app.urlToPath(selectedFile)
    }
    FileDialog {
        id: config3DDialog
        parentWindow: root.Window.window
        title: qsTr("Choose stereo/3D SGCT configuration")
        currentFolder: app.pathToUrl(app.sliceDataPath("configs"))
        nameFilters: [ qsTr("SGCT JSON configurations (*.json)"), qsTr("All files (*)") ]
        onAccepted: config3DField.text = app.urlToPath(selectedFile)
    }
    FileDialog {
        id: paramFileDialog
        parentWindow: root.Window.window
        title: qsTr("Choose FFmpeg parameter JSON")
        currentFolder: app.pathToUrl(app.sliceDataPath("parameters"))
        nameFilters: [ qsTr("JSON files (*.json)"), qsTr("All files (*)") ]
        onAccepted: paramFileField.text = app.urlToPath(selectedFile)
    }
    FolderDialog {
        id: firstLeftInputLocationDialog
        parentWindow: root.Window.window
        title: qsTr("Choose first left/input folder")
        currentFolder: app.pathToUrl(firstLeftInputLocationField.text.length > 0 ? firstLeftInputLocationField.text : root.controller.leftInputDialogLocation)
        onAccepted: firstLeftInputLocationField.text = app.urlToPath(selectedFolder)
    }
    FolderDialog {
        id: firstRightInputLocationDialog
        parentWindow: root.Window.window
        title: qsTr("Choose first right-eye folder")
        currentFolder: app.pathToUrl(firstRightInputLocationField.text.length > 0 ? firstRightInputLocationField.text : root.controller.rightInputDialogLocation)
        onAccepted: firstRightInputLocationField.text = app.urlToPath(selectedFolder)
    }
    FolderDialog {
        id: firstOutputDirectoryLocationDialog
        parentWindow: root.Window.window
        title: qsTr("Choose startup output folder")
        currentFolder: app.pathToUrl(firstOutputDirectoryLocationField.text.length > 0 ? firstOutputDirectoryLocationField.text : root.controller.outputDirectoryDialogLocation)
        onAccepted: firstOutputDirectoryLocationField.text = app.urlToPath(selectedFolder)
    }

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.max(0, parent.width - Kirigami.Units.largeSpacing * 2)
            x: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                ComboBox {
                    id: presetFileCombo
                    Layout.fillWidth: true
                    model: root.controller ? root.controller.presetNames : []
                }
                TextField {
                    id: presetNameField
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    placeholderText: qsTr("Preset name")
                    text: presetFileCombo.currentText
                }
                Button {
                    text: qsTr("Load Preset")
                    icon.name: "document-open"
                    icon.color: Kirigami.Theme.textColor
                    enabled: root.controller !== null && presetFileCombo.currentText.length > 0
                    onClicked: {
                        const values = root.controller.loadPresetValues(presetFileCombo.currentText)
                        root.applyValues(values)
                        root.controller.applyPresetValues(values)
                    }
                }
                Button {
                    text: qsTr("Save as Preset")
                    icon.name: "document-save-as"
                    icon.color: Kirigami.Theme.textColor
                    enabled: root.controller !== null && presetNameField.text.trim().length > 0
                    onClicked: {
                        loadRuntimeFields()
                        root.controller.savePresetValues(presetNameField.text, root.currentValues())
                    }
                }
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Startup values")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                TextField {
                    id: config2DField
                    Kirigami.FormData.label: qsTr("Configuration 2D:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse 2D…")
                    icon.name: "document-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: config2DDialog.open()
                }

                TextField {
                    id: config3DField
                    Kirigami.FormData.label: qsTr("Configuration 3D:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse 3D…")
                    icon.name: "document-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: config3DDialog.open()
                }

                TextField {
                    id: firstLeftInputLocationField
                    Kirigami.FormData.label: qsTr("First left/input folder:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse left…")
                    icon.name: "folder-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: firstLeftInputLocationDialog.open()
                }

                TextField {
                    id: firstRightInputLocationField
                    Kirigami.FormData.label: qsTr("First right-eye folder:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse right…")
                    icon.name: "folder-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: firstRightInputLocationDialog.open()
                }

                TextField {
                    id: firstOutputDirectoryLocationField
                    Kirigami.FormData.label: qsTr("Default output folder:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse output…")
                    icon.name: "folder-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: firstOutputDirectoryLocationDialog.open()
                }

                ComboBox {
                    id: formatCombo
                    Kirigami.FormData.label: qsTr("Map onto:")
                    model: [ "Dome", "Sphere EQR", "Sphere EAC", "Plane" ]
                }

                TextField {
                    id: surfaceRadiusField
                    Kirigami.FormData.label: qsTr("Dome/sphere radius (cm):")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                TextField {
                    id: surfaceFovField
                    Kirigami.FormData.label: qsTr("Dome FOV (deg):")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Layer rotation:")
                    SpinBox { id: layerPitchSpin; from: -3600; to: 3600; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Pitch"); ToolTip.visible: hovered }
                    SpinBox { id: layerYawSpin; from: -3600; to: 3600; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Yaw"); ToolTip.visible: hovered }
                    SpinBox { id: layerRollSpin; from: -3600; to: 3600; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Roll"); ToolTip.visible: hovered }
                    Label { text: qsTr("degrees") }
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Plane orientation:")
                    SpinBox { id: planeAzimuthSpin; from: -3600; to: 3600; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Azimuth"); ToolTip.visible: hovered }
                    SpinBox { id: planeElevationSpin; from: -1800; to: 1800; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Elevation"); ToolTip.visible: hovered }
                    SpinBox { id: planeRollSpin; from: -3600; to: 3600; editable: true; textFromValue: root.decimalSpinText; valueFromText: root.decimalSpinValue; ToolTip.text: qsTr("Roll"); ToolTip.visible: hovered }
                    Label { text: qsTr("degrees") }
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Plane position:")
                    SpinBox { id: planeDistanceSpin; from: 1; to: 100000; editable: true; ToolTip.text: qsTr("Distance"); ToolTip.visible: hovered }
                    SpinBox { id: planeHorizontalSpin; from: -100000; to: 100000; editable: true; ToolTip.text: qsTr("Horizontal"); ToolTip.visible: hovered }
                    SpinBox { id: planeVerticalSpin; from: -100000; to: 100000; editable: true; ToolTip.text: qsTr("Vertical"); ToolTip.visible: hovered }
                    Label { text: qsTr("cm") }
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Plane size:")
                    SpinBox { id: planeWidthSpin; from: 0; to: 100000; editable: true; ToolTip.text: qsTr("Width; 0 uses default"); ToolTip.visible: hovered }
                    SpinBox { id: planeHeightSpin; from: 0; to: 100000; editable: true; ToolTip.text: qsTr("Height; 0 uses default"); ToolTip.visible: hovered }
                    Label { text: qsTr("cm") }
                }

                ComboBox {
                    id: codecCombo
                    Kirigami.FormData.label: qsTr("Codec:")
                    model: [ "MPEG-1", "MPEG-2", "MPEG-4", "H264", "H265", "H264 NVENC", "H265 NVENC", "VP8", "VP9", "Hap", "ProRes", "PNG", "JPEG", "TGA" ]
                }

                CheckBox {
                    id: preferNtscOutputFrameRatesCheck
                    Kirigami.FormData.label: qsTr("Output frame rates:")
                    text: qsTr("Prefer NTSC output frame rates")
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Frame rate:")
                    ComboBox {
                        id: frameRatePresetCombo
                        model: [ "30", "60", "29.97", "59.94", qsTr("Custom") ]
                        onActivated: setFrameRatePreset(currentIndex)
                    }
                    SpinBox {
                        id: frameRateNumSpin
                        enabled: frameRatePresetCombo.currentIndex === 4
                        from: 1
                        to: 100000
                    }
                    Label { text: "/"; enabled: frameRatePresetCombo.currentIndex === 4 }
                    SpinBox {
                        id: frameRateDenSpin
                        enabled: frameRatePresetCombo.currentIndex === 4
                        from: 1
                        to: 100000
                    }
                }

                ComboBox {
                    id: softwarePresetCombo
                    Kirigami.FormData.label: qsTr("Software preset:")
                    enabled: isSoftwarePresetCodec(codecCombo.currentText)
                    model: softwarePresetModel()
                }

                ComboBox {
                    id: libxTuneCombo
                    Kirigami.FormData.label: qsTr("Software tune:")
                    enabled: isSoftwarePresetCodec(codecCombo.currentText)
                    model: libxTuneModel()
                }

                ComboBox {
                    id: nvencPresetCombo
                    Kirigami.FormData.label: qsTr("NVENC preset:")
                    enabled: isNvencCodec(codecCombo.currentText)
                    model: nvencPresetModel()
                }

                ComboBox {
                    id: nvencTuneCombo
                    Kirigami.FormData.label: qsTr("NVENC tune:")
                    enabled: isNvencCodec(codecCombo.currentText)
                    model: nvencTuneModel()
                }

                CheckBox {
                    id: nvencHardwareFramesCheck
                    Kirigami.FormData.label: qsTr("NVENC pipeline:")
                    text: qsTr("Use CUDA hardware frames")
                    enabled: isNvencCodec(codecCombo.currentText)
                    ToolTip.text: qsTr("Uploads frames to CUDA hardware frames before NVENC encoding when FFmpeg CUDA support is available.")
                    ToolTip.visible: hovered
                }

                ComboBox {
                    id: bitDepthCombo
                    Kirigami.FormData.label: qsTr("Encoding bit depth:")
                    enabled: supportsEncodingBitDepth(codecCombo.currentText)
                    model: [ "8-bit", "10-bit" ]
                }

                SpinBox {
                    id: crfSpin
                    Kirigami.FormData.label: qsTr("CRF:")
                    enabled: isSoftwarePresetCodec(codecCombo.currentText)
                    from: 0
                    to: 51
                }

                SpinBox {
                    id: cqSpin
                    Kirigami.FormData.label: qsTr("CQ:")
                    enabled: isNvencCodec(codecCombo.currentText)
                    from: 0
                    to: 51
                }

                SpinBox {
                    id: qscaleSpin
                    Kirigami.FormData.label: qsTr("QScale:")
                    enabled: isQScaleCodec(codecCombo.currentText)
                    from: 0
                    to: 51
                }

                TextField {
                    id: paramFileField
                    Kirigami.FormData.label: qsTr("Parameter file:")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Browse parameters…")
                    icon.name: "configure"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: paramFileDialog.open()
                }

                CheckBox {
                    id: preferMatroskaCheck
                    Kirigami.FormData.label: qsTr("Container:")
                    text: qsTr("Prefer Matroska (*.mkv)")
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("GOP Size:")

                    SpinBox {
                        id: gopSizeSpin
                        enabled: !useOnlyIframesCheck.checked
                        from: 1 // Represents 0.1 seconds
                        to: 100 // Represents 10.0 seconds
                        value: SliceSettings.gopSizeSeconds * 10 // Store as integer internally to allow 0.1 steps
                        stepSize: 1 // Represents 0.10 steps
                        editable: true

                        property int decimals: 1

                        // Calculates the real floating-point value from the internal integer
                        property real realValue: value / Math.pow(10, decimals)

                        textFromValue: function(value, locale) {
                            return Number(value / Math.pow(10, decimals)).toLocaleString(locale, 'f', gopSizeSpin.decimals)
                        }

                        valueFromText: function(text, locale) {
                            return Number.fromLocaleString(locale, text) * Math.pow(10, decimals)
                        }

                        onValueModified: {
                            SliceSettings.gopSizeSeconds = realValue
                            if (root.controller) {
                                root.controller.gopSizeSeconds = realValue
                            }
                        }
                        ToolTip.text: qsTr("GOP duration in seconds. High value decreases decoding speed but also lowers file size. Too low value might causes micro stuttering")
                        ToolTip.visible: hovered
                    }
                    Label { text: qsTr("seconds") }

                    CheckBox {
                        id: useOnlyIframesCheck
                        text: qsTr("Use only I-frames")
                        onToggled: {
                            SliceSettings.useOnlyIframes = checked
                            if (root.controller) {
                                root.controller.setUseOnlyIframes(checked)
                            }
                        }
                    }
                }

                SpinBox {
                    id: encoderThreadsSpin
                    Kirigami.FormData.label: qsTr("Encoder threads:")
                    from: 1
                    to: 128
                }

                SpinBox {
                    id: captureGpuSlotsSpin
                    Kirigami.FormData.label: qsTr("Capture GPU Slots:")
                    from: 1
                    to: 128
                }

                SpinBox {
                    id: imageBufferingThreadsSpin
                    Kirigami.FormData.label: qsTr("Loading threads:")
                    from: 1
                    to: 64
                }
                SpinBox {
                    id: imageSizeWarningPercentSpin
                    Kirigami.FormData.label: qsTr("Image size warning %:")
                    from: 0
                    to: 100
                }

                ComboBox {
                    id: imageErrorBehaviorCombo
                    Kirigami.FormData.label: qsTr("Image errors:")
                    property var behaviorValues: [ "Abort", "Pause", "Continue" ]
                    model: [ qsTr("Abort on image error"), qsTr("Pause on image error"), qsTr("Continue on image error") ]
                }

                ComboBox {
                    id: decodingModeCombo
                    Kirigami.FormData.label: qsTr("Video decoding mode:")
                    model: [ qsTr("Software"), qsTr("Hardware"), qsTr("Hybrid") ]
                    ToolTip.text: qsTr("Software uses software rendering (most compatible). Hardware uses hardware decoding. Hybrid uses hardware for left eye and software for right eye.")
                    ToolTip.visible: hovered
                }

                SpinBox {
                    id: inputFrameRateToleranceSpin
                    Kirigami.FormData.label: qsTr("Input FPS tolerance:")
                    from: 1
                    to: 200
                    stepSize: 1
                    editable: true
                    textFromValue: function(value, locale) { return Number(value / 100).toLocaleString(locale, "f", 2) }
                    valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                    ToolTip.text: qsTr("Tolerance for matching detected frame rates to standard presets (30, 60, 29.97, 59.94). Higher values match more frame rates.")
                    ToolTip.visible: hovered
                }
            }
        }
    }

    footer: Pane {
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        implicitHeight: footerLayout.implicitHeight + topPadding + bottomPadding

        RowLayout {
            id: footerLayout

            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: qsTr("Load Startup Values")
                icon.name: "document-open-recent"
                icon.color: Kirigami.Theme.textColor
                onClicked: {
                    SliceSettings.load()
                    root.loadFields()
                }
            }
            Button {
                text: qsTr("Load Default Values")
                icon.name: "edit-undo"
                icon.color: Kirigami.Theme.textColor
                onClicked: root.setDefaults()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Save Startup Values")
                icon.name: "document-save"
                icon.color: Kirigami.Theme.textColor
                onClicked: {
                    root.storeFields()
                    SliceSettings.save()
                    if (root.controller) {
                        root.controller.applyApplicationSettings()
                    }
                }
            }
            Button {
                text: qsTr("Close")
                icon.name: "dialog-close"
                icon.color: Kirigami.Theme.textColor
                onClicked: root.close()
            }
        }
    }
}
