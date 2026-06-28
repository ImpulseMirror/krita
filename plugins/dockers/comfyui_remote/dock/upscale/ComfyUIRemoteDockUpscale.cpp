/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyUpscaleRunner.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSignalBlocker>

#include <kis_filter_strategy.h>
#include <kis_image.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace {

QString upscalerDisplayName(const QString &filename)
{
    const QString base = filename;
    if (base == QStringLiteral("4x_NMKD-Superscale-SP_178000_G.pth"))
        return ComfyTr::tr("Default (%1)", QStringLiteral("4x_NMKD-Superscale-SP_178000_G"));
    if (base.endsWith(QStringLiteral("4x-UltraSharp.pth")))
        return ComfyTr::tr("Sharp (%1)", QStringLiteral("4x-UltraSharp"));
    if (base.contains(QStringLiteral("RealESRGAN"), Qt::CaseInsensitive))
        return ComfyTr::tr("Quality (%1)", base.left(base.length() - 4));
    if (base.endsWith(QStringLiteral(".safetensors")))
        return ComfyTr::tr("Fast (%1)", base.left(base.length() - 12));
    return filename;
}

int upscalerSortOrder(const QString &filename)
{
    if (filename == QStringLiteral("4x_NMKD-Superscale-SP_178000_G.pth"))
        return 0;
    if (filename.endsWith(QStringLiteral(".safetensors")))
        return 1;
    if (filename.contains(QStringLiteral("RealESRGAN"), Qt::CaseInsensitive))
        return 2;
    if (filename.endsWith(QStringLiteral("4x-UltraSharp.pth")))
        return 3;
    return 99;
}

} // namespace

void ComfyUIRemoteDock::slotUpscale()
{
    ComfyUpscaleRunner::onUpscale(this);
}

void ComfyUIRemoteDock::continueUpscaleAfterCanvasUpload(int canvasW, int canvasH, int w2, int h2)
{
    ComfyUpscaleRunner::continueAfterCanvasUpload(this, canvasW, canvasH, w2, h2);
}

void ComfyUIRemoteDock::beginUpscaleConditioningUploadPipeline()
{
    ComfyUpscaleRunner::beginConditioningUploadPipeline(this);
}

void ComfyUIRemoteDock::uploadNextUpscaleRegionMask()
{
    ComfyUpscaleRunner::uploadNextRegionMask(this);
}

void ComfyUIRemoteDock::finalizeUpscaleWorkflowAndSubmit()
{
    ComfyUpscaleRunner::finalizeWorkflowAndSubmit(this);
}

void ComfyUIRemoteDock::submitUpscaleWorkflow(const QJsonObject &workflow, bool wantRefine, bool useTiledRefine)
{
    ComfyUpscaleRunner::submitWorkflow(this, workflow, wantRefine, useTiledRefine);
}

void ComfyUIRemoteDock::slotUpscalePoll()
{
    ComfyUpscaleRunner::onPollTimer(this);
}

QString ComfyUIRemoteDock::selectedUpscalerModelName() const
{
    if (!m_d->upscale.comboUpscaleModel || m_d->upscale.comboUpscaleModel->currentIndex() < 0)
        return m_d->upscaleRt.upscalerModel;
    const QString data = m_d->upscale.comboUpscaleModel->currentData().toString();
    if (!data.isEmpty())
        return data;
    return m_d->upscale.comboUpscaleModel->currentText();
}

void ComfyUIRemoteDock::refreshUpscaleModelCombo(const QStringList &serverModels)
{
    if (!m_d->upscale.comboUpscaleModel)
        return;

    QStringList models = serverModels;
    if (models.isEmpty()) {
        models << QStringLiteral("4x_NMKD-Superscale-SP_178000_G.pth");
    }
    models.removeDuplicates();
    std::sort(models.begin(), models.end(), [](const QString &a, const QString &b) {
        const int oa = upscalerSortOrder(a);
        const int ob = upscalerSortOrder(b);
        if (oa != ob)
            return oa < ob;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    const QString prev = selectedUpscalerModelName();
    QSignalBlocker blocker(m_d->upscale.comboUpscaleModel);
    m_d->upscale.comboUpscaleModel->clear();
    for (const QString &file : models) {
        m_d->upscale.comboUpscaleModel->addItem(upscalerDisplayName(file), file);
    }
    int idx = m_d->upscale.comboUpscaleModel->findData(prev);
    if (idx < 0)
        idx = 0;
    m_d->upscale.comboUpscaleModel->setCurrentIndex(idx);
    m_d->upscaleRt.upscalerModel = m_d->upscale.comboUpscaleModel->currentData().toString();
}

void ComfyUIRemoteDock::updateUpscaleUsePromptLabel()
{
    if (!m_d->upscale.labelUpscaleUsePromptText)
        return;
    QString text;
    if (m_d->generate.editPrompt) {
        text = ComfyUIUtils::stripPromptComments(m_d->generate.editPrompt->toPlainText()).trimmed();
        if (text.length() > 48)
            text = text.left(45) + QStringLiteral("…");
    }
    if (text.isEmpty())
        text = ComfyTr::tr("(no prompt)");
    m_d->upscale.labelUpscaleUsePromptText->setText(text);
}

void ComfyUIRemoteDock::syncUpscaleRefineControlsEnabled(bool enabled)
{
    if (m_d->upscale.upscaleRefineDetails)
        m_d->upscale.upscaleRefineDetails->setEnabled(enabled);
}

void ComfyUIRemoteDock::scaleDocumentForUpscale(int targetW, int targetH)
{
    if (!m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return;

    const QSize cur = image->size();
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("UPSCALE_DIAG scaleDocumentForUpscale current=") << cur.width() << cur.height()
        << QStringLiteral("target=") << targetW << targetH;
    if (cur.width() == targetW && cur.height() == targetH)
        return;

    KisFilterStrategy *strategy = KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bilinear"));
    if (!strategy)
        return;
    image->scaleImage(QSize(targetW, targetH), image->xRes(), image->yRes(), strategy);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    updateUpscaleTargetSize();
}
