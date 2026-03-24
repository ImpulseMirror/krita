/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyUIWorkflows.h"

#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUuid>
#include <QRandomGenerator>
#include <QFile>

#include <klocalizedstring.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KisViewManager.h>
#include <kis_image_manager.h>
#include <kis_types.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_image.h>
#include <KoColorSpaceConstants.h>

void ComfyUIRemoteDock::slotAddRegion()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    QStringList maskSources;
    maskSources << "selection";
    QString createdLayerName;  // §13.207: if we create a layer/group, use it as default mask source
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP image = m_d->viewManager->image();
        KisGroupLayerSP root = image->rootLayer();
        if (root && image->colorSpace()) {
            const int n = regs.size() + 1;
            const QString baseName = i18n("Region %1", n);
            const bool isLive = (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2);
            if (isLive) {
                // §13.207: Live workspace — group=false, single paint layer
                KisPaintLayerSP paintLayer = new KisPaintLayer(image, baseName, OPACITY_OPAQUE_U8);
                if (image->addNode(paintLayer, root, root->firstChild())) {
                    createdLayerName = baseName;
                }
            } else {
                // §13.207: Generate/Upscale/Animation/Graph — group=true, group + paint layer inside
                KisGroupLayerSP group = new KisGroupLayer(image, baseName, OPACITY_OPAQUE_U8, image->colorSpace());
                KisPaintLayerSP paintLayer = new KisPaintLayer(image, image->nextLayerName(i18n("Paint layer")), OPACITY_OPAQUE_U8);
                if (image->addNode(group, root, root->firstChild()) && image->addNode(paintLayer, group, KisNodeSP())) {
                    createdLayerName = baseName;
                }
            }
            if (!createdLayerName.isEmpty())
                maskSources << "layer:" + createdLayerName;
        }
        if (root) {
            QList<KisNodeSP> nodes;
            nodes.append(root);
            while (!nodes.isEmpty()) {
                KisNodeSP node = nodes.takeFirst();
                if (KisLayerSP layer = dynamic_cast<KisLayer*>(node.data())) {
                    if (!layer->name().isEmpty())
                        maskSources << "layer:" + layer->name();
                }
                for (int i = 0; i < static_cast<int>(node->childCount()); i++)
                    nodes.append(node->at(i));
            }
        }
    }
    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Add region"));
    QFormLayout *form = new QFormLayout(&dlg);
    QLineEdit *editName = new QLineEdit(createdLayerName);
    editName->setPlaceholderText(i18n("e.g. Background"));
    QLineEdit *editPrompt = new QLineEdit();
    editPrompt->setPlaceholderText(i18n("Prompt for this area"));
    QComboBox *comboMask = new QComboBox();
    comboMask->addItem(i18n("Current selection"), "selection");
    for (const QString &s : maskSources) {
        if (s == "selection") continue;
        comboMask->addItem(s, s);
    }
    if (!createdLayerName.isEmpty())
        comboMask->setCurrentIndex(comboMask->findData(QVariant(QStringLiteral("layer:") + createdLayerName)));
    form->addRow(i18n("Name:"), editName);
    form->addRow(i18n("Prompt:"), editPrompt);
    form->addRow(i18n("Mask source:"), comboMask);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted) return;
    Private::RegionEntry e;
    e.name = editName->text().trimmed().isEmpty() ? i18n("Region %1", regs.size() + 1) : editName->text().trimmed();
    e.prompt = editPrompt->text().trimmed();
    e.maskSource = comboMask->currentData().toString();
    regs.append(e);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(i18n("Added region \"%1\".", e.name));
}

void ComfyUIRemoteDock::slotRemoveRegion()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= regs.size()) return;
    QString name = regs.at(row).name;
    regs.removeAt(row);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(i18n("Removed region \"%1\".", name));
}

void ComfyUIRemoteDock::slotMoveRegionUp()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    int row = m_d->listRegions->currentRow();
    if (row <= 0 || row >= regs.size()) return;
    regs.move(row, row - 1);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->listRegions->setCurrentRow(row - 1);
}

void ComfyUIRemoteDock::slotMoveRegionDown()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= regs.size() - 1) return;
    regs.move(row, row + 1);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->listRegions->setCurrentRow(row + 1);
}

void ComfyUIRemoteDock::slotEditRegion()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    int row = m_d->listRegions->currentRow();
    if (row < 0 || row >= regs.size()) return;
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
    QLineEdit *editName = new QLineEdit(regs.at(row).name);
    QLineEdit *editPrompt = new QLineEdit(regs.at(row).prompt);
    QComboBox *comboMask = new QComboBox();
    comboMask->addItem(i18n("Current selection"), "selection");
    for (const QString &s : maskSources) {
        if (s == "selection") continue;
        comboMask->addItem(s, s);
    }
    int idx = comboMask->findData(regs.at(row).maskSource);
    if (idx >= 0) comboMask->setCurrentIndex(idx);
    else comboMask->setCurrentText(regs.at(row).maskSource);
    form->addRow(i18n("Name:"), editName);
    form->addRow(i18n("Prompt:"), editPrompt);
    form->addRow(i18n("Mask source:"), comboMask);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted) return;
    regs[row].name = editName->text().trimmed().isEmpty() ? regs[row].name : editName->text().trimmed();
    regs[row].prompt = editPrompt->text().trimmed();
    regs[row].maskSource = comboMask->currentData().toString();
    saveRegionsToConfig();
    refreshRegionsList();
}

void ComfyUIRemoteDock::slotGenerateRegions()
{
    if (comfyActiveRegionEntries(m_d.data()).isEmpty()) {
        setStatusMessage(i18n("Add at least one region (name, prompt, mask source)."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(i18n("Open a document first."), true);
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(i18n("Enter a server URL."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(i18n("Invalid URL."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }
    m_d->regionCurrentImage = ComfyUIUtils::getCanvasAsQImage(image);
    if (m_d->regionCurrentImage.isNull()) {
        setStatusMessage(i18n("Could not export canvas."), true);
        return;
    }
    m_d->regionCurrentImage = m_d->regionCurrentImage.convertToFormat(QImage::Format_ARGB32);
    m_d->regionGenerationSnapshot = comfyActiveRegionEntries(m_d.data());
    m_d->regionJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        setStatusMessage(i18n("Could not save temp image."), true);
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
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(i18n("Uploading canvas…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: upload finished
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Upload error: %1", reply->errorString()), true);
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        m_d->regionUploadedImageName = obj.value("name").toString();
        m_d->regionUploadedImageSubfolder = obj.value("subfolder").toString();
        if (m_d->regionUploadedImageName.isEmpty()) {
            setStatusMessage(i18n("Server did not return image name."), true);
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

    if (m_d->regionIndex >= m_d->regionGenerationSnapshot.size()) {
        if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
            m_d->labelStatus->setText(i18n("Regions done (no document to paste)."));
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        QTemporaryFile tmp;
        tmp.setFileTemplate(tmp.fileTemplate() + ".png");
        if (!tmp.open() || !m_d->regionCurrentImage.save(tmp.fileName())) {
            setStatusMessage(i18n("Could not save result."), true);
            m_d->btnGenerateRegions->setEnabled(true);
            return;
        }
        tmp.close();
        // §13.184: Add region result to history with regionLayerNames so Apply can use create_result_layer
        QStringList layerNames;
        for (const Private::RegionEntry &re : m_d->regionGenerationSnapshot) {
            if (re.maskSource.startsWith(QStringLiteral("layer:")))
                layerNames.append(re.maskSource.mid(6));
        }
        if (!layerNames.isEmpty() && !m_d->regionJobId.isEmpty()) {
            const QString cachePath = ComfyUIUtils::historyCacheDir() + QLatin1Char('/') + m_d->regionJobId + QStringLiteral(".png");
            if (QFile::copy(tmp.fileName(), cachePath)) {
                Private::HistoryEntry entry;
                entry.jobId = m_d->regionJobId;
                entry.resultImagePath = cachePath;
                entry.resultImagePaths = QStringList() << cachePath;
                entry.regionLayerNames = layerNames;
                entry.prompt = m_d->regionGenerationSnapshot.isEmpty() ? QString() : m_d->regionGenerationSnapshot.first().prompt;
                if (m_d->editNegative)
                    entry.negative = m_d->editNegative->toPlainText();
                if (m_d->spinWidth)
                    entry.width = m_d->spinWidth->value();
                if (m_d->spinHeight)
                    entry.height = m_d->spinHeight->value();
                if (m_d->spinSteps)
                    entry.steps = m_d->spinSteps->value();
                if (m_d->spinCfg)
                    entry.cfg = m_d->spinCfg->value();
                if (m_d->spinStrength)
                    entry.strength = m_d->spinStrength->value();
                if (m_d->comboSampler)
                    entry.samplerName = m_d->comboSampler->currentText();
                if (m_d->spinSeed)
                    entry.seed = m_d->spinSeed->value();
                if (m_d->comboCheckpoint)
                    entry.checkpoint = m_d->comboCheckpoint->currentText();
                if (m_d->comboPreset)
                    entry.styleName = m_d->comboPreset->currentText();
                m_d->historyEntries.prepend(entry);
                while (m_d->historyEntries.size() > Private::maxHistoryEntries) {
                    Private::HistoryEntry old = m_d->historyEntries.takeLast();
                    evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                    QStringList paths = old.resultImagePaths;
                    if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                    for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
                }
                pruneHistoryToStorageLimit();
                persistTopHistoryEntryToDocument(false);
                refreshHistoryList();
            }
        }
        qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
        if (n > 0 && m_d->canvas) m_d->canvas->updateCanvas();
        m_d->labelStatus->setText(i18n("Regions done. Result added as new layer."));
        m_d->btnGenerateRegions->setEnabled(true);
        return;
    }

    const Private::RegionEntry &region = m_d->regionGenerationSnapshot.at(m_d->regionIndex);
    KisImageSP image = m_d->viewManager->image();
    QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, m_d->viewManager, region.maskSource);
    if (maskImg.isNull()) {
        setStatusMessage(i18n("Region \"%1\": could not get mask.", region.name), true);
        m_d->regionIndex++;
        QTimer::singleShot(0, this, &ComfyUIRemoteDock::runNextRegionInpainting);
        return;
    }
    m_d->labelStatus->setText(i18n("Region %1/%2: %3…", m_d->regionIndex + 1, m_d->regionGenerationSnapshot.size(), region.name));

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
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Mask upload error: %1", reply->errorString()), true);
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
        const Private::RegionEntry &r = m_d->regionGenerationSnapshot.at(m_d->regionIndex);
        QJsonParseError err;
        QJsonObject workflow = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err).object();
        if (err.error != QJsonParseError::NoError) {
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
            m_d->checkEditMode && m_d->checkEditMode->isChecked(),
            m_d->comboCheckpoint->currentText().trimmed(),
            m_d->spinSteps->value(),
            m_d->spinCfg->value(),
            (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0,
            m_d->comboSampler->currentText().trimmed(),
            m_d->ksamplerScheduler);
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
        // §13.43 / §13.127: grow from calc_selection_pre_process (extent diagonal, strength, settings); clamp 0–499
        const int extentW = m_d->regionCurrentImage.width();
        const int extentH = m_d->regionCurrentImage.height();
        const double strength0to1 = (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0;
        int selFeather = 50;
        double selMinTransition = 0.0;
        int selGrowOffset = 0;
        ComfyUIUtils::getSelectionModifierSettings(&selFeather, &selMinTransition, &selGrowOffset);
        const int regionGrow = ComfyUIUtils::calcSelectionPreProcessGrow(extentW, extentH, 0, 0, strength0to1, selFeather, selMinTransition, selGrowOffset);
        QJsonObject n7 = workflow["7"].toObject();
        QJsonObject i7 = n7["inputs"].toObject();
        i7["grow_mask_by"] = ComfyUIUtils::clampInpaintGrowFeather(qMax(6, regionGrow));
        n7["inputs"] = i7;
        workflow["7"] = n7;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        i4["ckpt_name"] = link.checkpoint.isEmpty() ? QString("v1-5-pruned-emaonly.safetensors") : link.checkpoint;
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        quint32 rngSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        QString regionPrompt = ComfyUIUtils::stripPromptComments(r.prompt).trimmed();
        if (link.active)
            regionPrompt = ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, regionPrompt).trimmed();
        regionPrompt = ComfyUIUtils::evalWildcards(regionPrompt, rngSeed);
        ComfyUIUtils::extractLayerPlaceholders(regionPrompt);  // §13.35: <layer:name> → "Picture {n}"
        regionPrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(regionPrompt);
        i5["text"] = regionPrompt.isEmpty() ? QString("a beautiful painting") : regionPrompt;
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        {
            const QString negSrc = link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed();
            i6["text"] = ComfyUIUtils::evalWildcards(negSrc, rngSeed);
        }
        n6["inputs"] = i6;
        workflow["6"] = n6;
        QJsonObject n8 = workflow["8"].toObject();
        QJsonObject i8 = n8["inputs"].toObject();
        i8["seed"] = static_cast<double>(rngSeed);
        i8["denoise"] = link.denoise;
        i8["steps"] = link.steps;
        i8["cfg"] = link.cfg;
        i8["sampler_name"] = link.sampler;
        i8["scheduler"] = link.scheduler;
        n8["inputs"] = i8;
        workflow["8"] = n8;
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (m_d->clientId.isEmpty())
            m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject payload;
        payload["prompt"] = workflow;
        payload["client_id"] = m_d->clientId;
        payload["prompt_id"] = expectedPromptId;
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
        else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
        else promptUrl.setPath(p + "prompt");
        QNetworkRequest reqPrompt(promptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqPrompt);
        reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId]() {
            replyPrompt->deleteLater();
            QByteArray body = replyPrompt->readAll();
            if (replyPrompt->error() != QNetworkReply::NoError) {
                QJsonObject obj = QJsonDocument::fromJson(body).object();
                if (obj.contains("error"))
                    setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                else
                    setStatusMessage(i18n("Submit error: %1", replyPrompt->errorString()), true);
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (obj.contains("error")) {
                setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            QString promptId = obj["prompt_id"].toString();
            if (promptId.isEmpty()) {
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                m_d->regionIndex++;
                runNextRegionInpainting();
                return;
            }
            m_d->regionPromptId = promptId;
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
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("History error: %1", reply->errorString()), true);
            m_d->regionIndex++;
            runNextRegionInpainting();
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->regionPromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->regionPollCount++;
            if (m_d->regionPollCount >= Private::regionMaxPollCount) {
                setStatusMessage(i18n("Region generation timed out."), true);
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
        ComfyUIUtils::setComfyUIRequestHeaders(reqView);
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
                QImage maskImg = ComfyUIUtils::getMaskAsQImage(m_d->viewManager->image(), m_d->viewManager, m_d->regionGenerationSnapshot.at(m_d->regionIndex).maskSource);
                if (!maskImg.isNull())
                    ComfyUIUtils::compositeWithMask(m_d->regionCurrentImage, result.convertToFormat(QImage::Format_ARGB32), maskImg);
            }
            m_d->regionPromptId.clear();
            m_d->regionIndex++;
            runNextRegionInpainting();
        });
    });
}
