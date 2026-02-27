/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QMessageBox>
#include <QTimer>
#include <QRandomGenerator>
#include <QPointer>
#include <QInputDialog>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHttpMultiPart>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QMenu>
#include <QStandardPaths>
#include <QDir>
#include <QPixmap>
#include <QApplication>
#include <QClipboard>
#include <QTabWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QDesktopServices>
#include <QToolButton>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>
#include <KisViewManager.h>
#include <kis_canvas2.h>
#include <kis_signal_auto_connection.h>
#include <kis_image_manager.h>
#include <kis_selection.h>
#include <kis_types.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorConversionTransformation.h>

// Minimal ComfyUI default workflow (text2img). Node keys "3".."9" as in ComfyUI basic_api_example.
static const char defaultWorkflow[] = R"({
 "3": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["5", 0], "model": ["4", 0], "negative": ["7", 0], "positive": ["6", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "EmptyLatentImage", "inputs": {"batch_size": 1, "height": 512, "width": 512}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "7": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "8": {"class_type": "VAEDecode", "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
 "9": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI", "images": ["8", 0]}}
})";

// Minimal inpainting workflow: LoadImage (1=image, 2=mask), Checkpoint (4), CLIP (5,6), VAEEncodeForInpaint (7), KSampler (8), VAEDecode (9), SaveImage (10).
static const char inpaintingWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "LoadImage", "inputs": {"image": "MASK_PLACEHOLDER"}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "PROMPT_PLACEHOLDER"}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "NEGATIVE_PLACEHOLDER"}},
 "7": {"class_type": "VAEEncodeForInpaint", "inputs": {"grow_mask_by": 6, "mask": ["2", 1], "pixels": ["1", 0], "vae": ["4", 2]}},
 "8": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["7", 0], "model": ["4", 0], "negative": ["6", 0], "positive": ["5", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["4", 2]}},
 "10": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_region", "images": ["9", 0]}}
})";

// Upscale: LoadImage (1), ImageScale (2), SaveImage (3). Uses ComfyUI core ImageScale (width/height).
static const char upscaleWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "ImageScale", "inputs": {"height": 1024, "image": ["1", 0], "upscale_method": "lanczos", "width": 1024}},
 "3": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_upscale", "images": ["2", 0]}}
})";

// img2img for Live: LoadImage (1), VAEEncode (2), Checkpoint (3), CLIP (4,5), KSampler (6), VAEDecode (7), SaveImage (8)
static const char img2imgWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "VAEEncode", "inputs": {"pixels": ["1", 0], "vae": ["3", 2]}},
 "3": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"}},
 "4": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["3", 1], "text": "PROMPT_PLACEHOLDER"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["3", 1], "text": "NEGATIVE_PLACEHOLDER"}},
 "6": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 0.75, "latent_image": ["2", 0], "model": ["3", 0], "negative": ["5", 0], "positive": ["4", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["3", 2]}},
 "8": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_live", "images": ["7", 0]}}
})";

namespace
{
QString historyCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QString path = base + QStringLiteral("/krita/comfyui_remote");
    QDir().mkpath(path);
    return path;
}
} // namespace

struct ComfyUIRemoteDock::Private
{
    QPointer<KisViewManager> viewManager;
    QPointer<KisCanvas2> canvas;

    QLineEdit *editServerUrl = nullptr;
    QComboBox *comboCheckpoint = nullptr;
    QPushButton *btnRefreshCheckpoints = nullptr;
    QComboBox *comboPreset = nullptr;
    QComboBox *comboSizePreset = nullptr;
    QPushButton *btnSaveAsPreset = nullptr;
    QPushButton *btnDeletePreset = nullptr;
    QPlainTextEdit *editPrompt = nullptr;
    QPlainTextEdit *editNegative = nullptr;
    QSpinBox *spinWidth = nullptr;
    QSpinBox *spinHeight = nullptr;
    QSpinBox *spinSteps = nullptr;
    QDoubleSpinBox *spinCfg = nullptr;
    QComboBox *comboSampler = nullptr;
    QPushButton *btnRefreshSamplers = nullptr;
    QSpinBox *spinSeed = nullptr;
    QCheckBox *checkFixedSeed = nullptr;
    QPushButton *btnRandomSeed = nullptr;
    QProgressBar *progressBar = nullptr;
    QComboBox *comboWorkspace = nullptr;
    QComboBox *comboQueueMode = nullptr;
    QSpinBox *spinBatchCount = nullptr;
    QLabel *labelQueueCount = nullptr;
    QPushButton *btnTest = nullptr;
    QPushButton *btnGenerate = nullptr;
    QPushButton *btnCancelQueue = nullptr;
    QPushButton *btnInpaint = nullptr;
    QPushButton *btnUpscale = nullptr;
    QSpinBox *spinAnimationFrames = nullptr;
    QPushButton *btnGenerateAnimation = nullptr;
    QCheckBox *checkUseReferenceImage = nullptr;
    QCheckBox *checkLiveMode = nullptr;
    QTimer *liveTimer = nullptr;
    QString liveUploadedImageName;
    QString livePromptId;
    int livePollCount = 0;
    static const int liveMaxPollCount = 120;
    QTimer *livePollTimer = nullptr;
    QLabel *labelStatus = nullptr;
    QListWidget *listHistory = nullptr;
    QPushButton *btnHistoryReRun = nullptr;
    QPushButton *btnHistoryApply = nullptr;
    QPlainTextEdit *editCustomWorkflow = nullptr;
    QPushButton *btnLoadWorkflow = nullptr;

    struct RegionEntry {
        QString name;
        QString prompt;
        QString maskSource; // "selection" or "layer:LayerName"
    };
    QList<RegionEntry> regionEntries;
    QListWidget *listRegions = nullptr;
    QPushButton *btnAddRegion = nullptr;
    QPushButton *btnRemoveRegion = nullptr;
    QPushButton *btnMoveRegionUp = nullptr;
    QPushButton *btnMoveRegionDown = nullptr;
    QPushButton *btnEditRegion = nullptr;
    QPushButton *btnGenerateRegions = nullptr;

    void refreshRegionsList();
    void loadRegionsFromConfig();
    void saveRegionsToConfig();

    struct HistoryEntry {
        QString prompt, negative, checkpoint;
        int width = 512, height = 512;
        int steps = 20;
        double cfg = 8.0;
        QString samplerName;
        qint64 seed = 0;
        QString resultImagePath;  // path to cached thumbnail/result image
    };
    QList<HistoryEntry> historyEntries;
    QMap<QString, HistoryEntry> pendingHistoryByPromptId;
    static const int maxHistoryEntries = 20;

    static const int builtinPresetCount = 5; // None, Portrait, Landscape, Anime, Realistic

    void refreshHistoryList();

    QNetworkAccessManager *nam = nullptr;
    QTimer *pollTimer = nullptr;
    QString currentPromptId;      // the one we're currently polling
    QStringList jobQueue;         // prompt_ids waiting (first is running)
    int pollCount = 0;
    static const int maxPollCount = 300; // 5 min at 1s
    KisSignalAutoConnectionsStore connections;

    // Batch submit state
    QStringList batchCollectIds;
    int batchSubmitIndex = 0;
    int batchCountTarget = 0;
    int batchQueueMode = 0;
    QUrl batchBaseUrl;
    bool batchUseCustomWorkflow = false;
    QJsonObject batchCustomWorkflow;

    // Region generation state (used during slotGenerateRegions async chain)
    QImage regionCurrentImage;
    int regionIndex = 0;
    QString regionUploadedImageName;
    QString regionUploadedImageSubfolder;
    QString regionPromptId;
    QString regionMaskUploadedName;
    QString regionMaskUploadedSubfolder;
    int regionPollCount = 0;
    static const int regionMaxPollCount = 300;

    // One-click inpaint state
    QString inpaintUploadedImageName;
    QString inpaintUploadedImageSubfolder;
    QString inpaintUploadedMaskName;
    QString inpaintUploadedMaskSubfolder;
    QString inpaintPromptId;
    QImage inpaintCurrentImage;
    int inpaintPollCount = 0;
    static const int inpaintMaxPollCount = 300;
    QTimer *inpaintPollTimer = nullptr;

    // Upscale state
    QString upscaleUploadedImageName;
    QString upscalePromptId;
    int upscalePollCount = 0;
    static const int upscaleMaxPollCount = 300;
    QTimer *upscalePollTimer = nullptr;

    // Settings dialog (connection & workflow configuration)
    QPointer<QDialog> settingsDialog;

    // Queue popup button (jobs, batch, seed, enqueue mode, cancel)
    QToolButton *btnQueuePopup = nullptr;
    QSlider *sliderResolutionMultiplier = nullptr;
    QLabel *labelResolutionMultiplier = nullptr;
    double resolutionMultiplier = 1.0;

    // Group pointers for workspace visibility toggling
    QGroupBox *genGroupBox = nullptr;
    QGroupBox *histGroupBox = nullptr;
    QGroupBox *regionsGroupBox = nullptr;
};

ComfyUIRemoteDock::ComfyUIRemoteDock()
    : QDockWidget()
    , m_d(new Private)
{
    m_d->nam = new QNetworkAccessManager(this);
    m_d->pollTimer = new QTimer(this);
    m_d->pollTimer->setSingleShot(true);
    m_d->inpaintPollTimer = new QTimer(this);
    m_d->inpaintPollTimer->setSingleShot(true);
    connect(m_d->inpaintPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotInpaintPoll);
    m_d->upscalePollTimer = new QTimer(this);
    m_d->upscalePollTimer->setSingleShot(true);
    connect(m_d->upscalePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotUpscalePoll);
    m_d->liveTimer = new QTimer(this);
    m_d->liveTimer->setSingleShot(true);
    connect(m_d->liveTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLiveTick);
    m_d->livePollTimer = new QTimer(this);
    m_d->livePollTimer->setSingleShot(true);
    connect(m_d->livePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLivePoll);
    connect(m_d->pollTimer, &QTimer::timeout, this, [this]() {
        if (m_d->currentPromptId.isEmpty()) return;
        QUrl base(m_d->editServerUrl->text().trimmed());
        if (!base.isValid()) return;
        base.setPath(base.path() + "/history/" + m_d->currentPromptId);
        QNetworkRequest req(base);
        QNetworkReply *reply = m_d->nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_d->labelStatus->setText(i18n("History error: %1", reply->errorString()));
                m_d->progressBar->setValue(0);
                m_d->currentPromptId.clear();
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                } else {
                    m_d->btnGenerate->setEnabled(true);
                }
                updateQueueStatus();
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject hist = doc.object().value(m_d->currentPromptId).toObject();
            QJsonObject outputs = hist.value("outputs").toObject();
            if (outputs.isEmpty()) {
                m_d->pollCount++;
                if (m_d->pollCount >= Private::maxPollCount) {
                    m_d->labelStatus->setText(i18n("Generation timed out."));
                    m_d->progressBar->setValue(0);
                    m_d->currentPromptId.clear();
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                updateQueueStatus();
                m_d->pollTimer->start(1000);
                return;
            }
            // Find SaveImage output (node "9" in default workflow)
            QString filename, subfolder;
            for (const QString &nodeId : outputs.keys()) {
                QJsonObject nodeOut = outputs.value(nodeId).toObject();
                QJsonArray images = nodeOut.value("images").toArray();
                if (!images.isEmpty()) {
                    QJsonObject img = images.at(0).toObject();
                    filename = img.value("filename").toString();
                    subfolder = img.value("subfolder").toString();
                    break;
                }
            }
            if (filename.isEmpty()) {
                m_d->labelStatus->setText(i18n("No image in output."));
                m_d->progressBar->setValue(0);
                m_d->currentPromptId.clear();
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                } else {
                    m_d->btnGenerate->setEnabled(true);
                }
                updateQueueStatus();
                return;
            }
            QString completedId = m_d->currentPromptId;
            m_d->currentPromptId.clear();
            if (m_d->jobQueue.isEmpty()) {
                m_d->btnGenerate->setEnabled(true);
            }
            QUrl baseUrl(m_d->editServerUrl->text().trimmed());
            QUrl viewUrl(baseUrl);
            QString path = viewUrl.path();
            if (!path.endsWith('/')) path += '/';
            path += "view";
            viewUrl.setPath(path);
            QUrlQuery q;
            q.addQueryItem("filename", filename);
            if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
            viewUrl.setQuery(q);
            QNetworkRequest req(viewUrl);
            QNetworkReply *getReply = m_d->nam->get(req);
            connect(getReply, &QNetworkReply::finished, this, [this, getReply, completedId]() {
                getReply->deleteLater();
                if (getReply->error() != QNetworkReply::NoError) {
                    m_d->labelStatus->setText(i18n("Download error: %1", getReply->errorString()));
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                QByteArray data = getReply->readAll();
                QString suffix = "png";
                if (data.startsWith("\x89PNG")) suffix = "png";
                else if (data.startsWith("\xff\xd8")) suffix = "jpg";
                QTemporaryFile tmp;
                tmp.setFileTemplate(tmp.fileTemplate() + "." + suffix);
                if (!tmp.open()) {
                    m_d->labelStatus->setText(i18n("Could not create temp file."));
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                tmp.write(data);
                tmp.close();
                QString cachePath;
                if (m_d->pendingHistoryByPromptId.contains(completedId)) {
                    cachePath = historyCacheDir() + QStringLiteral("/") + completedId + QStringLiteral(".png");
                    if (QFile::exists(cachePath)) QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath))
                        m_d->pendingHistoryByPromptId[completedId].resultImagePath = cachePath;
                }
                if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
                    m_d->labelStatus->setText(i18n("No document open."));
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (n > 0) {
                    if (m_d->canvas) m_d->canvas->updateCanvas();
                }
                m_d->progressBar->setValue(100);
                if (m_d->pendingHistoryByPromptId.contains(completedId)) {
                    m_d->historyEntries.prepend(m_d->pendingHistoryByPromptId.take(completedId));
                    while (m_d->historyEntries.size() > Private::maxHistoryEntries)
                        m_d->historyEntries.removeLast();
                    refreshHistoryList();
                }
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                }
                updateQueueStatus();
            });
        });
    });

    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *scrollContent = new QWidget();
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);

    QGroupBox *connGroup = new QGroupBox(i18n("Connection"));
    QVBoxLayout *connLayout = new QVBoxLayout(connGroup);
    m_d->editServerUrl = new QLineEdit();
    m_d->editServerUrl->setPlaceholderText(i18n("e.g. http://192.168.1.10:8188"));
    m_d->editServerUrl->setClearButtonEnabled(true);

    m_d->comboCheckpoint = new QComboBox();
    m_d->comboCheckpoint->setEditable(true);
    m_d->comboCheckpoint->setInsertPolicy(QComboBox::NoInsert);
    m_d->comboCheckpoint->addItem("v1-5-pruned-emaonly.safetensors");
    m_d->btnRefreshCheckpoints = new QPushButton(i18n("Refresh"));
    m_d->btnRefreshCheckpoints->setToolTip(i18n("Load checkpoint list from server"));
    connect(m_d->btnRefreshCheckpoints, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRefreshCheckpoints);
    QHBoxLayout *checkpointRow = new QHBoxLayout();
    checkpointRow->addWidget(new QLabel(i18n("Checkpoint:")));
    checkpointRow->addWidget(m_d->comboCheckpoint, 1);
    checkpointRow->addWidget(m_d->btnRefreshCheckpoints);
    connLayout->addLayout(checkpointRow);

    m_d->comboPreset = new QComboBox();
    m_d->comboPreset->addItem(i18n("None"));
    m_d->comboPreset->addItem(i18n("Portrait"));
    m_d->comboPreset->addItem(i18n("Landscape"));
    m_d->comboPreset->addItem(i18n("Anime"));
    m_d->comboPreset->addItem(i18n("Realistic"));
    m_d->comboPreset->setItemIcon(0, KisIconUtils::loadIcon("document-new"));
    m_d->comboPreset->setItemIcon(1, KisIconUtils::loadIcon("user-identity"));
    m_d->comboPreset->setItemIcon(2, KisIconUtils::loadIcon("view-landscape"));
    m_d->comboPreset->setItemIcon(3, KisIconUtils::loadIcon("draw-brush"));
    m_d->comboPreset->setItemIcon(4, KisIconUtils::loadIcon("camera-photo"));
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    QStringList customNames = cfg.readEntry("PresetNames", QStringList());
    for (const QString &name : customNames) {
        if (!name.isEmpty()) m_d->comboPreset->addItem(name);
    }
    connect(m_d->comboPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComfyUIRemoteDock::slotPresetChanged);
    m_d->btnSaveAsPreset = new QPushButton(i18n("Save as preset"));
    m_d->btnDeletePreset = new QPushButton(i18n("Delete preset"));
    connect(m_d->btnSaveAsPreset, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotSaveAsPreset);
    connect(m_d->btnDeletePreset, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotDeletePreset);
    QHBoxLayout *presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(i18n("Style:")));
    presetRow->addWidget(m_d->comboPreset, 2);

    // Simple quality selector inspired by krita-ai ("Fast" vs "Quality")
    QComboBox *comboQuality = new QComboBox();
    comboQuality->addItem(i18n("Fast"));
    comboQuality->addItem(i18n("Quality"));
    comboQuality->setCurrentIndex(1);
    comboQuality->setToolTip(i18n("Fast: fewer steps, quicker results. Quality: more steps, better details."));
    connect(comboQuality, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_d->spinSteps) return;
        if (idx == 0) { // Fast
            int v = m_d->spinSteps->value();
            m_d->spinSteps->setValue(qMax(1, v / 2));
        } else { // Quality
            if (m_d->spinSteps->value() < 20)
                m_d->spinSteps->setValue(20);
        }
    });
    presetRow->addWidget(new QLabel(i18n("Quality:")));
    presetRow->addWidget(comboQuality, 1);

    presetRow->addWidget(m_d->btnSaveAsPreset);
    presetRow->addWidget(m_d->btnDeletePreset);
    connLayout->addLayout(presetRow);
    m_d->btnDeletePreset->setEnabled(false);

    // Widgets for advanced configuration (shown in settings dialog instead of main dock)
    m_d->checkUseReferenceImage = new QCheckBox(i18n("Use current layer as reference (replace REFERENCE_IMAGE in workflow)"));
    m_d->checkUseReferenceImage->setToolTip(i18n("Export current layer, upload to server, and replace REFERENCE_IMAGE in your workflow JSON with the uploaded filename."));
    m_d->editCustomWorkflow = new QPlainTextEdit();
    m_d->editCustomWorkflow->setPlaceholderText(i18n("Paste ComfyUI API workflow from File → Export (API), or leave empty for default text2img."));
    m_d->editCustomWorkflow->setMaximumHeight(80);
    m_d->btnLoadWorkflow = new QPushButton(i18n("Load from file…"));
    connect(m_d->btnLoadWorkflow, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotLoadWorkflowFromFile);

    // Open settings dialog (connection + workflow) instead of exposing config directly
    QPushButton *btnSettings = new QPushButton(i18n("Settings…"));
    btnSettings->setIcon(KisIconUtils::loadIcon("configure"));
    connect(btnSettings, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotConfigureHelp);
    connLayout->addWidget(btnSettings);
    scrollLayout->addWidget(connGroup);

    QGroupBox *genGroup = new QGroupBox(i18n("Generate"));
    m_d->genGroupBox = genGroup;
    QVBoxLayout *genLayout = new QVBoxLayout(genGroup);

    // Workspace selector similar to krita-ai (Generate / Upscale / Live / Animation / Regions)
    m_d->comboWorkspace = new QComboBox();
    m_d->comboWorkspace->addItem(i18n("Generate"));
    m_d->comboWorkspace->addItem(i18n("Upscale"));
    m_d->comboWorkspace->addItem(i18n("Live"));
    m_d->comboWorkspace->addItem(i18n("Animation"));
    m_d->comboWorkspace->addItem(i18n("Regions"));
    m_d->comboWorkspace->setToolTip(i18n("Choose workspace: image generation, upscaling, live painting, animation, or region-based prompts."));
    connect(m_d->comboWorkspace, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        // Toggle visibility of mode-specific controls
        if (!m_d->genGroupBox) return;
        const bool isGenerate = (idx == 0);
        const bool isUpscale = (idx == 1);
        const bool isLive = (idx == 2);
        const bool isAnimation = (idx == 3);
        const bool isRegions = (idx == 4);

        if (m_d->btnGenerate) m_d->btnGenerate->setVisible(isGenerate || isRegions);
        if (m_d->btnInpaint) m_d->btnInpaint->setVisible(isGenerate || isRegions);
        if (m_d->btnUpscale) m_d->btnUpscale->setVisible(isUpscale);
        if (m_d->btnGenerateAnimation) m_d->btnGenerateAnimation->setVisible(isAnimation);
        if (m_d->checkLiveMode) m_d->checkLiveMode->setVisible(isLive);
        if (m_d->regionsGroupBox) m_d->regionsGroupBox->setVisible(isRegions);
    });
    genLayout->addWidget(m_d->comboWorkspace);
    genLayout->addWidget(new QLabel(i18n("Prompt:")));
    m_d->editPrompt = new QPlainTextEdit();
    m_d->editPrompt->setPlaceholderText(i18n("Describe the content you want to see, or leave empty."));
    m_d->editPrompt->setToolTip(i18n("Tip: (word) for emphasis, [word] to reduce strength. Use commas to separate concepts."));
    m_d->editPrompt->setMaximumHeight(60);
    genLayout->addWidget(m_d->editPrompt);

    genLayout->addWidget(new QLabel(i18n("Negative prompt:")));
    m_d->editNegative = new QPlainTextEdit();
    m_d->editNegative->setPlaceholderText(i18n("Describe content you want to avoid."));
    m_d->editNegative->setMaximumHeight(40);
    genLayout->addWidget(m_d->editNegative);

    QHBoxLayout *stepsCfgRow = new QHBoxLayout();
    m_d->spinSteps = new QSpinBox();
    m_d->spinSteps->setRange(1, 150);
    m_d->spinSteps->setValue(20);
    m_d->spinSteps->setToolTip(i18n("Sampler steps"));
    m_d->spinCfg = new QDoubleSpinBox();
    m_d->spinCfg->setRange(1.0, 30.0);
    m_d->spinCfg->setValue(8.0);
    m_d->spinCfg->setDecimals(1);
    m_d->spinCfg->setToolTip(i18n("CFG scale (guidance strength)"));
    m_d->comboSampler = new QComboBox();
    m_d->comboSampler->setEditable(true);
    m_d->comboSampler->addItem("euler");
    m_d->comboSampler->addItem("euler_ancestral");
    m_d->comboSampler->addItem("dpmpp_2m");
    m_d->comboSampler->addItem("dpmpp_2s_ancestral");
    m_d->comboSampler->addItem("heun");
    m_d->comboSampler->addItem("dpm_2");
    m_d->comboSampler->addItem("dpm_2_ancestral");
    m_d->btnRefreshSamplers = new QPushButton(i18n("Refresh"));
    m_d->btnRefreshSamplers->setToolTip(i18n("Load sampler list from server"));
    connect(m_d->btnRefreshSamplers, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRefreshSamplers);
    stepsCfgRow->addWidget(new QLabel(i18n("Steps:")));
    stepsCfgRow->addWidget(m_d->spinSteps);
    stepsCfgRow->addWidget(new QLabel(i18n("CFG:")));
    stepsCfgRow->addWidget(m_d->spinCfg);
    stepsCfgRow->addWidget(new QLabel(i18n("Sampler:")));
    stepsCfgRow->addWidget(m_d->comboSampler, 1);
    stepsCfgRow->addWidget(m_d->btnRefreshSamplers);
    genLayout->addLayout(stepsCfgRow);

    m_d->checkFixedSeed = new QCheckBox(i18n("Fixed seed"));
    m_d->spinSeed = new QSpinBox();
    m_d->spinSeed->setRange(0, 2147483647);
    m_d->spinSeed->setValue(0);
    m_d->btnRandomSeed = new QPushButton(i18n("Random"));
    m_d->btnRandomSeed->setIcon(KisIconUtils::loadIcon("random"));
    connect(m_d->btnRandomSeed, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRandomSeed);

    m_d->comboSizePreset = new QComboBox();
    m_d->comboSizePreset->addItem(i18n("512×512 (default)"), QSize(512, 512));
    m_d->comboSizePreset->addItem(i18n("768×768"), QSize(768, 768));
    m_d->comboSizePreset->addItem(i18n("1024×1024"), QSize(1024, 1024));
    m_d->comboSizePreset->addItem(i18n("2048×2048 (4k)"), QSize(2048, 2048));
    m_d->comboSizePreset->addItem(i18n("4096×4096 (8k)"), QSize(4096, 4096));
    connect(m_d->comboSizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        QSize s = m_d->comboSizePreset->itemData(idx).toSize();
        if (s.isValid()) {
            m_d->spinWidth->setValue(s.width());
            m_d->spinHeight->setValue(s.height());
        }
    });
    genLayout->addWidget(new QLabel(i18n("Size preset:")));
    genLayout->addWidget(m_d->comboSizePreset);
    QHBoxLayout *sizeLayout = new QHBoxLayout();
    m_d->spinWidth = new QSpinBox();
    m_d->spinWidth->setRange(64, 8192);
    m_d->spinWidth->setValue(512);
    m_d->spinHeight = new QSpinBox();
    m_d->spinHeight->setRange(64, 8192);
    m_d->spinHeight->setValue(512);
    sizeLayout->addWidget(new QLabel(i18n("Width:")));
    sizeLayout->addWidget(m_d->spinWidth);
    sizeLayout->addWidget(new QLabel(i18n("Height:")));
    sizeLayout->addWidget(m_d->spinHeight);
    genLayout->addLayout(sizeLayout);

    m_d->btnGenerate = new QPushButton(i18n("Generate"));
    m_d->btnGenerate->setIcon(KisIconUtils::loadIcon("run-build"));
    connect(m_d->btnGenerate, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerate);
    genLayout->addWidget(m_d->btnGenerate);

    m_d->btnInpaint = new QPushButton(i18n("Inpaint (selection)"));
    m_d->btnInpaint->setToolTip(i18n("Generate in selection."));
    connect(m_d->btnInpaint, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotInpaint);
    genLayout->addWidget(m_d->btnInpaint);

    m_d->btnUpscale = new QPushButton(i18n("Upscale 2×"));
    m_d->btnUpscale->setToolTip(i18n("Upscale canvas 2× using ComfyUI ImageScale (lanczos)."));
    connect(m_d->btnUpscale, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotUpscale);
    genLayout->addWidget(m_d->btnUpscale);

    QHBoxLayout *animRow = new QHBoxLayout();
    m_d->spinAnimationFrames = new QSpinBox();
    m_d->spinAnimationFrames->setRange(2, 16);
    m_d->spinAnimationFrames->setValue(4);
    m_d->spinAnimationFrames->setToolTip(i18n("Number of frames (seeds: seed, seed+1, …)"));
    m_d->btnGenerateAnimation = new QPushButton(i18n("Generate animation"));
    m_d->btnGenerateAnimation->setToolTip(i18n("Generate N images with sequential seeds as new layers."));
    connect(m_d->btnGenerateAnimation, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerateAnimation);
    animRow->addWidget(new QLabel(i18n("Frames:")));
    animRow->addWidget(m_d->spinAnimationFrames);
    animRow->addWidget(m_d->btnGenerateAnimation);
    genLayout->addLayout(animRow);

    m_d->checkLiveMode = new QCheckBox(i18n("Live (periodic img2img from canvas)"));
    m_d->checkLiveMode->setToolTip(i18n("Every 30 s: export canvas, run img2img, apply result as new layer. Stop by unchecking."));
    connect(m_d->checkLiveMode, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_d->liveTimer->start(30000);
        else { m_d->liveTimer->stop(); m_d->livePollTimer->stop(); }
    });
    genLayout->addWidget(m_d->checkLiveMode);

    // Queue popup (similar to krita-ai Queue button)
    m_d->labelQueueCount = new QLabel(i18n("Queue: 0"));
    m_d->comboQueueMode = new QComboBox();
    m_d->comboQueueMode->addItem(i18n("Back"), 0);
    m_d->comboQueueMode->addItem(i18n("Front"), 1);
    m_d->comboQueueMode->addItem(i18n("Replace"), 2);
    m_d->comboQueueMode->setToolTip(i18n("Back: add after current jobs. Front: new jobs run first. Replace: clear queue and add."));
    m_d->spinBatchCount = new QSpinBox();
    m_d->spinBatchCount->setRange(1, 10);
    m_d->spinBatchCount->setValue(1);
    m_d->spinBatchCount->setToolTip(i18n("Number of images to generate per click"));

    m_d->btnQueuePopup = new QToolButton();
    m_d->btnQueuePopup->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_d->btnQueuePopup->setPopupMode(QToolButton::InstantPopup);
    m_d->btnQueuePopup->setIcon(KisIconUtils::loadIcon("view-refresh"));
    m_d->btnQueuePopup->setText(QStringLiteral("0 "));
    m_d->btnQueuePopup->setToolTip(i18n("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));

    QMenu *queueMenu = new QMenu(m_d->btnQueuePopup);
    QWidget *queueWidget = new QWidget(queueMenu);
    QVBoxLayout *queueLayout = new QVBoxLayout(queueWidget);
    queueLayout->setContentsMargins(8, 8, 8, 8);

    QHBoxLayout *countsLayout = new QHBoxLayout();
    countsLayout->addWidget(new QLabel(i18n("Jobs:"), queueWidget));
    countsLayout->addWidget(m_d->labelQueueCount, 1);
    queueLayout->addLayout(countsLayout);

    QHBoxLayout *batchLayout = new QHBoxLayout();
    batchLayout->addWidget(new QLabel(i18n("Batch:"), queueWidget));
    batchLayout->addWidget(m_d->spinBatchCount, 1);
    queueLayout->addLayout(batchLayout);

    // Resolution multiplier (similar to krita-ai)
    m_d->sliderResolutionMultiplier = new QSlider(Qt::Horizontal, queueWidget);
    m_d->sliderResolutionMultiplier->setRange(3, 15); // 0.3x .. 1.5x
    m_d->sliderResolutionMultiplier->setValue(10);    // 1.0x
    m_d->labelResolutionMultiplier = new QLabel(i18n("1.0×"), queueWidget);
    m_d->labelResolutionMultiplier->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_d->sliderResolutionMultiplier, &QSlider::valueChanged, this, [this](int v) {
        m_d->resolutionMultiplier = qMax(0.3, v / 10.0);
        m_d->labelResolutionMultiplier->setText(QString::number(m_d->resolutionMultiplier, 'f', 1) + QLatin1String("×"));
    });
    QHBoxLayout *resLayout = new QHBoxLayout();
    resLayout->addWidget(new QLabel(i18n("Resolution:"), queueWidget));
    resLayout->addWidget(m_d->sliderResolutionMultiplier, 1);
    resLayout->addWidget(m_d->labelResolutionMultiplier);
    queueLayout->addLayout(resLayout);

    QHBoxLayout *seedLayout = new QHBoxLayout();
    seedLayout->addWidget(m_d->checkFixedSeed);
    seedLayout->addWidget(m_d->spinSeed, 1);
    seedLayout->addWidget(m_d->btnRandomSeed);
    queueLayout->addLayout(seedLayout);

    QHBoxLayout *modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(i18n("Enqueue:"), queueWidget));
    modeLayout->addWidget(m_d->comboQueueMode, 1);
    queueLayout->addLayout(modeLayout);

    QPushButton *popupCancel = new QPushButton(i18n("Cancel queue"), queueWidget);
    popupCancel->setIcon(KisIconUtils::loadIcon("dialog-cancel"));
    connect(popupCancel, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotCancelQueue);
    queueLayout->addWidget(popupCancel);

    QWidgetAction *queueAction = new QWidgetAction(queueMenu);
    queueAction->setDefaultWidget(queueWidget);
    queueMenu->addAction(queueAction);
    m_d->btnQueuePopup->setMenu(queueMenu);

    QHBoxLayout *queueRow = new QHBoxLayout();
    queueRow->addWidget(m_d->btnQueuePopup);
    queueRow->addStretch();
    genLayout->addLayout(queueRow);

    m_d->btnCancelQueue = popupCancel;
    m_d->btnCancelQueue->setEnabled(false);

    m_d->progressBar = new QProgressBar();
    m_d->progressBar->setMinimum(0);
    m_d->progressBar->setMaximum(100);
    m_d->progressBar->setValue(0);
    m_d->progressBar->setTextVisible(false);
    m_d->progressBar->setFixedHeight(6);
    genLayout->addWidget(m_d->progressBar);
    scrollLayout->addWidget(genGroup);

    QGroupBox *histGroup = new QGroupBox(i18n("History"));
    QVBoxLayout *histLayout = new QVBoxLayout(histGroup);
    histLayout->addWidget(new QLabel(i18n("Results (double-click Apply, right-click for menu):")));
    m_d->listHistory = new QListWidget();
    m_d->listHistory->setMaximumHeight(140);
    m_d->listHistory->setViewMode(QListWidget::IconMode);
    m_d->listHistory->setIconSize(QSize(64, 64));
    m_d->listHistory->setSpacing(4);
    m_d->listHistory->setMovement(QListWidget::Static);
    m_d->listHistory->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_d->listHistory, &QListWidget::customContextMenuRequested, this, &ComfyUIRemoteDock::slotHistoryContextMenu);
    connect(m_d->listHistory, &QListWidget::itemSelectionChanged, this, &ComfyUIRemoteDock::slotHistoryItemSelected);
    connect(m_d->listHistory, &QListWidget::doubleClicked, this, &ComfyUIRemoteDock::slotHistoryApply);
    histLayout->addWidget(m_d->listHistory);
    QHBoxLayout *historyBtns = new QHBoxLayout();
    m_d->btnHistoryReRun = new QPushButton(i18n("Re-run"));
    m_d->btnHistoryApply = new QPushButton(i18n("Apply"));
    connect(m_d->btnHistoryReRun, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotHistoryReRun);
    connect(m_d->btnHistoryApply, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotHistoryApply);
    historyBtns->addWidget(m_d->btnHistoryReRun);
    historyBtns->addWidget(m_d->btnHistoryApply);
    histLayout->addLayout(historyBtns);
    m_d->btnHistoryReRun->setEnabled(false);
    m_d->btnHistoryApply->setEnabled(false);
    scrollLayout->addWidget(histGroup);

    QGroupBox *regGroup = new QGroupBox(i18n("Regions"));
    QVBoxLayout *regLayout = new QVBoxLayout(regGroup);
    regLayout->addWidget(new QLabel(i18n("Different prompt per area (layer or selection):")));
    m_d->listRegions = new QListWidget();
    m_d->listRegions->setMaximumHeight(80);
    connect(m_d->listRegions, &QListWidget::doubleClicked, this, &ComfyUIRemoteDock::slotEditRegion);
    regLayout->addWidget(m_d->listRegions);
    QHBoxLayout *regionBtns = new QHBoxLayout();
    m_d->btnAddRegion = new QPushButton(i18n("Add"));
    m_d->btnRemoveRegion = new QPushButton(i18n("Remove"));
    m_d->btnMoveRegionUp = new QPushButton(i18n("Up"));
    m_d->btnMoveRegionDown = new QPushButton(i18n("Down"));
    m_d->btnEditRegion = new QPushButton(i18n("Edit"));
    m_d->btnGenerateRegions = new QPushButton(i18n("Generate regions"));
    connect(m_d->btnAddRegion, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotAddRegion);
    connect(m_d->btnRemoveRegion, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRemoveRegion);
    connect(m_d->btnMoveRegionUp, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotMoveRegionUp);
    connect(m_d->btnMoveRegionDown, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotMoveRegionDown);
    connect(m_d->btnEditRegion, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotEditRegion);
    connect(m_d->btnGenerateRegions, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerateRegions);
    regionBtns->addWidget(m_d->btnAddRegion);
    regionBtns->addWidget(m_d->btnRemoveRegion);
    regionBtns->addWidget(m_d->btnMoveRegionUp);
    regionBtns->addWidget(m_d->btnMoveRegionDown);
    regionBtns->addWidget(m_d->btnEditRegion);
    regionBtns->addWidget(m_d->btnGenerateRegions);
    regLayout->addLayout(regionBtns);
    scrollLayout->addWidget(regGroup);

    scrollLayout->addStretch();
    scroll->setWidget(scrollContent);
    layout->addWidget(scroll);
    m_d->labelStatus = new QLabel(i18n("Use Settings to configure server URL and advanced options."));
    m_d->labelStatus->setWordWrap(true);
    layout->addWidget(m_d->labelStatus);
    setWidget(widget);
    setWindowTitle(i18n("AI Image (ComfyUI)"));
    setEnabled(false);

    loadRegionsFromConfig();
    refreshRegionsList();
}

ComfyUIRemoteDock::~ComfyUIRemoteDock()
{
}

void ComfyUIRemoteDock::setViewManager(KisViewManager *viewManager)
{
    m_d->connections.clear();
    m_d->viewManager = viewManager;
}

void ComfyUIRemoteDock::setCanvas(KoCanvasBase *canvas)
{
    KisCanvas2 *c = dynamic_cast<KisCanvas2 *>(canvas);
    m_d->canvas = c;
    setEnabled(canvas != nullptr);
}

void ComfyUIRemoteDock::unsetCanvas()
{
    setCanvas(nullptr);
}

void ComfyUIRemoteDock::slotPresetChanged(int index)
{
    m_d->btnDeletePreset->setEnabled(index >= Private::builtinPresetCount);
    if (index <= 0) return; // None
    if (index < Private::builtinPresetCount) {
        switch (index) {
        case 1: // Portrait
            m_d->editPrompt->setPlainText("portrait, face, detailed skin, soft lighting");
            m_d->editNegative->setPlainText("blurry, deformed");
            m_d->spinWidth->setValue(512);
            m_d->spinHeight->setValue(768);
            break;
        case 2: // Landscape
            m_d->editPrompt->setPlainText("landscape, scenery, detailed environment, atmosphere");
            m_d->editNegative->setPlainText("blurry, text");
            m_d->spinWidth->setValue(768);
            m_d->spinHeight->setValue(512);
            break;
        case 3: // Anime
            m_d->editPrompt->setPlainText("anime style, vibrant colors, clean lines");
            m_d->editNegative->setPlainText("realistic, photo");
            m_d->spinWidth->setValue(512);
            m_d->spinHeight->setValue(512);
            break;
        case 4: // Realistic
            m_d->editPrompt->setPlainText("photorealistic, 8k, detailed, high quality");
            m_d->editNegative->setPlainText("cartoon, anime, painting");
            m_d->spinWidth->setValue(512);
            m_d->spinHeight->setValue(512);
            break;
        default:
            break;
        }
        return;
    }
    // Custom preset: load from config
    QString name = m_d->comboPreset->itemText(index);
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    m_d->editPrompt->setPlainText(cfg.readEntry("Prompt", ""));
    m_d->editNegative->setPlainText(cfg.readEntry("Negative", ""));
    m_d->spinWidth->setValue(cfg.readEntry("Width", 512));
    m_d->spinHeight->setValue(cfg.readEntry("Height", 512));
    m_d->spinSteps->setValue(cfg.readEntry("Steps", 20));
    m_d->spinCfg->setValue(cfg.readEntry("Cfg", 8.0));
    m_d->comboSampler->setCurrentText(cfg.readEntry("Sampler", "euler"));
    QString ckpt = cfg.readEntry("Checkpoint", "");
    if (!ckpt.isEmpty()) {
        int i = m_d->comboCheckpoint->findText(ckpt);
        if (i >= 0) m_d->comboCheckpoint->setCurrentIndex(i);
        else m_d->comboCheckpoint->setCurrentText(ckpt);
    }
}

void ComfyUIRemoteDock::slotSaveAsPreset()
{
    QString name = QInputDialog::getText(this, i18n("Save preset"), i18n("Preset name:"), QLineEdit::Normal, QString());
    if (name.trimmed().isEmpty()) return;
    name = name.trimmed();
    KConfigGroup mainCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    QStringList names = mainCfg.readEntry("PresetNames", QStringList());
    if (!names.contains(name)) names << name;
    mainCfg.writeEntry("PresetNames", names);
    KConfigGroup presetCfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    presetCfg.writeEntry("Prompt", m_d->editPrompt->toPlainText());
    presetCfg.writeEntry("Negative", m_d->editNegative->toPlainText());
    presetCfg.writeEntry("Width", m_d->spinWidth->value());
    presetCfg.writeEntry("Height", m_d->spinHeight->value());
    presetCfg.writeEntry("Steps", m_d->spinSteps->value());
    presetCfg.writeEntry("Cfg", m_d->spinCfg->value());
    presetCfg.writeEntry("Sampler", m_d->comboSampler->currentText());
    presetCfg.writeEntry("Checkpoint", m_d->comboCheckpoint->currentText());
    mainCfg.config()->sync();
    if (m_d->comboPreset->findText(name) < 0)
        m_d->comboPreset->addItem(name);
    m_d->comboPreset->setCurrentText(name);
    m_d->labelStatus->setText(i18n("Saved preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotDeletePreset()
{
    int idx = m_d->comboPreset->currentIndex();
    if (idx < Private::builtinPresetCount) return;
    QString name = m_d->comboPreset->currentText();
    KConfigGroup mainCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    QStringList names = mainCfg.readEntry("PresetNames", QStringList());
    names.removeAll(name);
    mainCfg.writeEntry("PresetNames", names);
    KSharedConfig::openConfig()->deleteGroup("ComfyUIRemote_Preset_" + name);
    mainCfg.config()->sync();
    m_d->comboPreset->removeItem(idx);
    m_d->comboPreset->setCurrentIndex(0);
    m_d->labelStatus->setText(i18n("Deleted preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotLoadWorkflowFromFile()
{
    QString path = QFileDialog::getOpenFileName(this, i18n("Load workflow"), QString(), i18n("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Load workflow"), i18n("Could not open file: %1", f.errorString()));
        return;
    }
    m_d->editCustomWorkflow->setPlainText(QString::fromUtf8(f.readAll()));
    m_d->labelStatus->setText(i18n("Loaded workflow from file."));
}

void ComfyUIRemoteDock::slotRefreshSamplers()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL first."));
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    m_d->labelStatus->setText(i18n("Loading samplers…"));
    m_d->btnRefreshSamplers->setEnabled(false);
    QNetworkRequest req(url);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnRefreshSamplers->setEnabled(true);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Failed to load samplers: %1", reply->errorString()));
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QJsonObject nodeInfo = root.value("KSampler").toObject();
        QJsonObject input = nodeInfo.value("input").toObject();
        QJsonObject required = input.value("required").toObject();
        QJsonValue samplerVal = required.value("sampler_name");
        QStringList names;
        if (samplerVal.isArray()) {
            QJsonArray arr = samplerVal.toArray();
            if (!arr.isEmpty() && arr.at(0).isArray()) {
                for (const QJsonValue &v : arr.at(0).toArray())
                    names << v.toString();
            }
        }
        if (!names.isEmpty()) {
            QString current = m_d->comboSampler->currentText();
            m_d->comboSampler->clear();
            m_d->comboSampler->addItems(names);
            int idx = m_d->comboSampler->findText(current);
            if (idx >= 0) m_d->comboSampler->setCurrentIndex(idx);
            else m_d->comboSampler->setCurrentIndex(0);
            m_d->labelStatus->setText(i18n("Loaded %1 samplers.", names.size()));
        } else {
            m_d->labelStatus->setText(i18n("No sampler list in server response."));
        }
    });
}

void ComfyUIRemoteDock::slotRandomSeed()
{
    m_d->spinSeed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(2147483647)));
}

void ComfyUIRemoteDock::slotRefreshCheckpoints()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL first."));
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    m_d->labelStatus->setText(i18n("Loading checkpoints…"));
    m_d->btnRefreshCheckpoints->setEnabled(false);
    QNetworkRequest req(url);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnRefreshCheckpoints->setEnabled(true);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Failed to load checkpoints: %1", reply->errorString()));
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QJsonObject nodeInfo = root.value("CheckpointLoaderSimple").toObject();
        QJsonObject input = nodeInfo.value("input").toObject();
        QJsonObject required = input.value("required").toObject();
        QJsonValue ckptVal = required.value("ckpt_name");
        QStringList names;
        if (ckptVal.isArray()) {
            QJsonArray arr = ckptVal.toArray();
            if (!arr.isEmpty() && arr.at(0).isArray()) {
                for (const QJsonValue &v : arr.at(0).toArray())
                    names << v.toString();
            }
        }
        m_d->comboCheckpoint->clear();
        if (names.isEmpty()) {
            m_d->comboCheckpoint->addItem("v1-5-pruned-emaonly.safetensors");
            m_d->labelStatus->setText(i18n("No checkpoint list in server response (use custom name)."));
        } else {
            m_d->comboCheckpoint->addItems(names);
            m_d->labelStatus->setText(i18n("Loaded %1 checkpoints.", names.size()));
        }
        m_d->comboCheckpoint->setCurrentIndex(0);
    });
}

void ComfyUIRemoteDock::slotTestConnection()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL."));
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    // ComfyUI GET /system_stats returns server info; use it to test connection
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/system_stats");
    else if (!path.endsWith('/')) url.setPath(path + "/system_stats");
    else url.setPath(path + "system_stats");
    m_d->labelStatus->setText(i18n("Connecting…"));
    m_d->btnTest->setEnabled(false);
    QNetworkRequest req(url);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnTest->setEnabled(true);
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            m_d->labelStatus->setText(i18n("Connected to ComfyUI."));
        } else {
            m_d->labelStatus->setText(i18n("Connection failed: %1", reply->errorString()));
        }
    });
}

void ComfyUIRemoteDock::slotGenerate()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL."));
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->labelStatus->setText(i18n("Open a document first."));
        return;
    }

    QString customJson = m_d->editCustomWorkflow->toPlainText().trimmed();
    if (!customJson.isEmpty() && m_d->checkUseReferenceImage->isChecked()) {
        KisImageSP image = m_d->viewManager->image();
        QImage refImg = getCanvasAsQImage(image);
        if (refImg.isNull()) {
            m_d->labelStatus->setText(i18n("Could not export canvas for reference."));
            return;
        }
        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + ".png");
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            m_d->labelStatus->setText(i18n("Could not save temp image."));
            return;
        }
        m_d->labelStatus->setText(i18n("Uploading reference image…"));
        m_d->btnGenerate->setEnabled(false);
        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
        else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
        else uploadUrl.setPath(up + "upload/image");
        tmp->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"image\"; filename=\"reference.png\""));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp, urlStr, baseUrl]() {
            replyUp->deleteLater();
            if (replyUp->error() != QNetworkReply::NoError) {
                m_d->labelStatus->setText(i18n("Upload error: %1", replyUp->errorString()));
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value("name").toString();
            if (refName.isEmpty()) {
                m_d->labelStatus->setText(i18n("Server did not return image name."));
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString workflowText = m_d->editCustomWorkflow->toPlainText().replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QJsonObject workflow = QJsonDocument::fromJson(workflowText.toUtf8(), &err).object();
            if (err.error != QJsonParseError::NoError) {
                m_d->labelStatus->setText(i18n("Workflow JSON error after reference replace."));
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            int batchCount = m_d->spinBatchCount->value();
            int queueMode = m_d->comboQueueMode->currentData().toInt();
            if (queueMode == 2) {
                m_d->pollTimer->stop();
                for (const QString &id : m_d->jobQueue) m_d->pendingHistoryByPromptId.remove(id);
                if (!m_d->currentPromptId.isEmpty()) m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
                m_d->jobQueue.clear();
                m_d->currentPromptId.clear();
                QUrl interruptUrl(baseUrl);
                QString ip = interruptUrl.path();
                if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
                else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
                else interruptUrl.setPath(ip + "interrupt");
                m_d->nam->post(QNetworkRequest(interruptUrl), QByteArray("{}"));
            }
            m_d->batchCollectIds.clear();
            m_d->batchSubmitIndex = 0;
            m_d->batchCountTarget = qMax(1, batchCount);
            m_d->batchQueueMode = queueMode;
            m_d->batchBaseUrl = baseUrl;
            QString path = baseUrl.path();
            if (path.isEmpty() || path == "/") m_d->batchBaseUrl.setPath("/prompt");
            else if (!path.endsWith('/')) m_d->batchBaseUrl.setPath(path + "/prompt");
            else m_d->batchBaseUrl.setPath(path + "prompt");
            m_d->batchUseCustomWorkflow = true;
            m_d->batchCustomWorkflow = workflow;
            m_d->progressBar->setValue(0);
            slotBatchSubmitNext();
        });
        return;
    }

    QJsonObject workflow;
    if (!customJson.isEmpty()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(customJson.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            m_d->labelStatus->setText(i18n("Custom workflow JSON error: %1", err.errorString()));
            return;
        }
        workflow = doc.object();
    } else {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(defaultWorkflow), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            m_d->labelStatus->setText(i18n("Workflow JSON error."));
            return;
        }
        workflow = doc.object();
        {
            QJsonObject n3 = workflow["3"].toObject();
            QJsonObject i3 = n3["inputs"].toObject();
            qint64 seed = m_d->checkFixedSeed->isChecked()
                ? static_cast<qint64>(m_d->spinSeed->value())
                : static_cast<qint64>(QRandomGenerator::global()->bounded(2147483647));
            i3["seed"] = static_cast<double>(seed);
            i3["steps"] = m_d->spinSteps->value();
            i3["cfg"] = m_d->spinCfg->value();
            i3["sampler_name"] = m_d->comboSampler->currentText().trimmed().isEmpty()
                ? QString("euler") : m_d->comboSampler->currentText().trimmed();
            n3["inputs"] = i3;
            workflow["3"] = n3;
        }
        {
            QJsonObject n4 = workflow["4"].toObject();
            QJsonObject i4 = n4["inputs"].toObject();
            QString ckpt = m_d->comboCheckpoint->currentText().trimmed();
            i4["ckpt_name"] = ckpt.isEmpty() ? QString("v1-5-pruned-emaonly.safetensors") : ckpt;
            n4["inputs"] = i4;
            workflow["4"] = n4;
        }
        {
            QJsonObject n5 = workflow["5"].toObject();
            QJsonObject i5 = n5["inputs"].toObject();
            int w = m_d->spinWidth->value();
            int h = m_d->spinHeight->value();
            double mul = (m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier);
            mul = qMax(0.3, qMin(mul, 3.0));
            w = static_cast<int>(w * mul);
            h = static_cast<int>(h * mul);
            w = qBound(64, w, 8192);
            h = qBound(64, h, 8192);
            i5["width"] = w;
            i5["height"] = h;
            n5["inputs"] = i5;
            workflow["5"] = n5;
        }
        {
            QJsonObject n6 = workflow["6"].toObject();
            QJsonObject i6 = n6["inputs"].toObject();
            QString promptText = m_d->editPrompt->toPlainText().trimmed();
            i6["text"] = promptText.isEmpty() ? QString("a beautiful painting") : promptText;
            n6["inputs"] = i6;
            workflow["6"] = n6;
        }
        {
            QJsonObject n7 = workflow["7"].toObject();
            QJsonObject i7 = n7["inputs"].toObject();
            i7["text"] = m_d->editNegative->toPlainText().trimmed();
            n7["inputs"] = i7;
            workflow["7"] = n7;
        }
    }

    int batchCount = m_d->spinBatchCount->value();
    int queueMode = m_d->comboQueueMode->currentData().toInt();
    if (queueMode == 2) { // Replace
        m_d->pollTimer->stop();
        for (const QString &id : m_d->jobQueue)
            m_d->pendingHistoryByPromptId.remove(id);
        if (!m_d->currentPromptId.isEmpty())
            m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
        m_d->jobQueue.clear();
        m_d->currentPromptId.clear();
        QUrl interruptUrl(baseUrl);
        QString ip = interruptUrl.path();
        if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
        else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
        else interruptUrl.setPath(ip + "interrupt");
        QNetworkRequest reqI(interruptUrl);
        reqI.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_d->nam->post(reqI, QByteArray("{}"));
    }

    m_d->batchCollectIds.clear();
    m_d->batchSubmitIndex = 0;
    m_d->batchCountTarget = qMax(1, batchCount);
    m_d->batchQueueMode = queueMode;
    m_d->batchBaseUrl = baseUrl;
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") m_d->batchBaseUrl.setPath("/prompt");
    else if (!path.endsWith('/')) m_d->batchBaseUrl.setPath(path + "/prompt");
    else m_d->batchBaseUrl.setPath(path + "prompt");
    m_d->batchUseCustomWorkflow = !customJson.isEmpty();
    if (m_d->batchUseCustomWorkflow)
        m_d->batchCustomWorkflow = workflow;

    m_d->labelStatus->setText(i18n("Submitting…"));
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(false);
    slotBatchSubmitNext();
}

void ComfyUIRemoteDock::slotBatchSubmitNext()
{
    if (m_d->batchSubmitIndex >= m_d->batchCountTarget) {
        // Batch complete: apply enqueue mode
        if (m_d->batchQueueMode == 0) // Back
            m_d->jobQueue.append(m_d->batchCollectIds);
        else if (m_d->batchQueueMode == 1) { // Front
            for (int i = m_d->batchCollectIds.size() - 1; i >= 0; i--)
                m_d->jobQueue.prepend(m_d->batchCollectIds.at(i));
        } else // Replace
            m_d->jobQueue = m_d->batchCollectIds;
        m_d->batchCollectIds.clear();
        m_d->batchCountTarget = 0;
        if (m_d->currentPromptId.isEmpty() && !m_d->jobQueue.isEmpty()) {
            m_d->currentPromptId = m_d->jobQueue.takeFirst();
            m_d->pollCount = 0;
            startPolling();
        }
        updateQueueStatus();
        return;
    }

    QJsonObject workflow;
    if (m_d->batchUseCustomWorkflow) {
        workflow = m_d->batchCustomWorkflow;
    } else {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(defaultWorkflow), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
        workflow = doc.object();
        qint64 seed = m_d->checkFixedSeed->isChecked()
            ? static_cast<qint64>(m_d->spinSeed->value()) + m_d->batchSubmitIndex
            : static_cast<qint64>(QRandomGenerator::global()->bounded(2147483647));
        QJsonObject n3 = workflow["3"].toObject();
        QJsonObject i3 = n3["inputs"].toObject();
        i3["seed"] = static_cast<double>(seed);
        i3["steps"] = m_d->spinSteps->value();
        i3["cfg"] = m_d->spinCfg->value();
        i3["sampler_name"] = m_d->comboSampler->currentText().trimmed().isEmpty()
            ? QString("euler") : m_d->comboSampler->currentText().trimmed();
        n3["inputs"] = i3;
        workflow["3"] = n3;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        i4["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty()
            ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        i5["width"] = m_d->spinWidth->value();
        i5["height"] = m_d->spinHeight->value();
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        QString promptText = m_d->editPrompt->toPlainText().trimmed();
        i6["text"] = promptText.isEmpty() ? QString("a beautiful painting") : promptText;
        n6["inputs"] = i6;
        workflow["6"] = n6;
        QJsonObject n7 = workflow["7"].toObject();
        QJsonObject i7 = n7["inputs"].toObject();
        i7["text"] = m_d->editNegative->toPlainText().trimmed();
        n7["inputs"] = i7;
        workflow["7"] = n7;
    }

    QJsonObject payload;
    payload["prompt"] = workflow;
    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkRequest req(m_d->batchBaseUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_d->nam->post(req, body);
    int index = m_d->batchSubmitIndex;
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Submit error: %1", reply->errorString()));
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            m_d->labelStatus->setText(i18n("Server error: %1", obj["error"].toString()));
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            return;
        }
        QString promptId = obj["prompt_id"].toString();
        if (promptId.isEmpty()) {
            m_d->labelStatus->setText(i18n("No prompt_id in response."));
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            return;
        }
        Private::HistoryEntry entry;
        entry.prompt = m_d->editPrompt->toPlainText();
        entry.negative = m_d->editNegative->toPlainText();
        entry.checkpoint = m_d->comboCheckpoint->currentText();
        entry.width = m_d->spinWidth->value();
        entry.height = m_d->spinHeight->value();
        entry.steps = m_d->spinSteps->value();
        entry.cfg = m_d->spinCfg->value();
        entry.samplerName = m_d->comboSampler->currentText().trimmed();
        entry.seed = m_d->checkFixedSeed->isChecked()
            ? static_cast<qint64>(m_d->spinSeed->value()) + index
            : static_cast<qint64>(QRandomGenerator::global()->bounded(2147483647));
        m_d->pendingHistoryByPromptId.insert(promptId, entry);
        m_d->batchCollectIds.append(promptId);
        m_d->batchSubmitIndex++;
        slotBatchSubmitNext();
    });
}

void ComfyUIRemoteDock::startPolling()
{
    m_d->labelStatus->setText(i18n("Generating… %1", m_d->pollCount));
    m_d->pollTimer->start(1000);
}

void ComfyUIRemoteDock::updateQueueStatus()
{
    int running = m_d->currentPromptId.isEmpty() ? 0 : 1;
    int queued = m_d->jobQueue.size();
    m_d->labelQueueCount->setText(i18n("Queue: %1", running + queued));
    if (m_d->btnQueuePopup) {
        int total = running + queued;
        m_d->btnQueuePopup->setText(QString::number(total) + QStringLiteral(" "));
        if (total > 0) {
            m_d->btnQueuePopup->setIcon(KisIconUtils::loadIcon("run-build"));
            if (queued > 0) {
                m_d->btnQueuePopup->setToolTip(i18n("Generating image. %1 jobs queued. Click to adjust queue or cancel.", queued));
            } else {
                m_d->btnQueuePopup->setToolTip(i18n("Generating image. Click to adjust queue or cancel."));
            }
        } else {
            m_d->btnQueuePopup->setIcon(KisIconUtils::loadIcon("dialog-ok"));
            m_d->btnQueuePopup->setToolTip(i18n("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));
        }
    }
    if (running + queued > 0) {
        if (queued > 0) {
            m_d->labelStatus->setText(i18n("Queue: 1 running, %1 queued.", queued));
        } else {
            m_d->labelStatus->setText(i18n("Generating… %1", m_d->pollCount));
        }
    } else {
        m_d->labelStatus->setText(i18n("Ready."));
    }
    m_d->btnCancelQueue->setEnabled(running + queued > 0);
}

void ComfyUIRemoteDock::refreshHistoryList()
{
    m_d->listHistory->clear();
    const QSize iconSize = m_d->listHistory->iconSize();
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        QString snippet = e.prompt.left(40);
        if (e.prompt.size() > 40) snippet += "…";
        QString tip = QString("%1 (%2×%3)\nSeed: %4").arg(snippet).arg(e.width).arg(e.height).arg(e.seed);
        QListWidgetItem *item = new QListWidgetItem();
        if (!e.resultImagePath.isEmpty() && QFile::exists(e.resultImagePath)) {
            QPixmap pix(e.resultImagePath);
            if (!pix.isNull())
                item->setIcon(QIcon(pix.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        if (item->icon().isNull())
            item->setText(snippet);
        item->setToolTip(tip);
        m_d->listHistory->addItem(item);
    }
}

void ComfyUIRemoteDock::slotHistoryItemSelected()
{
    bool hasSelection = m_d->listHistory->currentRow() >= 0;
    m_d->btnHistoryReRun->setEnabled(hasSelection);
    m_d->btnHistoryApply->setEnabled(hasSelection);
}

void ComfyUIRemoteDock::slotHistoryApply()
{
    int row = m_d->listHistory->currentRow();
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->historyEntries.at(row);
    if (e.resultImagePath.isEmpty() || !QFile::exists(e.resultImagePath)) {
        m_d->labelStatus->setText(i18n("No result image to apply."));
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        m_d->labelStatus->setText(i18n("Open a document first."));
        return;
    }
    qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(e.resultImagePath), "KisPaintLayer");
    if (n > 0) {
        if (m_d->canvas) m_d->canvas->updateCanvas();
        m_d->labelStatus->setText(i18n("Applied result as new layer."));
    } else {
        m_d->labelStatus->setText(i18n("Could not import image."));
    }
}

void ComfyUIRemoteDock::slotHistoryContextMenu(QPoint pos)
{
    QListWidgetItem *item = m_d->listHistory->itemAt(pos);
    if (!item) return;
    int row = m_d->listHistory->row(item);
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    QMenu menu(this);
    menu.addAction(i18n("Apply"), this, &ComfyUIRemoteDock::slotHistoryApply);
    menu.addAction(i18n("Copy prompt"), this, &ComfyUIRemoteDock::slotHistoryCopyPrompt);
    menu.addAction(i18n("Copy seed"), this, &ComfyUIRemoteDock::slotHistoryCopySeed);
    menu.addSeparator();
    menu.addAction(i18n("Discard"), this, &ComfyUIRemoteDock::slotHistoryDiscard);
    menu.addAction(i18n("Clear history"), this, &ComfyUIRemoteDock::slotHistoryClear);
    menu.exec(m_d->listHistory->mapToGlobal(pos));
}

void ComfyUIRemoteDock::slotHistoryCopyPrompt()
{
    int row = m_d->listHistory->currentRow();
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    QString text = m_d->historyEntries.at(row).prompt;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(text);
    m_d->labelStatus->setText(i18n("Prompt copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopySeed()
{
    int row = m_d->listHistory->currentRow();
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    qint64 seed = m_d->historyEntries.at(row).seed;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(QString::number(seed));
    m_d->labelStatus->setText(i18n("Seed copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryDiscard()
{
    int row = m_d->listHistory->currentRow();
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    Private::HistoryEntry e = m_d->historyEntries.takeAt(row);
    if (!e.resultImagePath.isEmpty() && QFile::exists(e.resultImagePath))
        QFile::remove(e.resultImagePath);
    refreshHistoryList();
    m_d->labelStatus->setText(i18n("Discarded from history."));
}

void ComfyUIRemoteDock::slotHistoryClear()
{
    if (m_d->historyEntries.isEmpty()) return;
    if (QMessageBox::question(this, i18n("Clear history"),
            i18n("Discard all %1 generated images from history?", m_d->historyEntries.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        if (!e.resultImagePath.isEmpty() && QFile::exists(e.resultImagePath))
            QFile::remove(e.resultImagePath);
    }
    m_d->historyEntries.clear();
    refreshHistoryList();
    m_d->btnHistoryReRun->setEnabled(false);
    m_d->btnHistoryApply->setEnabled(false);
    m_d->labelStatus->setText(i18n("History cleared."));
}

void ComfyUIRemoteDock::refreshRegionsList()
{
    m_d->listRegions->clear();
    for (const Private::RegionEntry &r : m_d->regionEntries) {
        QString src = r.maskSource == "selection" ? i18n("(selection)") : r.maskSource;
        m_d->listRegions->addItem(r.name + " — " + r.prompt.left(30) + (r.prompt.size() > 30 ? "…" : "") + " " + src);
    }
}

void ComfyUIRemoteDock::loadRegionsFromConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    int n = cfg.readEntry("RegionsCount", 0);
    m_d->regionEntries.clear();
    for (int i = 0; i < n; i++) {
        Private::RegionEntry e;
        e.name = cfg.readEntry(QString("Region_%1_Name").arg(i), QString());
        e.prompt = cfg.readEntry(QString("Region_%1_Prompt").arg(i), QString());
        e.maskSource = cfg.readEntry(QString("Region_%1_MaskSource").arg(i), "selection");
        if (!e.name.isEmpty())
            m_d->regionEntries.append(e);
    }
}

void ComfyUIRemoteDock::saveRegionsToConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", m_d->regionEntries.size());
    for (int i = 0; i < m_d->regionEntries.size(); i++) {
        const Private::RegionEntry &e = m_d->regionEntries.at(i);
        cfg.writeEntry(QString("Region_%1_Name").arg(i), e.name);
        cfg.writeEntry(QString("Region_%1_Prompt").arg(i), e.prompt);
        cfg.writeEntry(QString("Region_%1_MaskSource").arg(i), e.maskSource);
    }
    cfg.config()->sync();
}

void ComfyUIRemoteDock::slotAddRegion()
{
    QStringList maskSources;
    maskSources << "selection";
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP image = m_d->viewManager->image();
        if (image->rootLayer()) {
            QList<KisNodeSP> nodes;
            nodes.append(image->rootLayer());
            while (!nodes.isEmpty()) {
                KisNodeSP n = nodes.takeFirst();
                if (KisLayerSP layer = dynamic_cast<KisLayer*>(n.data())) {
                    if (!layer->name().isEmpty())
                        maskSources << "layer:" + layer->name();
                }
                for (int i = 0; i < static_cast<int>(n->childCount()); i++)
                    nodes.append(n->at(i));
            }
        }
    }
    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Add region"));
    QFormLayout *form = new QFormLayout(&dlg);
    QLineEdit *editName = new QLineEdit();
    editName->setPlaceholderText(i18n("e.g. Background"));
    QLineEdit *editPrompt = new QLineEdit();
    editPrompt->setPlaceholderText(i18n("Prompt for this area"));
    QComboBox *comboMask = new QComboBox();
    comboMask->addItem(i18n("Current selection"), "selection");
    for (const QString &s : maskSources) {
        if (s == "selection") continue;
        comboMask->addItem(s, s);
    }
    form->addRow(i18n("Name:"), editName);
    form->addRow(i18n("Prompt:"), editPrompt);
    form->addRow(i18n("Mask source:"), comboMask);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted) return;
    Private::RegionEntry e;
    e.name = editName->text().trimmed().isEmpty() ? i18n("Region %1", m_d->regionEntries.size() + 1) : editName->text().trimmed();
    e.prompt = editPrompt->text().trimmed();
    e.maskSource = comboMask->currentData().toString();
    m_d->regionEntries.append(e);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(i18n("Added region \"%1\".", e.name));
}

void ComfyUIRemoteDock::slotRemoveRegion()
{
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= m_d->regionEntries.size()) return;
    QString name = m_d->regionEntries.at(row).name;
    m_d->regionEntries.removeAt(row);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(i18n("Removed region \"%1\".", name));
}

void ComfyUIRemoteDock::slotMoveRegionUp()
{
    int row = m_d->listRegions->currentRow();
    if (row <= 0 || row >= m_d->regionEntries.size()) return;
    m_d->regionEntries.move(row, row - 1);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->listRegions->setCurrentRow(row - 1);
}

void ComfyUIRemoteDock::slotMoveRegionDown()
{
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= m_d->regionEntries.size() - 1) return;
    m_d->regionEntries.move(row, row + 1);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->listRegions->setCurrentRow(row + 1);
}

void ComfyUIRemoteDock::slotEditRegion()
{
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= m_d->regionEntries.size()) return;
    QStringList maskSources;
    maskSources << "selection";
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP image = m_d->viewManager->image();
        if (image->rootLayer()) {
            QList<KisNodeSP> nodes;
            nodes.append(image->rootLayer());
            while (!nodes.isEmpty()) {
                KisNodeSP n = nodes.takeFirst();
                if (KisLayerSP layer = dynamic_cast<KisLayer*>(n.data())) {
                    if (!layer->name().isEmpty())
                        maskSources << "layer:" + layer->name();
                }
                for (int i = 0; i < static_cast<int>(n->childCount()); i++)
                    nodes.append(n->at(i));
            }
        }
    }
    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Edit region"));
    QFormLayout *form = new QFormLayout(&dlg);
    QLineEdit *editName = new QLineEdit(m_d->regionEntries.at(row).name);
    QLineEdit *editPrompt = new QLineEdit(m_d->regionEntries.at(row).prompt);
    QComboBox *comboMask = new QComboBox();
    comboMask->addItem(i18n("Current selection"), "selection");
    for (const QString &s : maskSources) {
        if (s == "selection") continue;
        comboMask->addItem(s, s);
    }
    int idx = comboMask->findData(m_d->regionEntries.at(row).maskSource);
    if (idx >= 0) comboMask->setCurrentIndex(idx);
    else comboMask->setCurrentText(m_d->regionEntries.at(row).maskSource);
    form->addRow(i18n("Name:"), editName);
    form->addRow(i18n("Prompt:"), editPrompt);
    form->addRow(i18n("Mask source:"), comboMask);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted) return;
    m_d->regionEntries[row].name = editName->text().trimmed().isEmpty() ? m_d->regionEntries[row].name : editName->text().trimmed();
    m_d->regionEntries[row].prompt = editPrompt->text().trimmed();
    m_d->regionEntries[row].maskSource = comboMask->currentData().toString();
    saveRegionsToConfig();
    refreshRegionsList();
}

namespace
{
QImage getCanvasAsQImage(KisImageSP image)
{
    if (!image || !image->projection()) return QImage();
    QRect bounds = image->bounds();
    if (bounds.isEmpty()) return QImage();
    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    return image->projection()->convertToQImage(profile, bounds,
        KoColorConversionTransformation::internalRenderingIntent(),
        KoColorConversionTransformation::internalConversionFlags());
}

QImage getMaskAsQImage(KisImageSP image, KisViewManager *viewManager, const QString &maskSource)
{
    QRect bounds = image->bounds();
    if (bounds.isEmpty()) return QImage();
    QImage maskImage(bounds.width(), bounds.height(), QImage::Format_Grayscale8);
    maskImage.fill(0);

    if (maskSource == "selection") {
        KisSelectionSP sel = viewManager ? viewManager->selection() : nullptr;
        if (!sel || !sel->pixelSelection()) return QImage();
        QRect rect = sel->selectedExactRect();
        rect &= bounds;
        if (rect.isEmpty()) return QImage();
        KisPaintDeviceSP dev = sel->pixelSelection();
        int ps = dev->pixelSize();
        QVector<quint8> data(rect.width() * rect.height() * ps);
        dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
        for (int y = 0; y < rect.height(); y++) {
            for (int x = 0; x < rect.width(); x++) {
                int srcIdx = (y * rect.width() + x) * ps;
                quint8 v = ps > 0 ? data.value(srcIdx, 0) : 0;
                maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
            }
        }
        return maskImage;
    }

    if (maskSource.startsWith("layer:")) {
        QString layerName = maskSource.mid(6);
        KisNodeSP root = image->rootLayer();
        if (!root) return QImage();
        QList<KisNodeSP> nodes;
        nodes.append(root);
        KisNodeSP foundNode;
        while (!nodes.isEmpty()) {
            KisNodeSP n = nodes.takeFirst();
            if (n->name() == layerName) { foundNode = n; break; }
            for (int i = 0; i < static_cast<int>(n->childCount()); i++) nodes.append(n->at(i));
        }
        KisLayer *foundLayer = foundNode ? dynamic_cast<KisLayer*>(foundNode.data()) : nullptr;
        if (!foundLayer || !foundLayer->projection()) return QImage();
        const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
        QImage rgba = foundLayer->projection()->convertToQImage(profile, bounds,
            KoColorConversionTransformation::internalRenderingIntent(),
            KoColorConversionTransformation::internalConversionFlags());
        if (rgba.isNull() || rgba.size() != maskImage.size()) return QImage();
        for (int y = 0; y < rgba.height(); y++) {
            for (int x = 0; x < rgba.width(); x++) {
                int a = qAlpha(rgba.pixel(x, y));
                maskImage.setPixel(x, y, qRgb(a, a, a));
            }
        }
        return maskImage;
    }
    return QImage();
}

// Composite result over current using mask (mask white = use result pixel).
void compositeWithMask(QImage &current, const QImage &result, const QImage &mask)
{
    if (current.size() != result.size() || current.size() != mask.size() || result.format() != QImage::Format_RGB32) return;
    if (current.format() != QImage::Format_ARGB32 && current.format() != QImage::Format_RGB32)
        current = current.convertToFormat(QImage::Format_ARGB32);
    QImage res = result.format() == QImage::Format_ARGB32 ? result : result.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < current.height(); y++) {
        for (int x = 0; x < current.width(); x++) {
            int m = qGray(mask.pixel(x, y));
            if (m <= 0) continue;
            QRgb cur = current.pixel(x, y);
            QRgb resPix = res.pixel(x, y);
            if (m >= 255) {
                current.setPixel(x, y, resPix);
            } else {
                int inv = 255 - m;
                current.setPixel(x, y, qRgba(
                    (qRed(cur) * inv + qRed(resPix) * m) / 255,
                    (qGreen(cur) * inv + qGreen(resPix) * m) / 255,
                    (qBlue(cur) * inv + qBlue(resPix) * m) / 255,
                    (qAlpha(cur) * inv + qAlpha(resPix) * m) / 255));
            }
        }
    }
}
} // namespace

void ComfyUIRemoteDock::slotGenerateRegions()
{
    if (m_d->regionEntries.isEmpty()) {
        m_d->labelStatus->setText(i18n("Add at least one region (name, prompt, mask source)."));
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->labelStatus->setText(i18n("Open a document first."));
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL."));
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    m_d->regionCurrentImage = getCanvasAsQImage(image);
    if (m_d->regionCurrentImage.isNull()) {
        m_d->labelStatus->setText(i18n("Could not export canvas."));
        return;
    }
    m_d->regionCurrentImage = m_d->regionCurrentImage.convertToFormat(QImage::Format_ARGB32);
    m_d->regionIndex = 0;
    m_d->regionUploadedImageName.clear();
    m_d->regionUploadedImageSubfolder.clear();
    m_d->btnGenerateRegions->setEnabled(false);

    auto setUploadPath = [baseUrl](QUrl &url, const QString &pathSuffix) {
        QString p = url.path();
        if (p.isEmpty() || p == "/") url.setPath("/" + pathSuffix);
        else if (!p.endsWith('/')) url.setPath(p + "/" + pathSuffix);
        else url.setPath(p + pathSuffix);
    };

    // Step 1: Upload canvas image (once).
    QUrl uploadUrl = baseUrl;
    setUploadPath(uploadUrl, "upload/image");
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->write(QByteArray()); // ensure file exists
    tmpImage->close();
    if (!m_d->regionCurrentImage.save(tmpImage->fileName())) {
        m_d->labelStatus->setText(i18n("Could not save temp image."));
        m_d->btnGenerateRegions->setEnabled(true);
        return;
    }
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_region_canvas.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(uploadUrl);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(i18n("Uploading canvas…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Upload error: %1", reply->errorString()));
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        m_d->regionUploadedImageName = obj.value("name").toString();
        m_d->regionUploadedImageSubfolder = obj.value("subfolder").toString();
        if (m_d->regionUploadedImageName.isEmpty()) {
            m_d->labelStatus->setText(i18n("Server did not return image name."));
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        runNextRegionInpainting();
    });
}

void ComfyUIRemoteDock::runNextRegionInpainting()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { m_d->btnGenerateRegions->setEnabled(true); return; }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) { m_d->btnGenerateRegions->setEnabled(true); return; }

    if (m_d->regionIndex >= m_d->regionEntries.size()) {
        if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
            m_d->labelStatus->setText(i18n("Regions done (no document to paste)."));
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        QTemporaryFile tmp;
        tmp.setFileTemplate(tmp.fileTemplate() + ".png");
        if (!tmp.open() || !m_d->regionCurrentImage.save(tmp.fileName())) {
            m_d->labelStatus->setText(i18n("Could not save result."));
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        tmp.close();
        qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
        if (n > 0 && m_d->canvas) m_d->canvas->updateCanvas();
        m_d->labelStatus->setText(i18n("Regions done. Result added as new layer."));
        m_d->btnGenerateRegions->setEnabled(true);
        return;
    }

    const Private::RegionEntry &region = m_d->regionEntries.at(m_d->regionIndex);
    KisImageSP image = m_d->viewManager->image();
    QImage maskImg = getMaskAsQImage(image, m_d->viewManager, region.maskSource);
    if (maskImg.isNull()) {
        m_d->labelStatus->setText(i18n("Region \"%1\": could not get mask.", region.name));
        m_d->regionIndex++;
        QTimer::singleShot(0, this, &ComfyUIRemoteDock::runNextRegionInpainting);
        return;
    }
    m_d->labelStatus->setText(i18n("Region %1/%2: %3…", m_d->regionIndex + 1, m_d->regionEntries.size(), region.name));

    // Save mask as PNG with alpha (ComfyUI uses alpha: 0 = inpaint after invert).
    QImage maskPng(maskImg.size(), QImage::Format_ARGB32);
    for (int y = 0; y < maskImg.height(); y++)
        for (int x = 0; x < maskImg.width(); x++) {
            int g = qGray(maskImg.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    QTemporaryFile *tmpMask = new QTemporaryFile(this);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        m_d->regionIndex++;
        QTimer::singleShot(0, this, &ComfyUIRemoteDock::runNextRegionInpainting);
        return;
    }
    tmpMask->open();
    QUrl uploadUrl(baseUrl);
    QString upPath = uploadUrl.path();
    if (upPath.isEmpty() || upPath == "/") uploadUrl.setPath("/upload/image");
    else if (!upPath.endsWith('/')) uploadUrl.setPath(upPath + "/upload/image");
    else uploadUrl.setPath(upPath + "upload/image");
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_region_mask.png\""));
    part.setBodyDevice(tmpMask);
    tmpMask->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Mask upload error: %1", reply->errorString()));
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->regionMaskUploadedName = obj.value("name").toString();
        m_d->regionMaskUploadedSubfolder = obj.value("subfolder").toString();
        if (m_d->regionMaskUploadedName.isEmpty()) {
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        const Private::RegionEntry &r = m_d->regionEntries.at(m_d->regionIndex);
        QJsonParseError err;
        QJsonObject workflow = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err).object();
        if (err.error != QJsonParseError::NoError) {
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        QJsonObject n1 = workflow["1"].toObject();
        QJsonObject i1 = n1["inputs"].toObject();
        i1["image"] = m_d->regionUploadedImageName;
        n1["inputs"] = i1;
        workflow["1"] = n1;
        QJsonObject n2 = workflow["2"].toObject();
        QJsonObject i2 = n2["inputs"].toObject();
        i2["image"] = m_d->regionMaskUploadedName;
        n2["inputs"] = i2;
        workflow["2"] = n2;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        i4["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty() ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        i5["text"] = r.prompt.trimmed().isEmpty() ? QString("a beautiful painting") : r.prompt.trimmed();
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        i6["text"] = m_d->editNegative->toPlainText().trimmed();
        n6["inputs"] = i6;
        workflow["6"] = n6;
        QJsonObject n8 = workflow["8"].toObject();
        QJsonObject i8 = n8["inputs"].toObject();
        i8["seed"] = static_cast<double>(QRandomGenerator::global()->bounded(2147483647));
        n8["inputs"] = i8;
        workflow["8"] = n8;
        QJsonObject payload;
        payload["prompt"] = workflow;
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
        else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
        else promptUrl.setPath(p + "prompt");
        QNetworkRequest reqPrompt(promptUrl);
        reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt]() {
            replyPrompt->deleteLater();
            if (replyPrompt->error() != QNetworkReply::NoError) {
                m_d->labelStatus->setText(i18n("Submit error: %1", replyPrompt->errorString()));
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyPrompt->readAll()).object();
            if (obj.contains("error")) {
                m_d->labelStatus->setText(i18n("Server: %1", obj["error"].toString()));
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            m_d->regionPromptId = obj["prompt_id"].toString();
            if (m_d->regionPromptId.isEmpty()) {
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            m_d->regionPollCount = 0;
            pollRegionHistory();
        });
    });
}

void ComfyUIRemoteDock::pollRegionHistory()
{
    if (m_d->regionPromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) return;
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->regionPromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->regionPromptId);
    else baseUrl.setPath(path + "history/" + m_d->regionPromptId);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("History error: %1", reply->errorString()));
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->regionPromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->regionPollCount++;
            if (m_d->regionPollCount >= Private::regionMaxPollCount) {
                m_d->labelStatus->setText(i18n("Region generation timed out."));
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            QTimer::singleShot(1000, this, &ComfyUIRemoteDock::pollRegionHistory);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        vp += "view";
        viewUrl.setPath(vp);
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqView(viewUrl);
        QNetworkReply *replyView = m_d->nam->get(reqView);
        connect(replyView, &QNetworkReply::finished, this, [this, replyView]() {
            replyView->deleteLater();
            if (replyView->error() != QNetworkReply::NoError) {
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            QImage result;
            result.loadFromData(replyView->readAll());
            if (!result.isNull() && result.size() == m_d->regionCurrentImage.size()) {
                QImage maskImg = getMaskAsQImage(m_d->viewManager->image(), m_d->viewManager, m_d->regionEntries.at(m_d->regionIndex).maskSource);
                if (!maskImg.isNull())
                    compositeWithMask(m_d->regionCurrentImage, result.convertToFormat(QImage::Format_ARGB32), maskImg);
            }
            m_d->regionPromptId.clear();
            m_d->regionIndex++;
            runNextRegionInpainting();
        });
    });
}

void ComfyUIRemoteDock::slotInpaint()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->labelStatus->setText(i18n("Open a document first."));
        return;
    }
    KisSelectionSP sel = m_d->viewManager->selection();
    if (!sel || !sel->pixelSelection()) {
        m_d->labelStatus->setText(i18n("Make a selection to inpaint."));
        return;
    }
    QRect rect = sel->pixelSelection()->selectedExactRect();
    if (rect.isEmpty()) {
        m_d->labelStatus->setText(i18n("Selection is empty. Draw a selection first."));
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL first."));
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    m_d->inpaintCurrentImage = getCanvasAsQImage(image);
    if (m_d->inpaintCurrentImage.isNull()) {
        m_d->labelStatus->setText(i18n("Could not export canvas."));
        return;
    }
    m_d->inpaintCurrentImage = m_d->inpaintCurrentImage.convertToFormat(QImage::Format_ARGB32);
    QImage maskImg = getMaskAsQImage(image, m_d->viewManager, QString("selection"));
    if (maskImg.isNull()) {
        m_d->labelStatus->setText(i18n("Could not get selection mask."));
        return;
    }
    // Mask for ComfyUI VAEEncodeForInpaint: white = area to inpaint
    QImage maskPng(maskImg.size(), QImage::Format_ARGB32);
    for (int y = 0; y < maskImg.height(); y++)
        for (int x = 0; x < maskImg.width(); x++) {
            int g = qGray(maskImg.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!m_d->inpaintCurrentImage.save(tmpImage->fileName())) {
        m_d->labelStatus->setText(i18n("Could not save temp image."));
        return;
    }
    QTemporaryFile *tmpMask = new QTemporaryFile(this);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        m_d->labelStatus->setText(i18n("Could not save temp mask."));
        return;
    }
    m_d->btnInpaint->setEnabled(false);
    m_d->progressBar->setValue(0);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/upload/image");
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/upload/image");
    else baseUrl.setPath(path + "upload/image");
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_inpaint.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(i18n("Uploading image…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpMask, baseUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Upload error: %1", reply->errorString()));
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->inpaintUploadedImageName = obj.value("name").toString();
        m_d->inpaintUploadedImageSubfolder = obj.value("subfolder").toString();
        if (m_d->inpaintUploadedImageName.isEmpty()) {
            m_d->labelStatus->setText(i18n("Server did not return image name."));
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl uploadUrl(m_d->editServerUrl->text().trimmed());
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
        else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
        else uploadUrl.setPath(up + "upload/image");
        tmpMask->open();
        QHttpMultiPart *maskPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
            QVariant("form-data; name=\"image\"; filename=\"krita_inpaint_mask.png\""));
        part.setBodyDevice(tmpMask);
        tmpMask->setParent(maskPart);
        maskPart->append(part);
        QNetworkRequest reqMask(uploadUrl);
        QNetworkReply *replyMask = m_d->nam->post(reqMask, maskPart);
        maskPart->setParent(replyMask);
        m_d->labelStatus->setText(i18n("Uploading mask…"));
        connect(replyMask, &QNetworkReply::finished, this, [this, replyMask]() {
            replyMask->deleteLater();
            if (replyMask->error() != QNetworkReply::NoError) {
                m_d->labelStatus->setText(i18n("Mask upload error: %1", replyMask->errorString()));
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyMask->readAll()).object();
            m_d->inpaintUploadedMaskName = obj.value("name").toString();
            m_d->inpaintUploadedMaskSubfolder = obj.value("subfolder").toString();
            if (m_d->inpaintUploadedMaskName.isEmpty()) {
                m_d->labelStatus->setText(i18n("Server did not return mask name."));
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonParseError err;
            QJsonObject workflow = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err).object();
            if (err.error != QJsonParseError::NoError) {
                m_d->labelStatus->setText(i18n("Inpainting workflow error."));
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject n1 = workflow["1"].toObject();
            QJsonObject i1 = n1["inputs"].toObject();
            i1["image"] = m_d->inpaintUploadedImageName;
            n1["inputs"] = i1;
            workflow["1"] = n1;
            QJsonObject n2 = workflow["2"].toObject();
            QJsonObject i2 = n2["inputs"].toObject();
            i2["image"] = m_d->inpaintUploadedMaskName;
            n2["inputs"] = i2;
            workflow["2"] = n2;
            QJsonObject n4 = workflow["4"].toObject();
            QJsonObject i4 = n4["inputs"].toObject();
            i4["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty()
                ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
            n4["inputs"] = i4;
            workflow["4"] = n4;
            QJsonObject n5 = workflow["5"].toObject();
            QJsonObject i5 = n5["inputs"].toObject();
            i5["text"] = m_d->editPrompt->toPlainText().trimmed().isEmpty()
                ? QString("a beautiful painting") : m_d->editPrompt->toPlainText().trimmed();
            n5["inputs"] = i5;
            workflow["5"] = n5;
            QJsonObject n6 = workflow["6"].toObject();
            QJsonObject i6 = n6["inputs"].toObject();
            i6["text"] = m_d->editNegative->toPlainText().trimmed();
            n6["inputs"] = i6;
            workflow["6"] = n6;
            QJsonObject n8 = workflow["8"].toObject();
            QJsonObject i8 = n8["inputs"].toObject();
            i8["seed"] = static_cast<double>(QRandomGenerator::global()->bounded(2147483647));
            n8["inputs"] = i8;
            workflow["8"] = n8;
            QJsonObject payload;
            payload["prompt"] = workflow;
            QUrl promptUrl(m_d->editServerUrl->text().trimmed());
            QString p = promptUrl.path();
            if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
            else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
            else promptUrl.setPath(p + "prompt");
            QNetworkRequest reqPrompt(promptUrl);
            reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
            connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt]() {
                replyPrompt->deleteLater();
                if (replyPrompt->error() != QNetworkReply::NoError) {
                    m_d->labelStatus->setText(i18n("Submit error: %1", replyPrompt->errorString()));
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                QJsonObject obj = QJsonDocument::fromJson(replyPrompt->readAll()).object();
                if (obj.contains("error")) {
                    m_d->labelStatus->setText(i18n("Server: %1", obj["error"].toString()));
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                m_d->inpaintPromptId = obj["prompt_id"].toString();
                if (m_d->inpaintPromptId.isEmpty()) {
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                m_d->inpaintPollCount = 0;
                m_d->labelStatus->setText(i18n("Inpainting…"));
                m_d->inpaintPollTimer->start(1000);
            });
        });
    });
}

void ComfyUIRemoteDock::slotInpaintPoll()
{
    if (m_d->inpaintPromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->inpaintPromptId.clear();
        m_d->btnInpaint->setEnabled(true);
        m_d->progressBar->setValue(0);
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->inpaintPromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->inpaintPromptId);
    else baseUrl.setPath(path + "history/" + m_d->inpaintPromptId);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("History error: %1", reply->errorString()));
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->inpaintPromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->inpaintPollCount++;
            if (m_d->inpaintPollCount >= Private::inpaintMaxPollCount) {
                m_d->labelStatus->setText(i18n("Inpaint timed out."));
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->inpaintPollTimer->start(1000);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        vp += "view";
        viewUrl.setPath(vp);
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqView(viewUrl);
        QNetworkReply *replyView = m_d->nam->get(reqView);
        connect(replyView, &QNetworkReply::finished, this, [this, replyView]() {
            replyView->deleteLater();
            if (replyView->error() != QNetworkReply::NoError) {
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QImage result;
            result.loadFromData(replyView->readAll());
            KisImageSP image = m_d->viewManager->image();
            QImage maskImg = getMaskAsQImage(image, m_d->viewManager, QString("selection"));
            if (!result.isNull() && result.size() == m_d->inpaintCurrentImage.size() && !maskImg.isNull()) {
                compositeWithMask(m_d->inpaintCurrentImage, result.convertToFormat(QImage::Format_ARGB32), maskImg);
            } else if (!result.isNull()) {
                m_d->inpaintCurrentImage = result.convertToFormat(QImage::Format_ARGB32);
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open() || !m_d->inpaintCurrentImage.save(tmp.fileName())) {
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            tmp.close();
            if (m_d->viewManager->imageManager()) {
                m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (m_d->canvas) m_d->canvas->updateCanvas();
            }
            m_d->labelStatus->setText(i18n("Inpaint done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
        });
    });
}

void ComfyUIRemoteDock::slotUpscale()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->labelStatus->setText(i18n("Open a document first."));
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->labelStatus->setText(i18n("Enter a server URL first."));
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        m_d->labelStatus->setText(i18n("Invalid URL."));
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    QImage canvasImg = getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        m_d->labelStatus->setText(i18n("Could not export canvas."));
        return;
    }
    int w = canvasImg.width();
    int h = canvasImg.height();
    int w2 = w * 2;
    int h2 = h * 2;
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!canvasImg.save(tmpImage->fileName())) {
        m_d->labelStatus->setText(i18n("Could not save temp image."));
        return;
    }
    m_d->btnUpscale->setEnabled(false);
    m_d->progressBar->setValue(0);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/upload/image");
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/upload/image");
    else baseUrl.setPath(path + "upload/image");
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_upscale.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(i18n("Uploading for upscale…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, w2, h2]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("Upload error: %1", reply->errorString()));
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->upscaleUploadedImageName = obj.value("name").toString();
        if (m_d->upscaleUploadedImageName.isEmpty()) {
            m_d->labelStatus->setText(i18n("Server did not return image name."));
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonParseError err;
        QJsonObject workflow = QJsonDocument::fromJson(QByteArray(upscaleWorkflowTemplate), &err).object();
        if (err.error != QJsonParseError::NoError) {
            m_d->labelStatus->setText(i18n("Upscale workflow error."));
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject n1 = workflow["1"].toObject();
        QJsonObject i1 = n1["inputs"].toObject();
        i1["image"] = m_d->upscaleUploadedImageName;
        n1["inputs"] = i1;
        workflow["1"] = n1;
        QJsonObject n2 = workflow["2"].toObject();
        QJsonObject i2 = n2["inputs"].toObject();
        i2["width"] = w2;
        i2["height"] = h2;
        n2["inputs"] = i2;
        workflow["2"] = n2;
        QJsonObject payload;
        payload["prompt"] = workflow;
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
        else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
        else promptUrl.setPath(p + "prompt");
        QNetworkRequest reqPrompt(promptUrl);
        reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt]() {
            replyPrompt->deleteLater();
            if (replyPrompt->error() != QNetworkReply::NoError) {
                m_d->labelStatus->setText(i18n("Submit error: %1", replyPrompt->errorString()));
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyPrompt->readAll()).object();
            if (obj.contains("error")) {
                m_d->labelStatus->setText(i18n("Server: %1", obj["error"].toString()));
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePromptId = obj["prompt_id"].toString();
            if (m_d->upscalePromptId.isEmpty()) {
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePollCount = 0;
            m_d->labelStatus->setText(i18n("Upscaling…"));
            m_d->upscalePollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotUpscalePoll()
{
    if (m_d->upscalePromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->upscalePromptId.clear();
        m_d->btnUpscale->setEnabled(true);
        m_d->progressBar->setValue(0);
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->upscalePromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->upscalePromptId);
    else baseUrl.setPath(path + "history/" + m_d->upscalePromptId);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_d->labelStatus->setText(i18n("History error: %1", reply->errorString()));
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->upscalePromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->upscalePollCount++;
            if (m_d->upscalePollCount >= Private::upscaleMaxPollCount) {
                m_d->labelStatus->setText(i18n("Upscale timed out."));
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePollTimer->start(1000);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        vp += "view";
        viewUrl.setPath(vp);
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqView(viewUrl);
        QNetworkReply *replyView = m_d->nam->get(reqView);
        connect(replyView, &QNetworkReply::finished, this, [this, replyView]() {
            replyView->deleteLater();
            if (replyView->error() != QNetworkReply::NoError) {
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open()) {
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            tmp.write(replyView->readAll());
            tmp.close();
            if (m_d->viewManager->imageManager()) {
                m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (m_d->canvas) m_d->canvas->updateCanvas();
            }
            m_d->labelStatus->setText(i18n("Upscale done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
        });
    });
}

void ComfyUIRemoteDock::slotGenerateAnimation()
{
    m_d->spinBatchCount->setValue(m_d->spinAnimationFrames->value());
    m_d->checkFixedSeed->setChecked(true);
    slotGenerate();
}

void ComfyUIRemoteDock::slotConfigureHelp()
{
    if (!m_d->settingsDialog) {
        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(i18n("AI Image (ComfyUI) Settings"));
        dlg->resize(520, 420);
        m_d->settingsDialog = dlg;

        QVBoxLayout *mainLayout = new QVBoxLayout(dlg);
        QTabWidget *tabs = new QTabWidget(dlg);
        mainLayout->addWidget(tabs);

        // Connection tab
        QWidget *connPage = new QWidget(dlg);
        QVBoxLayout *connLayout = new QVBoxLayout(connPage);

        QFormLayout *connForm = new QFormLayout();
        connForm->addRow(i18n("ComfyUI server URL:"), m_d->editServerUrl);
        connLayout->addLayout(connForm);

        QHBoxLayout *connButtons = new QHBoxLayout();
        m_d->btnTest = new QPushButton(i18n("Test connection"), connPage);
        m_d->btnTest->setIcon(KisIconUtils::loadIcon("network-connect"));
        connect(m_d->btnTest, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotTestConnection);

        QPushButton *docsButton = new QPushButton(i18n("Open documentation"), connPage);
        docsButton->setIcon(KisIconUtils::loadIcon("help-contents"));
        connect(docsButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://docs.interstice.cloud")));
        });

        connButtons->addWidget(m_d->btnTest);
        connButtons->addWidget(docsButton);
        connButtons->addStretch();
        connLayout->addLayout(connButtons);
        connLayout->addStretch();

        tabs->addTab(connPage, i18n("Connection"));

        // Workflow tab
        QWidget *workflowPage = new QWidget(dlg);
        QVBoxLayout *workflowLayout = new QVBoxLayout(workflowPage);
        workflowLayout->addWidget(new QLabel(i18n("Custom workflow (optional, API JSON):"), workflowPage));
        workflowLayout->addWidget(m_d->checkUseReferenceImage);
        workflowLayout->addWidget(m_d->editCustomWorkflow);
        workflowLayout->addWidget(m_d->btnLoadWorkflow);
        workflowLayout->addStretch();

        tabs->addTab(workflowPage, i18n("Workflow"));
    }

    if (m_d->settingsDialog) {
        m_d->settingsDialog->show();
        m_d->settingsDialog->raise();
        m_d->settingsDialog->activateWindow();
    }
}

void ComfyUIRemoteDock::slotLiveTick()
{
    if (!m_d->checkLiveMode->isChecked() || !m_d->viewManager || !m_d->viewManager->image()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { m_d->liveTimer->start(30000); return; }
    KisImageSP image = m_d->viewManager->image();
    QImage canvasImg = getCanvasAsQImage(image);
    if (canvasImg.isNull()) { m_d->liveTimer->start(30000); return; }
    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + ".png");
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) { m_d->liveTimer->start(30000); return; }
    QUrl uploadUrl(m_d->editServerUrl->text().trimmed());
    QString up = uploadUrl.path();
    if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
    else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
    else uploadUrl.setPath(up + "upload/image");
    tmp->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"image\"; filename=\"krita_live.png\""));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!m_d->checkLiveMode->isChecked() || reply->error() != QNetworkReply::NoError) {
            if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
            return;
        }
        m_d->liveUploadedImageName = QJsonDocument::fromJson(reply->readAll()).object().value("name").toString();
        if (m_d->liveUploadedImageName.isEmpty()) { m_d->liveTimer->start(30000); return; }
        QJsonParseError err;
        QJsonObject workflow = QJsonDocument::fromJson(QByteArray(img2imgWorkflowTemplate), &err).object();
        if (err.error != QJsonParseError::NoError) { m_d->liveTimer->start(30000); return; }
        QJsonObject n1 = workflow["1"].toObject();
        QJsonObject i1 = n1["inputs"].toObject();
        i1["image"] = m_d->liveUploadedImageName;
        n1["inputs"] = i1;
        workflow["1"] = n1;
        QJsonObject n3 = workflow["3"].toObject();
        QJsonObject i3 = n3["inputs"].toObject();
        i3["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty() ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
        n3["inputs"] = i3;
        workflow["3"] = n3;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        i4["text"] = m_d->editPrompt->toPlainText().trimmed().isEmpty() ? QString("a beautiful painting") : m_d->editPrompt->toPlainText().trimmed();
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        i5["text"] = m_d->editNegative->toPlainText().trimmed();
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        i6["seed"] = static_cast<double>(QRandomGenerator::global()->bounded(2147483647));
        n6["inputs"] = i6;
        workflow["6"] = n6;
        QJsonObject payload;
        payload["prompt"] = workflow;
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
        else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
        else promptUrl.setPath(p + "prompt");
        QNetworkRequest reqP(promptUrl);
        reqP.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *replyP = m_d->nam->post(reqP, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyP, &QNetworkReply::finished, this, [this, replyP]() {
            replyP->deleteLater();
            if (!m_d->checkLiveMode->isChecked() || replyP->error() != QNetworkReply::NoError) {
                if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
                return;
            }
            m_d->livePromptId = QJsonDocument::fromJson(replyP->readAll()).object().value("prompt_id").toString();
            if (m_d->livePromptId.isEmpty()) { m_d->liveTimer->start(30000); return; }
            m_d->livePollCount = 0;
            m_d->livePollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotLivePoll()
{
    if (m_d->livePromptId.isEmpty() || !m_d->checkLiveMode->isChecked()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { m_d->livePromptId.clear(); m_d->liveTimer->start(30000); return; }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->livePromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->livePromptId);
    else baseUrl.setPath(path + "history/" + m_d->livePromptId);
    QNetworkRequest req(baseUrl);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!m_d->checkLiveMode->isChecked()) { m_d->livePromptId.clear(); return; }
        if (reply->error() != QNetworkReply::NoError) { m_d->livePromptId.clear(); m_d->liveTimer->start(30000); return; }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->livePromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->livePollCount++;
            if (m_d->livePollCount >= Private::liveMaxPollCount) { m_d->livePromptId.clear(); m_d->liveTimer->start(30000); return; }
            m_d->livePollTimer->start(1000);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) { m_d->livePromptId.clear(); m_d->liveTimer->start(30000); return; }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        viewUrl.setPath(vp + "view");
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqV(viewUrl);
        QNetworkReply *replyV = m_d->nam->get(reqV);
        connect(replyV, &QNetworkReply::finished, this, [this, replyV]() {
            replyV->deleteLater();
            if (!m_d->checkLiveMode->isChecked()) { m_d->livePromptId.clear(); return; }
            if (replyV->error() != QNetworkReply::NoError) { m_d->livePromptId.clear(); m_d->liveTimer->start(30000); return; }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (tmp.open()) {
                tmp.write(replyV->readAll());
                tmp.close();
                if (m_d->viewManager->imageManager()) {
                    m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                    if (m_d->canvas) m_d->canvas->updateCanvas();
                }
            }
            m_d->livePromptId.clear();
            if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
        });
    });
}

void ComfyUIRemoteDock::slotHistoryReRun()
{
    int row = m_d->listHistory->currentRow();
    if (row < 0 || row >= m_d->historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->historyEntries.at(row);
    m_d->editPrompt->setPlainText(e.prompt);
    m_d->editNegative->setPlainText(e.negative);
    m_d->spinWidth->setValue(e.width);
    m_d->spinHeight->setValue(e.height);
    m_d->spinSteps->setValue(e.steps);
    m_d->spinCfg->setValue(e.cfg);
    m_d->comboSampler->setCurrentText(e.samplerName.isEmpty() ? QString("euler") : e.samplerName);
    m_d->checkFixedSeed->setChecked(true);
    m_d->spinSeed->setValue(static_cast<int>(e.seed));
    int i = m_d->comboCheckpoint->findText(e.checkpoint);
    if (i >= 0) m_d->comboCheckpoint->setCurrentIndex(i);
    else m_d->comboCheckpoint->setCurrentText(e.checkpoint);
    slotGenerate();
}

void ComfyUIRemoteDock::slotCancelQueue()
{
    m_d->pollTimer->stop();
    for (const QString &id : m_d->jobQueue)
        m_d->pendingHistoryByPromptId.remove(id);
    if (!m_d->currentPromptId.isEmpty())
        m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
    m_d->jobQueue.clear();
    m_d->currentPromptId.clear();
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(true);
    m_d->btnCancelQueue->setEnabled(false);
    m_d->labelStatus->setText(i18n("Cancelled."));
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) return;
    QUrl url(urlStr);
    if (!url.isValid()) return;
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/interrupt");
    else if (!path.endsWith('/')) url.setPath(path + "/interrupt");
    else url.setPath(path + "interrupt");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_d->nam->post(req, QByteArray("{}"));
}
