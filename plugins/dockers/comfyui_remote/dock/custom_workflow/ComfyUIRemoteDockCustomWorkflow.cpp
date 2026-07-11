/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyWorkflowEngine.h"
#include "ComfyTheme.h"
#include "ComfySpinBox.h"
#include "ComfyUiStyle.h"
#include "ComfyDockUiBuilder.h"

#include <QTimer>
#include <QJsonDocument>
#include <QJsonParseError>

#include <kis_image.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_annotation.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

namespace {

QString comfyCustomWorkflowStorageKey(const ComfyUIUtils::CustomWorkflowParamSlot &sl)
{
    using Kind = ComfyUIUtils::CustomWorkflowParamSlot::Kind;
    if (sl.kind == Kind::KritaImageLayer || sl.kind == Kind::KritaMaskLayer)
        return sl.nodeId;
    return sl.paramName;
}

} // namespace

void ComfyUIRemoteDock::persistOpenCustomWorkflowToDocument()
{
    saveEmbeddedCustomWorkflowToDocument();
}
void ComfyUIRemoteDock::saveEmbeddedCustomWorkflowToDocument()
{
    if (!m_d->canvas || !m_d->editCustomWorkflow)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    const QString key = ComfyUIUtils::customWorkflowAnnotationKey();
    const QString text = m_d->editCustomWorkflow->toPlainText();
    if (text.trimmed().isEmpty()) {
        img->removeAnnotation(key);
        return;
    }
    img->removeAnnotation(key);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(
        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("custom_workflow")), text.toUtf8())));
}
void ComfyUIRemoteDock::loadEmbeddedCustomWorkflowFromDocument()
{
    if (!m_d->canvas || !m_d->editCustomWorkflow)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    m_d->customWorkflowParamOverrides.clear();
    QByteArray wfBytes;
    if (KisAnnotationSP ann = img->annotation(ComfyUIUtils::customWorkflowAnnotationKey())) {
        if (!ann->annotation().isEmpty())
            wfBytes = ann->annotation();
    }
    if (wfBytes.isEmpty()) {
        const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
        const QJsonObject cust = ui.value(QStringLiteral("custom")).toObject();
        const QJsonObject wfObj = cust.value(QStringLiteral("workflow")).toObject();
        if (!wfObj.isEmpty())
            wfBytes = QJsonDocument(wfObj).toJson(QJsonDocument::Compact);
        else
            wfBytes = cust.value(QStringLiteral("workflow_text")).toString().toUtf8();
        const QJsonObject pr = cust.value(QStringLiteral("params")).toObject();
        for (auto it = pr.begin(); it != pr.end(); ++it)
            m_d->customWorkflowParamOverrides.insert(it.key(), it.value().toVariant());
    }
    if (wfBytes.isEmpty())
        return;
    QSignalBlocker b(m_d->editCustomWorkflow);
    m_d->editCustomWorkflow->setPlainText(QString::fromUtf8(wfBytes));
    refreshCustomWorkflowParameterPanel();
}
bool ComfyUIRemoteDock::tryResolveCustomWorkflowInPlace(QJsonObject *workflow)
{
    if (!workflow)
        return false;
    QString err;
    if (!ComfyUIUtils::tryResolveCustomWorkflowJsonToApi(workflow, m_d->lastObjectInfoRoot, &err)) {
        setStatusMessage(err, true);
        return false;
    }
    return true;
}
void ComfyUIRemoteDock::reparentCustomWorkflowEditor(bool toGraphWorkspace)
{
    if (!m_d->editCustomWorkflow)
        return;
    // Graph keeps the editor as hidden storage (import/library fill it); no paste UI.
    m_d->editCustomWorkflow->hide();
    m_d->editCustomWorkflow->setMaximumHeight(0);
    if (toGraphWorkspace)
        refreshCustomWorkflowParameterPanel();
}
void ComfyUIRemoteDock::refreshCustomWorkflowParameterPanel()
{
    if (!m_d->customWorkflowParamsForm || !m_d->customWorkflowParamsGroup || !m_d->editCustomWorkflow)
        return;

    while (QLayoutItem *item = m_d->customWorkflowParamsForm->takeAt(0)) {
        if (item->widget())
            delete item->widget();
        delete item;
    }

    const QString json = m_d->editCustomWorkflow->toPlainText().trimmed();
    if (json.isEmpty()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(json.toUtf8()), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    QJsonObject wfObj = doc.object();
    QString convErr;
    if (!ComfyUIUtils::tryResolveCustomWorkflowJsonToApi(&wfObj, m_d->lastObjectInfoRoot, &convErr)) {
        Q_UNUSED(convErr);
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    const QList<ComfyUIUtils::CustomWorkflowParamSlot> slots =
        ComfyUIUtils::discoverCustomWorkflowParameterSlots(wfObj);
    if (slots.isEmpty()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }

    QSet<QString> storageKeysInUse;
    for (const auto &s : slots)
        storageKeysInUse.insert(comfyCustomWorkflowStorageKey(s));
    QStringList stale;
    for (auto it = m_d->customWorkflowParamOverrides.constBegin(); it != m_d->customWorkflowParamOverrides.constEnd(); ++it) {
        if (!storageKeysInUse.contains(it.key()))
            stale.append(it.key());
    }
    for (const QString &k : stale)
        m_d->customWorkflowParamOverrides.remove(k);

    m_d->customWorkflowParamsGroup->setVisible(true);
    m_d->customWorkflowParamsGroup->setTitle(ComfyTr::tr("Workflow parameters (ETN)"));

    for (const ComfyUIUtils::CustomWorkflowParamSlot &sl : slots) {
        const QString storageKey = comfyCustomWorkflowStorageKey(sl);
        const QVariant cur = m_d->customWorkflowParamOverrides.contains(storageKey)
            ? m_d->customWorkflowParamOverrides.value(storageKey)
            : sl.defaultValue;
        QString labelText = sl.paramName;
        if (!sl.typeStr.isEmpty() && sl.kind != ComfyUIUtils::CustomWorkflowParamSlot::Kind::Unsupported)
            labelText = QStringLiteral("%1 [%2]").arg(sl.paramName, sl.typeStr);

        switch (sl.kind) {
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterInt: {
            auto *sp = new ComfySpinBox(m_d->customWorkflowParamsGroup);
            int lo = static_cast<int>(qBound(-2147483648.0, sl.minV, 2147483647.0));
            int hi = static_cast<int>(qBound(-2147483648.0, sl.maxV, 2147483647.0));
            if (lo > hi)
                std::swap(lo, hi);
            sp->setRange(lo, hi);
            {
                bool ok = false;
                int v = cur.toInt(&ok);
                sp->setValue(ok ? v : sl.defaultValue.toInt());
            }
            const QString sk = storageKey;
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, sk](int v) {
                m_d->customWorkflowParamOverrides.insert(sk, v);
            });
            m_d->customWorkflowParamOverrides.insert(sk, sp->value());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), sp);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterFloat: {
            auto *sp = new QDoubleSpinBox(m_d->customWorkflowParamsGroup);
            sp->setDecimals(4);
            sp->setRange(sl.minV, sl.maxV);
            {
                bool ok = false;
                double v = cur.toDouble(&ok);
                sp->setValue(ok ? v : sl.defaultValue.toDouble());
            }
            const QString sk = storageKey;
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, sk](double v) {
                m_d->customWorkflowParamOverrides.insert(sk, v);
            });
            m_d->customWorkflowParamOverrides.insert(sk, sp->value());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), sp);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterBool: {
            auto *cb = new ComfyCheckBox(m_d->customWorkflowParamsGroup);
            bool chk = sl.defaultValue.toBool();
            if (!cur.isNull())
                chk = cur.toBool();
            cb->setChecked(chk);
            const QString sk = storageKey;
            connect(cb, &QCheckBox::toggled, this, [this, sk](bool on) {
                m_d->customWorkflowParamOverrides.insert(sk, on);
            });
            m_d->customWorkflowParamOverrides.insert(sk, cb->isChecked());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterText:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterPromptPositive:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterPromptNegative: {
            auto *le = new QLineEdit(m_d->customWorkflowParamsGroup);
            le->setText(cur.isNull() ? sl.defaultValue.toString() : cur.toString());
            const QString sk = storageKey;
            connect(le, &QLineEdit::editingFinished, this, [this, sk, le]() {
                m_d->customWorkflowParamOverrides.insert(sk, le->text());
            });
            m_d->customWorkflowParamOverrides.insert(sk, le->text());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), le);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterChoice: {
            const QString sk = storageKey;
            if (!sl.choices.isEmpty()) {
                auto *cb = new ComfyComboBox(m_d->customWorkflowParamsGroup);
                for (const QString &ch : sl.choices)
                    cb->addItem(ch, ch);
                const QString pick = cur.isNull() ? sl.defaultValue.toString() : cur.toString();
                int ix = cb->findData(pick);
                if (ix < 0)
                    ix = 0;
                cb->setCurrentIndex(ix);
                connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                    m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
                });
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
                m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            } else {
                auto *le = new QLineEdit(m_d->customWorkflowParamsGroup);
                le->setText(cur.isNull() ? sl.defaultValue.toString() : cur.toString());
                connect(le, &QLineEdit::editingFinished, this, [this, sk, le]() {
                    m_d->customWorkflowParamOverrides.insert(sk, le->text());
                });
                m_d->customWorkflowParamOverrides.insert(sk, le->text());
                m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), le);
            }
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaStylePicker: {
            auto *cb = new ComfyComboBox(m_d->customWorkflowParamsGroup);
            cb->setMinimumContentsLength(22);
            cb->addItem(ComfyTr::tr("(Dock style)"), QString());
            for (const ComfyStyleEntry &st : ComfyStyleCollection::instance().all())
                cb->addItem(ComfyStyleCollection::comboPresetName(st), st.styleId);
            QString wantId = cur.toString();
            if (wantId.isEmpty() && m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0)
                wantId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
            int selIx = cb->findData(wantId);
            if (selIx < 0)
                selIx = 0;
            cb->setCurrentIndex(selIx);
            const QString sk = storageKey;
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            });
            m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaImageLayer:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaMaskLayer: {
            labelText = QStringLiteral("%1 [%2]").arg(
                sl.paramName,
                sl.kind == ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaMaskLayer
                    ? QStringLiteral("mask layer")
                    : QStringLiteral("image layer"));
            auto *cb = new ComfyComboBox(m_d->customWorkflowParamsGroup);
            cb->setMinimumContentsLength(18);
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
            QVector<QPair<QString, QString>> items;
            KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
            if (img && img->rootLayer()) {
                if (sl.kind == ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaMaskLayer)
                    collectInpaintContextMaskLayerNodes(img->rootLayer(), &items);
                else
                    collectPaintLayerNodes(img->rootLayer(), &items);
            }
            for (const QPair<QString, QString> &p : items)
                cb->addItem(p.second, p.first);
            QString wantUuid = cur.toString();
            if (wantUuid.isEmpty() && !items.isEmpty())
                wantUuid = items.first().first;
            int selIx = cb->findData(wantUuid);
            if (selIx < 0 && cb->count() > 0)
                selIx = 0;
            cb->setCurrentIndex(selIx);
            const QString sk = storageKey;
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            });
            if (cb->count() > 0)
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::Unsupported:
        default: {
            auto *lb = new QLabel(
                ComfyTr::tr("Unsupported parameter type: %1", sl.paramName), m_d->customWorkflowParamsGroup);
            lb->setWordWrap(true);
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), lb);
            break;
        }
        }
    }
}
void ComfyUIRemoteDock::scheduleSaveEmbeddedCustomWorkflowToDocument()
{
    if (!m_d->customWorkflowDocumentSaveTimer)
        return;
    m_d->customWorkflowDocumentSaveTimer->start(800);
}
