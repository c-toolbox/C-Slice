/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICECONTROLLER_H
#define CSLICE_SLICECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QThread>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QLocalSocket>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

class SliceEstimateThread;
class ImageSequenceIndexThread;
class VideoMetadataThread;

class SliceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString configuration READ configuration WRITE setConfiguration NOTIFY configurationChanged)
    Q_PROPERTY(QString inputType READ inputType WRITE setInputType NOTIFY inputTypeChanged)
    Q_PROPERTY(QString leftInput READ leftInput WRITE setLeftInput NOTIFY leftInputChanged)
    Q_PROPERTY(QString rightInput READ rightInput WRITE setRightInput NOTIFY rightInputChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString outputName READ outputName WRITE setOutputName NOTIFY outputNameChanged)
    Q_PROPERTY(QString leftInputDialogLocation READ leftInputDialogLocation NOTIFY leftInputDialogLocationChanged)
    Q_PROPERTY(QString rightInputDialogLocation READ rightInputDialogLocation NOTIFY rightInputDialogLocationChanged)
    Q_PROPERTY(QString outputDirectoryDialogLocation READ outputDirectoryDialogLocation NOTIFY outputDirectoryDialogLocationChanged)
    Q_PROPERTY(bool stereo READ stereo WRITE setStereo NOTIFY stereoChanged)
    Q_PROPERTY(bool upsideDown READ upsideDown WRITE setUpsideDown NOTIFY upsideDownChanged)
    Q_PROPERTY(bool warping READ warping WRITE setWarping NOTIFY warpingChanged)
    Q_PROPERTY(bool blendMask READ blendMask WRITE setBlendMask NOTIFY blendMaskChanged)
    Q_PROPERTY(int startIndex READ startIndex WRITE setStartIndex NOTIFY startIndexChanged)
    Q_PROPERTY(int stopIndex READ stopIndex WRITE setStopIndex NOTIFY stopIndexChanged)
    Q_PROPERTY(int steps READ steps WRITE setSteps NOTIFY stepsChanged)
    Q_PROPERTY(int outputCount READ outputCount WRITE setOutputCount NOTIFY outputCountChanged)
    Q_PROPERTY(int selectedOutputCount READ selectedOutputCount NOTIFY outputsChanged)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(int maxEncoderThreads READ maxEncoderThreads WRITE setMaxEncoderThreads NOTIFY maxEncoderThreadsChanged)
    Q_PROPERTY(int imageBufferingThreadCount READ imageBufferingThreadCount WRITE setImageBufferingThreadCount NOTIFY imageBufferingThreadCountChanged)
    Q_PROPERTY(int captureGpuSlots READ captureGpuSlots WRITE setCaptureGpuSlots NOTIFY captureGpuSlotsChanged)
    Q_PROPERTY(int imageSizeWarningPercent READ imageSizeWarningPercent WRITE setImageSizeWarningPercent NOTIFY imageSizeWarningPercentChanged)
    Q_PROPERTY(bool runWithoutEncoding READ runWithoutEncoding WRITE setRunWithoutEncoding NOTIFY runWithoutEncodingChanged)
    Q_PROPERTY(bool runWithoutReadback READ runWithoutReadback WRITE setRunWithoutReadback NOTIFY runWithoutReadbackChanged)
    Q_PROPERTY(QString mappingMode READ mappingMode WRITE setMappingMode NOTIFY mappingModeChanged)
    Q_PROPERTY(double surfaceRadius READ surfaceRadius WRITE setSurfaceRadius NOTIFY surfaceRadiusChanged)
    Q_PROPERTY(double surfaceFov READ surfaceFov WRITE setSurfaceFov NOTIFY surfaceFovChanged)
    Q_PROPERTY(QString layerStereoMode READ layerStereoMode WRITE setLayerStereoMode NOTIFY layerSettingsChanged)
    Q_PROPERTY(int layerAlpha READ layerAlpha WRITE setLayerAlpha NOTIFY layerSettingsChanged)
    Q_PROPERTY(bool layerRoiEnabled READ layerRoiEnabled WRITE setLayerRoiEnabled NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerRoiX READ layerRoiX WRITE setLayerRoiX NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerRoiY READ layerRoiY WRITE setLayerRoiY NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerRoiWidth READ layerRoiWidth WRITE setLayerRoiWidth NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerRoiHeight READ layerRoiHeight WRITE setLayerRoiHeight NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerPitch READ layerPitch WRITE setLayerPitch NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerYaw READ layerYaw WRITE setLayerYaw NOTIFY layerSettingsChanged)
    Q_PROPERTY(double layerRoll READ layerRoll WRITE setLayerRoll NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeAzimuth READ planeAzimuth WRITE setPlaneAzimuth NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeElevation READ planeElevation WRITE setPlaneElevation NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeRoll READ planeRoll WRITE setPlaneRoll NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeDistance READ planeDistance WRITE setPlaneDistance NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeHorizontal READ planeHorizontal WRITE setPlaneHorizontal NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeVertical READ planeVertical WRITE setPlaneVertical NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeWidth READ planeWidth WRITE setPlaneWidth NOTIFY layerSettingsChanged)
    Q_PROPERTY(double planeHeight READ planeHeight WRITE setPlaneHeight NOTIFY layerSettingsChanged)
    Q_PROPERTY(int planeAspectRatio READ planeAspectRatio WRITE setPlaneAspectRatio NOTIFY layerSettingsChanged)
    Q_PROPERTY(QString codec READ codec WRITE setCodec NOTIFY codecChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY presetChanged)
    Q_PROPERTY(QString softwarePreset READ softwarePreset WRITE setSoftwarePreset NOTIFY softwarePresetChanged)
    Q_PROPERTY(QString nvencPreset READ nvencPreset WRITE setNvencPreset NOTIFY nvencPresetChanged)
    Q_PROPERTY(QString libxTune READ libxTune WRITE setLibxTune NOTIFY libxTuneChanged)
    Q_PROPERTY(QString nvencTune READ nvencTune WRITE setNvencTune NOTIFY nvencTuneChanged)
    Q_PROPERTY(bool nvencHardwareFrames READ nvencHardwareFrames WRITE setNvencHardwareFrames NOTIFY nvencHardwareFramesChanged)
    Q_PROPERTY(int encodingBitDepth READ encodingBitDepth WRITE setEncodingBitDepth NOTIFY encodingBitDepthChanged)
    Q_PROPERTY(int pixrate READ pixrate WRITE setPixrate NOTIFY pixrateChanged)
    Q_PROPERTY(int constantQuality READ constantQuality WRITE setConstantQuality NOTIFY constantQualityChanged)
    Q_PROPERTY(int crf READ crf WRITE setCrf NOTIFY crfChanged)
    Q_PROPERTY(int cq READ cq WRITE setCq NOTIFY cqChanged)
    Q_PROPERTY(int qscale READ qscale WRITE setQscale NOTIFY qscaleChanged)
    Q_PROPERTY(int frameRateNum READ frameRateNum WRITE setFrameRateNum NOTIFY frameRateNumChanged)
    Q_PROPERTY(int frameRateDen READ frameRateDen WRITE setFrameRateDen NOTIFY frameRateDenChanged)
    Q_PROPERTY(int inputFrameRateNum READ inputFrameRateNum WRITE setInputFrameRateNum NOTIFY inputFrameRateNumChanged)
    Q_PROPERTY(int inputFrameRateDen READ inputFrameRateDen WRITE setInputFrameRateDen NOTIFY inputFrameRateDenChanged)
    Q_PROPERTY(QString parameterFile READ parameterFile WRITE setParameterFile NOTIFY parameterFileChanged)
    Q_PROPERTY(bool useOnlyIframes READ useOnlyIframes WRITE setUseOnlyIframes NOTIFY useOnlyIframesChanged)
    Q_PROPERTY(double gopSizeSeconds READ gopSizeSeconds WRITE setGopSizeSeconds NOTIFY gopSizeSecondsChanged)
    Q_PROPERTY(bool preferNtscOutputFrameRates READ preferNtscOutputFrameRates WRITE setPreferNtscOutputFrameRates NOTIFY preferNtscOutputFrameRatesChanged)
    Q_PROPERTY(bool preferMatroska READ preferMatroska WRITE setPreferMatroska NOTIFY preferMatroskaChanged)
    Q_PROPERTY(QString outputContainerSuffix READ outputContainerSuffix WRITE setOutputContainerSuffix NOTIFY outputContainerSuffixChanged)
    Q_PROPERTY(QStringList outputContainerSuffixes READ outputContainerSuffixes NOTIFY outputContainerSuffixesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool slicePaused READ slicePaused NOTIFY slicePausedChanged)
    Q_PROPERTY(QString imageErrorBehavior READ imageErrorBehavior WRITE setImageErrorBehavior NOTIFY imageErrorBehaviorChanged)
    Q_PROPERTY(QString videoDecodingMode READ videoDecodingMode WRITE setVideoDecodingMode NOTIFY videoDecodingModeChanged)
    Q_PROPERTY(bool audioMuxRunning READ audioMuxRunning NOTIFY audioMuxRunningChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetNamesChanged)
    Q_PROPERTY(QString commandLinePreview READ commandLinePreview NOTIFY commandLinePreviewChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString sequenceStatus READ sequenceStatus NOTIFY sequenceStatusChanged)
    Q_PROPERTY(bool sequenceIndexing READ sequenceIndexing NOTIFY sequenceIndexingChanged)
    Q_PROPERTY(bool hasIndexedRange READ hasIndexedRange NOTIFY indexedRangeChanged)
    Q_PROPERTY(int indexedStartIndex READ indexedStartIndex NOTIFY indexedRangeChanged)
    Q_PROPERTY(int indexedStopIndex READ indexedStopIndex NOTIFY indexedRangeChanged)
    Q_PROPERTY(int sliceProgress READ sliceProgress NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceProcessedFrames READ sliceProcessedFrames NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceLoadedProgress READ sliceLoadedProgress NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceRenderedProgress READ sliceRenderedProgress NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceLoadedFrames READ sliceLoadedFrames NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceRenderedFrames READ sliceRenderedFrames NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceTotalFrames READ sliceTotalFrames NOTIFY sliceProgressChanged)
    Q_PROPERTY(int sliceCurrentFrame READ sliceCurrentFrame NOTIFY sliceProgressChanged)
    Q_PROPERTY(QString sliceElapsedTime READ sliceElapsedTime NOTIFY sliceProgressChanged)
    Q_PROPERTY(QString sliceRemainingTime READ sliceRemainingTime NOTIFY sliceRemainingTimeChanged)
    Q_PROPERTY(QString sliceProgressStatus READ sliceProgressStatus NOTIFY sliceProgressChanged)
    Q_PROPERTY(QString sliceProgressAction READ sliceProgressAction NOTIFY sliceProgressChanged)
    Q_PROPERTY(QString sliceCriticalErrors READ sliceCriticalErrors NOTIFY sliceProgressChanged)
    Q_PROPERTY(QVariantList sliceFailedFiles READ sliceFailedFiles NOTIFY sliceProgressChanged)
    Q_PROPERTY(bool sequencePreviewRight READ sequencePreviewRight WRITE setSequencePreviewRight NOTIFY sequencePreviewChanged)
    Q_PROPERTY(int sequencePreviewFrame READ sequencePreviewFrame WRITE setSequencePreviewFrame NOTIFY sequencePreviewChanged)
    Q_PROPERTY(int sequencePreviewMinimum READ sequencePreviewMinimum NOTIFY sequencePreviewChanged)
    Q_PROPERTY(int sequencePreviewMaximum READ sequencePreviewMaximum NOTIFY sequencePreviewChanged)
    Q_PROPERTY(QString sequencePreviewPath READ sequencePreviewPath NOTIFY sequencePreviewChanged)
    Q_PROPERTY(QString sequencePreviewStatus READ sequencePreviewStatus NOTIFY sequencePreviewChanged)

    // Queue properties (mirroring C-Stitch)
    Q_PROPERTY(bool cjobEnabled READ isCJobEnabled WRITE setCJobEnabled NOTIFY cjobEnabledChanged)
    Q_PROPERTY(int internalQueueSize READ internalQueueSize NOTIFY internalQueueChanged)
    Q_PROPERTY(QStringList internalQueuedJobs READ internalQueuedJobs NOTIFY internalQueueChanged)
    Q_PROPERTY(bool internalQueueRunning READ isInternalQueueRunning WRITE setInternalQueueRunning NOTIFY internalQueueRunningChanged)
    Q_PROPERTY(int runningJobIndex READ runningJobIndex NOTIFY runningJobIndexChanged)
    Q_PROPERTY(bool editingQueuedJob READ isEditingQueuedJob NOTIFY editingQueuedJobChanged)
    Q_PROPERTY(QString editedQueuedJobName READ editedQueuedJobName NOTIFY editingQueuedJobChanged)

public:
    struct JobInfo;

    explicit SliceController(QObject *parent = nullptr);
    ~SliceController() override;

    QString configuration() const;
    void setConfiguration(const QString &configuration);

    QString inputType() const;
    void setInputType(const QString &inputType);

    QString leftInput() const;
    void setLeftInput(const QString &leftInput);

    QString rightInput() const;
    void setRightInput(const QString &rightInput);

    QString outputDirectory() const;
    void setOutputDirectory(const QString &outputDirectory);

    QString outputName() const;
    void setOutputName(const QString &outputName);

    QString leftInputDialogLocation() const;
    QString rightInputDialogLocation() const;
    QString outputDirectoryDialogLocation() const;

    bool stereo() const;
    void setStereo(bool stereo);

    bool upsideDown() const;
    void setUpsideDown(bool upsideDown);

    bool warping() const;
    void setWarping(bool warping);

    bool blendMask() const;
    void setBlendMask(bool blendMask);

    int startIndex() const;
    void setStartIndex(int startIndex);

    int stopIndex() const;
    void setStopIndex(int stopIndex);

    int steps() const;
    void setSteps(int steps);

    int outputCount() const;
    void setOutputCount(int outputCount);
    int selectedOutputCount() const;
    QVariantList outputs() const;

    int maxEncoderThreads() const;
    void setMaxEncoderThreads(int maxEncoderThreads);

    int imageBufferingThreadCount() const;
    void setImageBufferingThreadCount(int imageBufferingThreadCount);

    int captureGpuSlots() const;
    void setCaptureGpuSlots(int captureGpuSlots);

    int imageSizeWarningPercent() const;
    void setImageSizeWarningPercent(int imageSizeWarningPercent);

    bool runWithoutEncoding() const;
    void setRunWithoutEncoding(bool runWithoutEncoding);

    bool runWithoutReadback() const;
    void setRunWithoutReadback(bool runWithoutReadback);


    QString mappingMode() const;
    void setMappingMode(const QString &mappingMode);

    double surfaceRadius() const;
    void setSurfaceRadius(double surfaceRadius);

    double surfaceFov() const;
    void setSurfaceFov(double surfaceFov);

    QString layerStereoMode() const;
    void setLayerStereoMode(const QString &layerStereoMode);

    int layerAlpha() const;
    void setLayerAlpha(int layerAlpha);

    bool layerRoiEnabled() const;
    void setLayerRoiEnabled(bool layerRoiEnabled);
    double layerRoiX() const;
    void setLayerRoiX(double layerRoiX);
    double layerRoiY() const;
    void setLayerRoiY(double layerRoiY);
    double layerRoiWidth() const;
    void setLayerRoiWidth(double layerRoiWidth);
    double layerRoiHeight() const;
    void setLayerRoiHeight(double layerRoiHeight);

    double layerPitch() const;
    void setLayerPitch(double layerPitch);
    double layerYaw() const;
    void setLayerYaw(double layerYaw);
    double layerRoll() const;
    void setLayerRoll(double layerRoll);

    double planeAzimuth() const;
    void setPlaneAzimuth(double planeAzimuth);
    double planeElevation() const;
    void setPlaneElevation(double planeElevation);
    double planeRoll() const;
    void setPlaneRoll(double planeRoll);
    double planeDistance() const;
    void setPlaneDistance(double planeDistance);
    double planeHorizontal() const;
    void setPlaneHorizontal(double planeHorizontal);
    double planeVertical() const;
    void setPlaneVertical(double planeVertical);
    double planeWidth() const;
    void setPlaneWidth(double planeWidth);
    double planeHeight() const;
    void setPlaneHeight(double planeHeight);
    int planeAspectRatio() const;
    void setPlaneAspectRatio(int planeAspectRatio);

    QString codec() const;
    void setCodec(const QString &codec);

    QString preset() const;
    void setPreset(const QString &preset);

    QString softwarePreset() const;
    void setSoftwarePreset(const QString &softwarePreset);

    QString nvencPreset() const;
    void setNvencPreset(const QString &nvencPreset);

    QString libxTune() const;
    void setLibxTune(const QString &libxTune);

    QString nvencTune() const;
    void setNvencTune(const QString &nvencTune);

    bool nvencHardwareFrames() const;
    void setNvencHardwareFrames(bool nvencHardwareFrames);

    int encodingBitDepth() const;
    void setEncodingBitDepth(int encodingBitDepth);

    int pixrate() const;
    void setPixrate(int pixrate);

    int constantQuality() const;
    void setConstantQuality(int constantQuality);

    int crf() const;
    void setCrf(int crf);

    int cq() const;
    void setCq(int cq);

    int qscale() const;
    void setQscale(int qscale);

    int frameRateNum() const;
    void setFrameRateNum(int frameRateNum);

    int frameRateDen() const;
    void setFrameRateDen(int frameRateDen);

    int inputFrameRateNum() const;
    void setInputFrameRateNum(int inputFrameRateNum);
    int inputFrameRateDen() const;
    void setInputFrameRateDen(int inputFrameRateDen);

    QString parameterFile() const;
    void setParameterFile(const QString &parameterFile);

    bool useOnlyIframes() const;
    void setUseOnlyIframes(bool useOnlyIframes);
    double gopSizeSeconds() const;
    void setGopSizeSeconds(double gopSizeSeconds);

    bool preferNtscOutputFrameRates() const;
    void setPreferNtscOutputFrameRates(bool preferNtscOutputFrameRates);

    bool preferMatroska() const;
    void setPreferMatroska(bool preferMatroska);

    QString outputContainerSuffix() const;
    void setOutputContainerSuffix(const QString &outputContainerSuffix);
    QStringList outputContainerSuffixes() const;

    bool running() const;
    bool slicePaused() const;
    QString imageErrorBehavior() const;
    void setImageErrorBehavior(const QString &imageErrorBehavior);
    QString videoDecodingMode() const { return m_videoDecodingMode; }
    void setVideoDecodingMode(const QString &videoDecodingMode);
    bool audioMuxRunning() const;
    QStringList presetNames() const;
    QString commandLinePreview() const;
    QString logText() const;
    QString sequenceStatus() const;
    bool sequenceIndexing() const;
    bool hasIndexedRange() const;
    int indexedStartIndex() const;
    int indexedStopIndex() const;
    int sliceProgress() const;
    int sliceProcessedFrames() const;
    int sliceLoadedProgress() const;
    int sliceRenderedProgress() const;
    int sliceLoadedFrames() const;
    int sliceRenderedFrames() const;
    int sliceTotalFrames() const;
    int sliceCurrentFrame() const;
    QString sliceElapsedTime() const;
    QString sliceRemainingTime() const;
    QString sliceProgressStatus() const;
    QString sliceProgressAction() const;
    QString sliceCriticalErrors() const;
    QVariantList sliceFailedFiles() const;
    bool sequencePreviewRight() const;
    void setSequencePreviewRight(bool sequencePreviewRight);
    int sequencePreviewFrame() const;
    void setSequencePreviewFrame(int sequencePreviewFrame);
    int sequencePreviewMinimum() const;
    int sequencePreviewMaximum() const;
    QString sequencePreviewPath() const;
    QString sequencePreviewStatus() const;

    Q_INVOKABLE void launchSlice();
    Q_INVOKABLE void verifySliceInputs();
    Q_INVOKABLE void pauseSlice();
    Q_INVOKABLE void resumeSlice();
    Q_INVOKABLE void abortSlice();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void openOutputDirectory();
    Q_INVOKABLE void openFailedFile(const QString &path);
    Q_INVOKABLE void applyApplicationSettings();
    Q_INVOKABLE void loadApplicationSettings();
    Q_INVOKABLE void loadSystemApplicationSettings();
    Q_INVOKABLE void saveApplicationSettings();
    Q_INVOKABLE QVariantMap loadPresetValues(const QString &presetName) const;
    Q_INVOKABLE bool savePresetValues(const QString &presetName, const QVariantMap &values);
    Q_INVOKABLE void applyPresetValues(const QVariantMap &values);
    Q_INVOKABLE void setOutputEnabled(int index, bool enabled);
    Q_INVOKABLE void setAllOutputsEnabled(bool enabled);
    Q_INVOKABLE QString buildAudioMuxCommandLine(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels) const;
    Q_INVOKABLE void muxAudio(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels);
    Q_INVOKABLE void abortAudioMux();
    Q_INVOKABLE void sequencePreviewPrevious();
    Q_INVOKABLE void sequencePreviewNext();
    Q_INVOKABLE void forceRefreshImageSequenceStatus();
    Q_INVOKABLE void resetStartIndexToIndexedRange();
    Q_INVOKABLE void resetStopIndexToIndexedRange();

    // Queue management methods (mirroring C-Stitch)
    Q_INVOKABLE void queueJob();  // Add current job to internal queue
    Q_INVOKABLE void reorderInternalQueue(int start, int destination);  // Reorder items in internal queue via drag-drop
    Q_INVOKABLE void removeJobFromInternalQueue(int index);  // Remove job at specified index from internal queue
    Q_INVOKABLE void launchJobAtQueueIndex(int index);  // Launch specific queued job
    Q_INVOKABLE void launchNextFromQueue();  // Launch next queued job
    Q_INVOKABLE bool beginQueuedJobEdit(int index);
    Q_INVOKABLE bool saveQueuedJobEdit();
    Q_INVOKABLE bool cancelQueuedJobEdit();
    Q_INVOKABLE bool saveInternalQueue(const QString &filePath);
    Q_INVOKABLE bool loadInternalQueue(const QString &filePath);
    Q_INVOKABLE bool isCJobEnabled() const;
    Q_INVOKABLE void setCJobEnabled(bool enabled);
    Q_INVOKABLE bool isCJobConnected() const;
    Q_INVOKABLE bool submitToCJob(const QString &jobId);  // Submit to C-Job server

Q_SIGNALS:
    void configurationChanged();
    void inputTypeChanged();
    void leftInputChanged();
    void rightInputChanged();
    void outputDirectoryChanged();
    void outputNameChanged();
    void leftInputDialogLocationChanged();
    void rightInputDialogLocationChanged();
    void outputDirectoryDialogLocationChanged();
    void stereoChanged();
    void upsideDownChanged();
    void warpingChanged();
    void blendMaskChanged();
    void startIndexChanged();
    void stopIndexChanged();
    void stepsChanged();
    void outputCountChanged();
    void outputsChanged();
    void maxEncoderThreadsChanged();
    void imageBufferingThreadCountChanged();
    void captureGpuSlotsChanged();
    void imageSizeWarningPercentChanged();
    void runWithoutEncodingChanged();
    void runWithoutReadbackChanged();
    void mappingModeChanged();
    void surfaceRadiusChanged();
    void surfaceFovChanged();
    void layerSettingsChanged();
    void codecChanged();
    void presetChanged();
    void softwarePresetChanged();
    void nvencPresetChanged();
    void libxTuneChanged();
    void nvencTuneChanged();
    void nvencHardwareFramesChanged();
    void encodingBitDepthChanged();
    void pixrateChanged();
    void constantQualityChanged();
    void crfChanged();
    void cqChanged();
    void qscaleChanged();
    void useOnlyIframesChanged();
    void gopSizeSecondsChanged();
    void frameRateNumChanged();
    void frameRateDenChanged();
    void inputFrameRateNumChanged();
    void inputFrameRateDenChanged();
    void parameterFileChanged();
    void preferNtscOutputFrameRatesChanged();
    void preferMatroskaChanged();
    void outputContainerSuffixChanged();
    void outputContainerSuffixesChanged();
    void runningChanged();
    void slicePausedChanged();
    void imageErrorBehaviorChanged();
    void videoDecodingModeChanged();
    void audioMuxRunningChanged();
    void presetNamesChanged();
    void commandLinePreviewChanged();
    void logTextChanged();
    void sequenceStatusChanged();
    void sequenceIndexingChanged();
    void indexedRangeChanged();
    void sliceProgressChanged();
    void sliceRemainingTimeChanged();
    void sequencePreviewChanged();

    // Queue signals (mirroring C-Stitch)
    void cjobEnabledChanged();
    void internalQueueChanged();
    void internalQueueRunningChanged();
    void runningJobIndexChanged();
    void editingQueuedJobChanged();

private:
    QStringList buildArguments(bool verifyOnly = false) const;
    QStringList buildAudioMuxArguments(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels, QString *error) const;
    QString outputSuffix() const;
    void appendLog(const QString &line);
    void updateOutputCountFromConfiguration();
    void updateConfigFeatureOptions();
    void setOutputNames(const QStringList &names);
    void setOutputCountAndNamesSilent(int count, const QStringList &names);
    bool outputEnabled(int index) const;
    QString outputIdentifier(int index) const;
    void setLeftInputDialogLocation(const QString &path, bool persist = true);
    void setRightInputDialogLocation(const QString &path, bool persist = true);
    void setOutputDirectoryDialogLocation(const QString &path, bool persist = true);
    void resetDialogLocationsFromSettings();
    void setSequenceStatus(const QString &status);
    void setSequenceIndexing(bool sequenceIndexing);
    void refreshImageSequenceStatus(bool adoptDetectedRange, bool scanLeft, bool scanRight);
    void refreshVideoMetadataStatus(bool adoptDetectedRange, bool probeLeft, bool probeRight);
    void applyImageSequenceStatus(int requestId,
        bool adoptDetectedRange,
        bool scanLeft,
        bool scanRight,
        const QString &leftPath,
        const QString &rightPath,
        const QVariantMap &leftSequence,
        const QVariantMap &rightSequence);
    void updateSequenceStatusFromScans(bool adoptDetectedRange,
        const QString &leftPath,
        const QString &rightPath,
        const QVariantMap &leftSequence,
        const QVariantMap &rightSequence);
    void handleProcessStdout(const QString &chunk);
    void handleProcessStderr(const QString &chunk);
    void handleProcessOutputLine(const QString &line);
    void setSliceError(const QString &message);
    void addFailedFile(const QString &path, const QString &message);
    static QString failedFilePathFromMessage(const QString &message);
    void resetSliceProgress();
    void startSliceRemainingTime();
    void resetSliceRemainingTime();
    void setSliceRemainingTime(const QString &remainingTime);
    void updateSliceProgress(int loadedFrames, int renderedFrames, int totalFrames, int currentFrame, const QString &elapsedTime, const QString &status);
    int expectedSliceFrameCount() const;
    void notifyCommandChanged();
    void notifyAllSettingsChanged();
    static QString quoteArgument(const QString &argument);
    JobInfo captureCurrentJobSettings() const;
    void applyJobSettings(const JobInfo &info);
    static QJsonObject jobInfoToJson(const JobInfo &info);
    static bool jobInfoFromJson(const QJsonObject &json, JobInfo *info, QString *error);
    bool canPersistInternalQueue(QString *error) const;

    QString m_configuration;
    QString m_inputType = QStringLiteral("Image sequence");
    QString m_leftInput;
    QString m_rightInput;
    QString m_outputDirectory;
    QString m_outputName = QStringLiteral("slice");
    QString m_leftInputDialogLocation;
    QString m_rightInputDialogLocation;
    QString m_outputDirectoryDialogLocation;
    bool m_stereo = false;
    bool m_upsideDown = false;
    bool m_warping = false;
    bool m_blendMask = false;
    int m_startIndex = 0;
    int m_stopIndex = 0;
    int m_steps = 1;
    int m_outputCount = 1;
    QStringList m_outputNames = { QStringLiteral("Output 0") };
    QList<bool> m_outputEnabled = { true };
    int m_maxEncoderThreads = 16;
    int m_imageBufferingThreadCount = 16;
    int m_captureGpuSlots = 4;
    int m_imageSizeWarningPercent = 25;
    bool m_runWithoutEncoding = false;
    bool m_runWithoutReadback = false;
    bool m_useOnlyIframes = false;
    double m_gopSizeSeconds = 1.0;
    QString m_mappingMode = QStringLiteral("Dome");
    double m_surfaceRadius = 740.0;
    double m_surfaceFov = 165.0;
    QString m_layerStereoMode = QStringLiteral("2D (mono)");
    int m_layerAlpha = 100;
    bool m_layerRoiEnabled = false;
    double m_layerRoiX = 0.0;
    double m_layerRoiY = 0.0;
    double m_layerRoiWidth = 1.0;
    double m_layerRoiHeight = 1.0;
    double m_layerPitch = 0.0;
    double m_layerYaw = 0.0;
    double m_layerRoll = 0.0;
    double m_planeAzimuth = 0.0;
    double m_planeElevation = 0.0;
    double m_planeRoll = 0.0;
    double m_planeDistance = 740.0;
    double m_planeHorizontal = 0.0;
    double m_planeVertical = 0.0;
    double m_planeWidth = 0.0;
    double m_planeHeight = 0.0;
    int m_planeAspectRatio = 1;
    QString m_codec = QStringLiteral("H264");
    QString m_softwarePreset = QStringLiteral("veryslow");
    QString m_nvencPreset = QStringLiteral("High quality (P6)");
    QString m_libxTune = QStringLiteral("fastdecode");
    QString m_nvencTune = QStringLiteral("High quality");
    bool m_nvencHardwareFrames = false;
    int m_encodingBitDepth = 8;
    int m_pixrate = 6;
    int m_constantQuality = 23;
    int m_crf = 28;
    int m_cq = 22;
    int m_qscale = 12;
    int m_frameRateNum = 1;
    int m_frameRateDen = 30;
    int m_inputFrameRateNum = 30;
    int m_inputFrameRateDen = 1;
    QString m_parameterFile;
    bool m_preferNtscOutputFrameRates = false;
    bool m_preferMatroska = false;
    bool m_slicePaused = false;
    QString m_imageErrorBehavior = QStringLiteral("Continue");
    QString m_videoDecodingMode = QStringLiteral("Software");
    QProcess m_process;
    QProcess m_audioMuxProcess;
    SliceEstimateThread *m_sliceEstimateThread = nullptr;
    ImageSequenceIndexThread *m_imageSequenceIndexThread = nullptr;
    VideoMetadataThread *m_videoMetadataThread = nullptr;
    QString m_processStdoutBuffer;
    QString m_processStderrBuffer;
    QString m_lastSliceError;
    bool m_verifyingSlice = false;
    QString m_logText;
    QString m_sequenceStatus;
    bool m_sequenceIndexing = false;
    int m_sequenceIndexRequestId = 0;
    bool m_hasIndexedRange = false;
    int m_indexedStartIndex = 0;
    int m_indexedStopIndex = 0;
    int m_indexedRightVideoLastFrame = -1; // -1 means not probed / not applicable
    QString m_lastIndexedLeftPath;
    QString m_lastIndexedRightPath;
    QVariantMap m_lastIndexedLeftSequence;
    QVariantMap m_lastIndexedRightSequence;
    int m_sliceLoadedProgress = 0;
    int m_sliceRenderedProgress = 0;
    int m_sliceLoadedFrames = 0;
    int m_sliceRenderedFrames = 0;
    int m_sliceTotalFrames = 0;
    int m_sliceCurrentFrame = 0;
    QString m_sliceElapsedTime;
    QString m_sliceRemainingTime;
    QString m_sliceProgressStatus;
    QString m_sliceProgressAction = QStringLiteral("Encoded");
    QStringList m_sliceCriticalErrors;
    QVariantList m_sliceFailedFiles;
    bool m_sequencePreviewRight = false;
    int m_sequencePreviewFrame = 0;

    // Queue structures (mirroring C-Stitch)
public:
    struct JobInfo {
        QString jobId;
        QString instanceId;
        QString status;
        int progressCompleted = 0;
        int progressTotal = 0;

        // Job parameters (copied from controller when queued)
        QString configuration;
        QString inputType = QStringLiteral("Image sequence");
        QString leftInput;
        QString rightInput;
        QString outputDirectory;
        QString outputName;
        bool stereo = false;
        bool upsideDown = false;
        bool warping = false;
        bool blendMask = false;
        int startIndex = 0;
        int stopIndex = 0;
        int steps = 1;
        int outputCount = 1;
        QStringList outputNames;
        QList<bool> outputEnabled;
        int maxEncoderThreads = 16;
        int imageBufferingThreadCount = 16;
        int captureGpuSlots = 4;
        int imageSizeWarningPercent = 25;
        bool runWithoutEncoding = false;
        bool runWithoutReadback = false;
        QString mappingMode;
        double surfaceRadius = 740.0;
        double surfaceFov = 165.0;
        QString layerStereoMode;
        int layerAlpha = 100;
        bool layerRoiEnabled = false;
        double layerRoiX = 0.0;
        double layerRoiY = 0.0;
        double layerRoiWidth = 1.0;
        double layerRoiHeight = 1.0;
        double layerPitch = 0.0;
        double layerYaw = 0.0;
        double layerRoll = 0.0;
        double planeAzimuth = 0.0;
        double planeElevation = 0.0;
        double planeRoll = 0.0;
        double planeDistance = 740.0;
        double planeHorizontal = 0.0;
        double planeVertical = 0.0;
        double planeWidth = 0.0;
        double planeHeight = 0.0;
        int planeAspectRatio = 1;
        QString codec;
        QString preset;
        QString softwarePreset;
        QString nvencPreset;
        QString libxTune;
        QString nvencTune;
        bool nvencHardwareFrames = false;
        int encodingBitDepth = 8;
        int pixrate = 6;
        int constantQuality = 23;
        int crf = 28;
        int cq = 22;
        int qscale = 12;
        int frameRateNum = 1;
        int frameRateDen = 30;
        int inputFrameRateNum = 30;
        int inputFrameRateDen = 1;
        QString parameterFile;
        bool useOnlyIframes = false;
        double gopSizeSeconds = 1.0;
        bool preferNtscOutputFrameRates = false;
        bool preferMatroska = false;
        QString outputContainerSuffix;
        QString imageErrorBehavior = QStringLiteral("Continue");
        QString videoDecodingMode = QStringLiteral("Software");
        QString jobName;  // Pre-generated descriptive name for display
    };

private:
    // C-Job client methods (mirroring C-Stitch)
    void connectToCJobServer();
    void sendRegisterInstance();
    void disconnectFromCJobServer();
    void sendJsonToCJob(const QJsonObject &json);
    void onCJobReadyRead();
    void processCJobMessage(const QString &message);

    // Queue state
    bool m_cjobEnabled = false;
    QLocalSocket *m_cjobSocket = nullptr;
    QString m_cjobBuffer;
    QString m_currentCJobJobId;  // Current job ID being processed from C-Job
    QVector<JobInfo> m_queue;  // Internal job queue
    QMap<QString, JobInfo> m_storedJobs;  // Full job parameters keyed by job ID

    Q_INVOKABLE JobInfo retrieveStoredJob(const QString &jobId) const { return m_storedJobs.value(jobId, JobInfo{}); }

    int internalQueueSize() const { return m_queue.size(); }
    QStringList internalQueuedJobs() const;
    bool isInternalQueueRunning() const { return m_internalQueueRunning; }
    void setInternalQueueRunning(bool running);
    int runningJobIndex() const { return m_runningJobIndex; }
    bool isEditingQueuedJob() const { return !m_editedQueuedJobId.isEmpty(); }
    QString editedQueuedJobName() const { return m_editedQueuedJobName; }

    bool m_internalQueueRunning = false;
    int m_runningJobIndex = -1;
    QString m_editedQueuedJobId;
    QString m_editedQueuedJobName;
    JobInfo m_preEditJobSettings;

    QString generateJobName() const;

    // Process finish handler for queue auto-advance
    void handleProcessFinish();
};

#endif // CSLICE_SLICECONTROLLER_H
