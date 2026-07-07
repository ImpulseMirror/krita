/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyLiveRunner.h"
#include "ComfyTheme.h"
#include "ComfyLocalization.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyPromptLayoutMetrics.h"
#include "ComfyHistoryInternal.h"

#include <kis_image.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_node_manager.h>

#include <QBoxLayout>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QPixmap>
#include <QSizePolicy>
#include <QUuid>
#include <QVBoxLayout>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace {

QBoxLayout *boxLayoutOf(QWidget *widget)
{
    if (!widget || !widget->parentWidget())
        return nullptr;
    return qobject_cast<QBoxLayout *>(widget->parentWidget()->layout());
}

void removeFromParentLayout(QWidget *widget)
{
    if (!widget)
        return;
    if (QBoxLayout *lay = boxLayoutOf(widget))
        lay->removeWidget(widget);
}

void clearHBox(QHBoxLayout *row)
{
    if (!row)
        return;
    while (QLayoutItem *item = row->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->setParent(row->parentWidget());
        delete item;
    }
}

void clearVBox(QVBoxLayout *col)
{
    if (!col)
        return;
    while (QLayoutItem *item = col->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->setParent(col->parentWidget());
        delete item;
    }
}

void applyLiveRegionPromptLayout(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.regionPromptWidget)
        return;
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool showNeg = s.value(QStringLiteral("show_negative_prompt")).toBool(false);
    const int lines = qBound(1,
                              s.value(QStringLiteral("prompt_line_count_live"))
                                  .toInt(ComfyPromptLayoutMetrics::kLivePositiveLinesDefault),
                              10);
    const int posLines = showNeg ? qMax(lines - ComfyPromptLayoutMetrics::kNegativeLineCount, 1) : lines;
    d->generate.regionPromptWidget->setLiveSingleRegionMode(true);
    d->generate.regionPromptWidget->setPromptHeaderMode(2);
    d->generate.regionPromptWidget->setShowNegativePrompt(showNeg);
    d->generate.regionPromptWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d->generate.regionPromptWidget->setMinimumSize(0, 0);
    d->generate.regionPromptWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    d->generate.regionPromptWidget->applyCompactLayout(posLines, showNeg, true, true);
    d->generate.regionPromptWidget->refresh();
}

void applyLivePromptRowHeights(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.regionPromptWidget || !d->live.livePromptHostWidget || !d->live.livePromptRowWidget)
        return;

    const int promptH = d->generate.regionPromptWidget->height();
    if (promptH <= 0)
        return;

    int hostH = promptH;
    if (QLayout *hostLay = d->live.livePromptHostWidget->layout()) {
        const QMargins mg = hostLay->contentsMargins();
        hostH += mg.top() + mg.bottom();
        if (auto *box = qobject_cast<QVBoxLayout *>(hostLay))
            box->setAlignment(Qt::AlignTop);
    }

    int rowH = hostH;
    if (QLayout *rowLay = d->live.livePromptRowWidget->layout()) {
        const QMargins mg = rowLay->contentsMargins();
        rowH += mg.top() + mg.bottom();
        if (auto *box = qobject_cast<QHBoxLayout *>(rowLay))
            box->setAlignment(Qt::AlignTop);
    }
    if (d->live.livePromptButtonsWidget) {
        int buttonsH = d->live.livePromptButtonsWidget->sizeHint().height();
        if (QLayout *btnLay = d->live.livePromptButtonsWidget->layout()) {
            const QMargins mg = btnLay->contentsMargins();
            buttonsH += mg.top() + mg.bottom();
        }
        if (QLayout *rowLay = d->live.livePromptRowWidget->layout()) {
            const QMargins mg = rowLay->contentsMargins();
            rowH = qMax(rowH, buttonsH + mg.top() + mg.bottom());
        }
    }

    d->live.livePromptHostWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d->live.livePromptHostWidget->setFixedHeight(hostH);
    d->live.livePromptRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d->live.livePromptRowWidget->setFixedHeight(rowH);
    d->live.livePromptHostWidget->updateGeometry();
    d->live.livePromptRowWidget->updateGeometry();
}

} // namespace

using namespace ComfyHistoryInternal;

static QPixmap scaleLivePreviewPixmap(const QImage &image, const QSize &target)
{
    if (image.isNull() || !target.isValid())
        return QPixmap();
    const QSize drawTarget = target.expandedTo(QSize(128, 128));
    return QPixmap::fromImage(image.scaled(drawTarget, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ComfyUIRemoteDock::showLiveDockerPreview(const QImage &composition)
{
    if (!m_d->live.livePreviewArea || composition.isNull())
        return;
    const QSize target = m_d->live.livePreviewArea->size().expandedTo(QSize(128, 128));
    m_d->live.livePreviewArea->setPixmap(scaleLivePreviewPixmap(composition, target));
    m_d->live.livePreviewArea->setMinimumSize(128, 128);
    updateLiveToolbarState();
}

void ComfyUIRemoteDock::removeStaleLiveCanvasPreviewLayer()
{
    if (!m_d->viewManager || !m_d->viewManager->image())
        return;
    KisImageSP image = m_d->viewManager->image();
    KisGroupLayerSP root = image->rootLayer();
    if (!root)
        return;

    QList<KisLayerSP> toRemove;
    for (KisNodeSP child = root->firstChild(); child; child = child->nextSibling()) {
        if (KisLayerSP layer = qobject_cast<KisLayer *>(child.data())) {
            if (layer->name().startsWith(QLatin1String("[Preview] live")))
                toRemove.append(layer);
        }
    }
    if (toRemove.isEmpty())
        return;

    KisLayerSP active = m_d->viewManager->activeLayer();
    for (const KisLayerSP &layer : toRemove) {
        if (!layerStillInDocument(image, layer))
            continue;
        if (active && active.data() == layer.data()) {
            if (m_d->viewManager->nodeManager()) {
                if (KisNodeSP anchor = firstImportAnchorLayer(image, toRemove))
                    m_d->viewManager->nodeManager()->slotNonUiActivatedNode(anchor);
            }
            active = m_d->viewManager->activeLayer();
        }
        image->removeNode(layer);
    }
    image->waitForDone();
    if (!m_d->previewLayerId.isEmpty()) {
        KisLayerSP tracked = findPreviewLayerByUuidString(image, m_d->previewLayerId);
        if (!tracked || tracked->name().startsWith(QLatin1String("[Preview] live"))) {
            m_d->previewLayerId.clear();
            m_d->history.previewHistoryJobId.clear();
            m_d->history.previewHistoryImageIndex = -1;
            savePreviewLayerIdToDocument(QString());
        }
    }
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE removed stale canvas preview layers count=") << toRemove.size();
}

void ComfyUIRemoteDock::clearLiveDockerPreview()
{
    if (m_d->live.livePreviewArea)
        m_d->live.livePreviewArea->clear();
    removeStaleLiveCanvasPreviewLayer();
}

void ComfyUIRemoteDock::updateLiveToolbarState()
{
    const bool live = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
    const bool active = m_d->live.checkLiveMode && m_d->live.checkLiveMode->isChecked();
    const bool recording = m_d->live.checkLiveRecord && m_d->live.checkLiveRecord->isChecked();
    const bool hasResult = !m_d->liveRt.lastLiveResultImagePath.isEmpty()
                           && QFile::exists(m_d->liveRt.lastLiveResultImagePath);
    const bool editMode = m_d->generate.checkEditMode && m_d->generate.checkEditMode->isChecked();

    if (m_d->live.btnLivePlay) {
        m_d->live.btnLivePlay->setIcon(ComfyTheme::icon(active ? QStringLiteral("pause") : QStringLiteral("play")));
        m_d->live.btnLivePlay->setToolTip(active ? ComfyTr::tr("Stop live preview") : ComfyTr::tr("Start/stop live preview"));
    }
    if (m_d->live.btnLiveRecord) {
        m_d->live.btnLiveRecord->setIcon(
            ComfyTheme::icon(recording ? QStringLiteral("record-active") : QStringLiteral("record")));
    }
    if (m_d->live.btnLiveApply)
        m_d->live.btnLiveApply->setEnabled(live && hasResult);
    if (m_d->live.btnLiveApplyLayer)
        m_d->live.btnLiveApplyLayer->setEnabled(live && hasResult);
    if (m_d->live.btnLiveEditToggle) {
        m_d->live.btnLiveEditToggle->setIcon(
            ComfyTheme::icon(editMode ? QStringLiteral("workspace-generation") : QStringLiteral("edit")));
        m_d->live.btnLiveEditToggle->setToolTip(
            editMode ? ComfyTr::tr("Switch to generate mode") : ComfyTr::tr("Switch to edit mode"));
    }
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE toolbar state live=") << live << QStringLiteral("active=") << active
        << QStringLiteral("recording=") << recording << QStringLiteral("hasResult=") << hasResult
        << QStringLiteral("editMode=") << editMode;
}

void ComfyUIRemoteDock::ensureGenerateStrengthRowLayout()
{
    if (!m_d || !m_d->comboWorkspace || m_d->comboWorkspace->currentIndex() != 0)
        return;
    ComfyUiLayoutDiagnostics::ensureGenerateStrengthRowLayout(m_d.data());
}

void ComfyUIRemoteDock::syncLivePromptRowHeights()
{
    if (!m_d || !m_d->comboWorkspace || m_d->comboWorkspace->currentIndex() != 2)
        return;
    applyLivePromptRowHeights(m_d.data());
    if (m_d->generate.genContentContainer)
        m_d->generate.genContentContainer->updateGeometry();
}

void ComfyUIRemoteDock::updateLiveWorkspaceUi()
{
    const bool live = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;

    for (QToolButton *btn :
         {m_d->live.btnLivePlay, m_d->live.btnLiveRecord, m_d->live.btnLiveApply, m_d->live.btnLiveApplyLayer}) {
        if (btn)
            btn->setVisible(live);
    }

    if (m_d->live.liveParamsRowWidget)
        m_d->live.liveParamsRowWidget->setVisible(live);
    if (m_d->live.livePromptRowWidget)
        m_d->live.livePromptRowWidget->setVisible(live);
    if (m_d->live.livePreviewGroupBox)
        m_d->live.livePreviewGroupBox->setVisible(live);
    if (!live)
        clearLiveDockerPreview();
    if (m_d->inpaint.strengthRowWidget)
        m_d->inpaint.strengthRowWidget->setVisible(!live);
    if (m_d->progressBar)
        m_d->progressBar->setVisible(!live);

    auto *genLay = m_d->generate.genContentContainer
                       ? qobject_cast<QVBoxLayout *>(m_d->generate.genContentContainer->layout())
                       : nullptr;

    if (live) {
        if (m_d->live.liveParamsRowWidget)
            m_d->live.liveParamsRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_d->live.livePromptRowWidget)
            m_d->live.livePromptRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_d->live.livePromptHostWidget)
            m_d->live.livePromptHostWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        if (genLay && m_d->live.liveParamsRowWidget) {
            genLay->removeWidget(m_d->live.liveParamsRowWidget);
            genLay->insertWidget(0, m_d->live.liveParamsRowWidget);
        }
        if (genLay && m_d->live.livePromptRowWidget) {
            genLay->removeWidget(m_d->live.livePromptRowWidget);
            genLay->insertWidget(1, m_d->live.livePromptRowWidget);
        }
        if (genLay && m_d->generate.regionPromptWidget) {
            removeFromParentLayout(m_d->generate.regionPromptWidget);
            genLay->removeWidget(m_d->generate.regionPromptWidget);
        }
        if (m_d->live.livePromptHostWidget && m_d->generate.regionPromptWidget) {
            if (auto *hostLay = qobject_cast<QVBoxLayout *>(m_d->live.livePromptHostWidget->layout())) {
                removeFromParentLayout(m_d->generate.regionPromptWidget);
                hostLay->addWidget(m_d->generate.regionPromptWidget, 0, Qt::AlignTop);
                m_d->generate.regionPromptWidget->show();
                m_d->generate.regionPromptWidget->updateGeometry();
            }
        }
        if (auto *btnCol = m_d->live.livePromptButtonsWidget
                               ? qobject_cast<QVBoxLayout *>(m_d->live.livePromptButtonsWidget->layout())
                               : nullptr) {
            clearVBox(btnCol);
            removeFromParentLayout(m_d->generate.btnAddRegionIcon);
            removeFromParentLayout(m_d->generate.btnAddControlIcon);
            if (m_d->generate.btnAddRegionIcon)
                btnCol->addWidget(m_d->generate.btnAddRegionIcon);
            if (m_d->generate.btnAddControlIcon)
                btnCol->addWidget(m_d->generate.btnAddControlIcon);
        }

        if (auto *paramsLay = m_d->live.liveParamsRowWidget
                                  ? qobject_cast<QHBoxLayout *>(m_d->live.liveParamsRowWidget->layout())
                                  : nullptr) {
            clearHBox(paramsLay);
            removeFromParentLayout(m_d->inpaint.strengthSliderWidget);
            removeFromParentLayout(m_d->generate.spinStrength);
            removeFromParentLayout(m_d->generate.spinSeed);
            if (m_d->generate.spinStrength)
                m_d->generate.spinStrength->setPrefix(ComfyTr::tr("Strength") + QStringLiteral(": "));
            if (m_d->inpaint.strengthSliderWidget) {
                m_d->inpaint.strengthSliderWidget->setVisible(true);
                paramsLay->addWidget(m_d->inpaint.strengthSliderWidget, 1);
            }
            if (m_d->generate.spinStrength) {
                m_d->generate.spinStrength->setVisible(true);
                paramsLay->addWidget(m_d->generate.spinStrength);
            }
            if (m_d->generate.spinSeed) {
                m_d->generate.spinSeed->setPrefix(ComfyTr::tr("Seed") + QStringLiteral(": "));
                paramsLay->addWidget(m_d->generate.spinSeed);
            }
            if (m_d->live.btnLiveRandomSeed)
                paramsLay->addWidget(m_d->live.btnLiveRandomSeed);
            if (m_d->live.btnLiveEditToggle)
                paramsLay->addWidget(m_d->live.btnLiveEditToggle);
        }
        if (m_d->live.liveParamsRowWidget) {
            int minH = 28;
            if (m_d->generate.spinStrength)
                minH = qMax(minH, m_d->generate.spinStrength->sizeHint().height());
            if (m_d->inpaint.strengthSliderWidget)
                minH = qMax(minH, m_d->inpaint.strengthSliderWidget->sizeHint().height());
            m_d->live.liveParamsRowWidget->setMinimumHeight(minH);
            m_d->live.liveParamsRowWidget->updateGeometry();
        }
        if (m_d->live.livePromptRowWidget)
            m_d->live.livePromptRowWidget->updateGeometry();

        applyLiveRegionPromptLayout(m_d.data());
        applyLivePromptRowHeights(m_d.data());

        QWidget *contentPage = nullptr;
        if (m_d->history.histGroupBox)
            contentPage = m_d->history.histGroupBox->parentWidget();
        if (!contentPage && m_d->progressBar)
            contentPage = m_d->progressBar->parentWidget();
        if (contentPage)
            ComfyUiLayoutDiagnostics::restoreLivePreviewPanelLayout(m_d.data(), contentPage);
    } else {
        if (m_d->live.livePromptHostWidget && m_d->generate.regionPromptWidget) {
            if (auto *hostLay = qobject_cast<QVBoxLayout *>(m_d->live.livePromptHostWidget->layout()))
                hostLay->removeWidget(m_d->generate.regionPromptWidget);
        }
        if (genLay && m_d->generate.regionPromptWidget) {
            removeFromParentLayout(m_d->generate.regionPromptWidget);
            genLay->removeWidget(m_d->generate.regionPromptWidget);
            const bool onUpscale = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 1;
            if (!onUpscale)
                genLay->insertWidget(0, m_d->generate.regionPromptWidget);
        }

        ComfyUiLayoutDiagnostics::ensureGenerateStrengthRowLayout(m_d.data());

        if (auto *seedLay = m_d->generate.seedRowWidget
                                ? qobject_cast<QHBoxLayout *>(m_d->generate.seedRowWidget->layout())
                                : nullptr) {
            removeFromParentLayout(m_d->generate.spinSeed);
            if (m_d->generate.spinSeed) {
                m_d->generate.spinSeed->setPrefix(QString());
                if (seedLay->indexOf(m_d->generate.spinSeed) < 0)
                    seedLay->insertWidget(2, m_d->generate.spinSeed);
            }
        }
    }

    const bool onGenerate = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0;
    const bool onUpscale = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 1;
    syncCompactGenerateLayoutRows(onGenerate || onUpscale || live);
    updateLiveToolbarState();
}

ComfyPrepareLiveWorkflow::Input ComfyUIRemoteDock::prepareLiveWorkflowInput() const
{
    ComfyPrepareLiveWorkflow::Input prepIn;
    if (!m_d->viewManager)
        return prepIn;
    prepIn.image = m_d->viewManager->image();
    prepIn.viewManager = m_d->viewManager;
    prepIn.checkpoint = checkpointForGenerate();
    if (m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            prepIn.styleArch = st->architecture;
    }
    prepIn.rootPositivePrompt = ComfyUIUtils::stripPromptComments(m_d->generate.editPrompt->toPlainText()).trimmed();
    prepIn.strength0to1 = (m_d->generate.spinStrength ? m_d->generate.spinStrength->value() : 30) / 100.0;
    prepIn.editMode = m_d->generate.checkEditMode && m_d->generate.checkEditMode->isChecked();
    prepIn.activeRegions = comfyActiveRegionEntries(m_d.data());
    return prepIn;
}

void ComfyUIRemoteDock::slotLiveTick()
{
    ComfyLiveRunner::onTick(this);
}

void ComfyUIRemoteDock::beginLiveUploadPipeline()
{
    ComfyLiveRunner::beginUploadPipeline(this);
}

void ComfyUIRemoteDock::continueLiveAfterLoraUploads()
{
    ComfyLiveRunner::continueAfterLoraUploads(this);
}

void ComfyUIRemoteDock::buildLivePreparedPrompts(const quint32 liveSeed)
{
    ComfyLiveRunner::buildPreparedPrompts(this, liveSeed);
}

void ComfyUIRemoteDock::uploadLiveCanvasAndPrompt()
{
    ComfyLiveRunner::uploadCanvasAndPrompt(this);
}

void ComfyUIRemoteDock::continueLiveAfterCanvasUpload()
{
    ComfyLiveRunner::continueAfterCanvasUpload(this);
}

void ComfyUIRemoteDock::continueLiveAfterMaskUpload()
{
    ComfyLiveRunner::continueAfterMaskUpload(this);
}

void ComfyUIRemoteDock::uploadNextLiveRegionMask()
{
    ComfyLiveRunner::uploadNextRegionMask(this);
}

void ComfyUIRemoteDock::continueLiveAfterRegionMaskUpload()
{
    ComfyLiveRunner::continueAfterRegionMaskUpload(this);
}

void ComfyUIRemoteDock::finalizeLiveWorkflowAndSubmit(QJsonObject workflow)
{
    ComfyLiveRunner::finalizeWorkflowAndSubmit(this, workflow);
}

void ComfyUIRemoteDock::submitLiveWorkflow(const QJsonObject &workflow)
{
    ComfyLiveRunner::submitWorkflow(this, workflow);
}

void ComfyUIRemoteDock::slotLivePoll()
{
    ComfyLiveRunner::onPollTimer(this);
}
