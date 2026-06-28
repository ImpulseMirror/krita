/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyGenerateUi.h"

#include <QFile>
#include <QPixmap>
#include <QTimer>

#include <kis_image.h>
#include <kis_paint_layer.h>
#include <kis_group_layer.h>
#include <kis_image_animation_interface.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

void ComfyUIRemoteDock::refreshAnimationTargetLayerLivePreview()
{
    if (!m_d->labelAnimationPreview)
        return;
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
    if (ws != 3 || !m_d->radioSingleFrame || !m_d->radioSingleFrame->isChecked())
        return;
    KisImageSP image = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!image || !image->rootLayer()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    const QString uuid =
        m_d->comboAnimationTargetLayer ? m_d->comboAnimationTargetLayer->currentData().toString().trimmed() : QString();
    if (uuid.isEmpty()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    KisPaintLayer *pl = findPaintLayerByUuidInTree(image->rootLayer(), uuid);
    if (!pl || !pl->projection()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    QRect bounds = pl->exactBounds() & image->bounds();
    if (bounds.isEmpty()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    QImage img = pl->projection()->convertToQImage(profile, bounds,
                                                  KoColorConversionTransformation::internalRenderingIntent(),
                                                  KoColorConversionTransformation::internalConversionFlags());
    if (img.isNull()) {
        m_d->labelAnimationPreview->clear();
        return;
    }
    QPixmap pm = QPixmap::fromImage(img);
    const int maxW = 280;
    if (pm.width() > maxW)
        pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
    m_d->labelAnimationPreview->setPixmap(pm);
}
void ComfyUIRemoteDock::updateAnimationButtonLabel()
{
    if (!m_d->generate.btnGenerateAnimation) return;
    const bool fullAnimation = m_d->radioFullAnimation && m_d->radioFullAnimation->isChecked();
    if (fullAnimation) {
        m_d->generate.btnGenerateAnimation->setText(ComfyTr::tr("Generate Animation"));
        m_d->generate.btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate multiple frames with sequential seeds as new layers."));
    } else {
        m_d->generate.btnGenerateAnimation->setText(ComfyTr::tr("Generate Frame"));
        m_d->generate.btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate a single frame at current time."));
    }
}
void ComfyUIRemoteDock::refreshAnimationTargetLayerCombo()
{
    if (!m_d->comboAnimationTargetLayer)
        return;
    const QString prev = m_d->comboAnimationTargetLayer->currentData().toString();
    m_d->comboAnimationTargetLayer->blockSignals(true);
    m_d->comboAnimationTargetLayer->clear();
    m_d->comboAnimationTargetLayer->addItem(ComfyTr::tr("(None)"), QString());
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (img) {
        QVector<QPair<QString, QString>> items;
        KisNodeSP root = img->rootLayer();
        if (root)
            collectPaintLayerNodes(root, &items);
        std::sort(items.begin(), items.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
            return QString::localeAwareCompare(a.second, b.second) < 0;
        });
        int selectAt = 0;
        for (int i = 0; i < items.size(); ++i) {
            m_d->comboAnimationTargetLayer->addItem(ComfyTr::tr("Target layer: %1", items.at(i).second), items.at(i).first);
            if (items.at(i).first == prev)
                selectAt = i + 1;
        }
        m_d->comboAnimationTargetLayer->setCurrentIndex(selectAt);
    } else {
        m_d->comboAnimationTargetLayer->setCurrentIndex(0);
    }
    m_d->comboAnimationTargetLayer->blockSignals(false);
}
void ComfyUIRemoteDock::updateAnimationTargetLayerRowVisibility()
{
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
    const bool animWorkspace = (ws == 3);
    const bool singleFrame = m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked();
    const bool show = animWorkspace && singleFrame;
    if (m_d->animationTargetRow)
        m_d->animationTargetRow->setVisible(show);
    if (m_d->animationPreviewRow) {
        m_d->animationPreviewRow->setVisible(show);
        if (!show)
            updateAnimationResultPreview(QString());
        else if (m_d->animationPreviewDebounce) {
            m_d->animationPreviewDebounce->stop();
            m_d->animationPreviewDebounce->start();
        }
    }
}
void ComfyUIRemoteDock::updateAnimationResultPreview(const QString &imagePath)
{
    if (!m_d->labelAnimationPreview)
        return;
    if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    QPixmap pm;
    if (!pm.load(imagePath)) {
        m_d->labelAnimationPreview->clear();
        return;
    }
    const int maxW = 280;
    if (pm.width() > maxW)
        pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
    m_d->labelAnimationPreview->setPixmap(pm);
}
void ComfyUIRemoteDock::loadAnimationWorkspaceFromDocument()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img) {
        refreshAnimationTargetLayerCombo();
        updateAnimationTargetLayerRowVisibility();
        return;
    }
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const QJsonObject anim = ui.value(QStringLiteral("animation")).toObject();
    if (anim.isEmpty()) {
        refreshAnimationTargetLayerCombo();
        updateAnimationTargetLayerRowVisibility();
        return;
    }
    QSignalBlocker bFull(m_d->radioFullAnimation);
    QSignalBlocker bSingle(m_d->radioSingleFrame);
    QSignalBlocker bQual(m_d->generate.comboQuality);
    QSignalBlocker bTarget(m_d->comboAnimationTargetLayer);

    if (anim.contains(QStringLiteral("batch_mode"))) {
        const bool full = anim.value(QStringLiteral("batch_mode")).toBool();
        if (m_d->radioFullAnimation)
            m_d->radioFullAnimation->setChecked(full);
        if (m_d->radioSingleFrame)
            m_d->radioSingleFrame->setChecked(!full);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("FullAnimation", full);
    }
    const QString sq = anim.value(QStringLiteral("sampling_quality")).toString();
    if (!sq.isEmpty() && m_d->generate.comboQuality) {
        const int idx = (sq == QStringLiteral("fast")) ? 0 : 1;
        m_d->generate.comboQuality->setCurrentIndex(idx);
    }
    refreshAnimationTargetLayerCombo();
    const QString tl = anim.value(QStringLiteral("target_layer")).toString();
    if (m_d->comboAnimationTargetLayer && !tl.isEmpty()) {
        const int ix = m_d->comboAnimationTargetLayer->findData(tl);
        if (ix >= 0)
            m_d->comboAnimationTargetLayer->setCurrentIndex(ix);
    }
    updateAnimationButtonLabel();
    updateAnimationTargetLayerRowVisibility();
}

QJsonObject ComfyUIRemoteDock::animationWorkspaceToJson() const
{
    QJsonObject o;
    if (m_d->radioFullAnimation)
        o.insert(QStringLiteral("batch_mode"), m_d->radioFullAnimation->isChecked());
    if (m_d->generate.comboQuality) {
        const QString sq = (m_d->generate.comboQuality->currentIndex() == 0) ? QStringLiteral("fast") : QStringLiteral("quality");
        o.insert(QStringLiteral("sampling_quality"), sq);
    }
    if (m_d->comboAnimationTargetLayer) {
        const QString id = m_d->comboAnimationTargetLayer->currentData().toString();
        if (!id.isEmpty())
            o.insert(QStringLiteral("target_layer"), id);
    }
    return o;
}
