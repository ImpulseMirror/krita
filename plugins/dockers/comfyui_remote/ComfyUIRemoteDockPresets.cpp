/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <thread>

#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QRandomGenerator>

void ComfyUIRemoteDock::slotPresetChanged(int index)
{
    const int firstCustom = firstCustomPresetIndex();
    m_d->btnDeletePreset->setEnabled(index >= firstCustom);
    updateNegativePromptAlertVisibility();  // §13.143: update alert when style changes
    if (index <= 0) return; // None
    if (index < firstCustom) {
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
        m_d->ksamplerScheduler = QStringLiteral("normal");
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
    if (m_d->spinStrength)
        m_d->spinStrength->setValue(qBound(1, cfg.readEntry("Strength", 100), 100));
    m_d->comboSampler->setCurrentText(cfg.readEntry("Sampler", "euler"));
    m_d->ksamplerScheduler = cfg.readEntry("Scheduler", QStringLiteral("normal"));
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
    if (m_d->spinStrength)
        presetCfg.writeEntry("Strength", m_d->spinStrength->value());
    presetCfg.writeEntry("Sampler", m_d->comboSampler->currentText());
    presetCfg.writeEntry("Scheduler", m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler);
    presetCfg.writeEntry("Checkpoint", m_d->comboCheckpoint->currentText());
    presetCfg.writeEntry("UsesNegativePrompt", true);  // §13.143: set false if style/arch ignores negative
    mainCfg.config()->sync();
    if (m_d->comboPreset->findText(name) < 0)
        m_d->comboPreset->addItem(name);
    m_d->comboPreset->setCurrentText(name);
    m_d->labelStatus->setText(i18n("Saved preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotSaveCurrentPreset()
{
    if (!m_d->comboPreset)
        return;
    const int idx = m_d->comboPreset->currentIndex();
    if (idx < firstCustomPresetIndex()) {
        QMessageBox::information(
            this,
            i18n("Save preset"),
            i18n("Built-in styles cannot be modified. Use the add (+) button to save as a new custom preset, or select a custom preset first."));
        return;
    }
    const QString name = m_d->comboPreset->currentText();
    KConfigGroup presetCfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    presetCfg.writeEntry("Prompt", m_d->editPrompt->toPlainText());
    presetCfg.writeEntry("Negative", m_d->editNegative->toPlainText());
    presetCfg.writeEntry("Width", m_d->spinWidth->value());
    presetCfg.writeEntry("Height", m_d->spinHeight->value());
    presetCfg.writeEntry("Steps", m_d->spinSteps->value());
    presetCfg.writeEntry("Cfg", m_d->spinCfg->value());
    if (m_d->spinStrength)
        presetCfg.writeEntry("Strength", m_d->spinStrength->value());
    presetCfg.writeEntry("Sampler", m_d->comboSampler->currentText());
    presetCfg.writeEntry("Scheduler", m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler);
    presetCfg.writeEntry("Checkpoint", m_d->comboCheckpoint->currentText());
    presetCfg.writeEntry("UsesNegativePrompt", true);
    presetCfg.config()->sync();
    m_d->labelStatus->setText(i18n("Saved preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotDeletePreset()
{
    int idx = m_d->comboPreset->currentIndex();
    if (idx < firstCustomPresetIndex()) return;
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

void ComfyUIRemoteDock::applyImportedWorkflowBytes(const QByteArray &raw, const QString &openError)
{
    if (!openError.isEmpty()) {
        QMessageBox::warning(this, i18n("Import Workflow"), i18n("Could not open file: %1", openError));
        return;
    }
    if (!m_d->editCustomWorkflow)
        return;
    m_d->editCustomWorkflow->setPlainText(QString::fromUtf8(raw));
    QJsonParseError err;
    QJsonObject workflow = QJsonDocument::fromJson(raw, &err).object();
    if (err.error == QJsonParseError::NoError && !workflow.isEmpty()) {
        auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
        if (!validation.first)
            setStatusMessage(validation.second, true);  // §13.103: show in Graph view so user cannot run
        else
            setStatusMessage(i18n("Loaded workflow from file."));
    } else {
        setStatusMessage(i18n("Loaded workflow from file."));
    }
    persistOpenCustomWorkflowToDocument();
}

void ComfyUIRemoteDock::slotLoadWorkflowFromFile()
{
    // §13.167 / §13.202: Title "Import Workflow", filter Workflow Files (*.json);;All Files (*), initial dir user's home
    QString path = QFileDialog::getOpenFileName(this, i18n("Import Workflow"), QDir::homePath(), i18n("Workflow Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    // §4.8: When multi-threading is on, read and strip comments off the GUI thread; validate on main thread.
    if (ComfyUIUtils::multiThreadingEnabled()) {
        QPointer<ComfyUIRemoteDock> self(this);
        std::thread([self, path]() {
            QByteArray raw;
            QString errStr;
            {
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                    errStr = f.errorString();
                else
                    raw = ComfyUIUtils::stripJsonLineComments(f.readAll());
            }
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), "applyImportedWorkflowBytes", Qt::QueuedConnection, Q_ARG(QByteArray, raw),
                                      Q_ARG(QString, errStr));
        }).detach();
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n("Import Workflow"), i18n("Could not open file: %1", f.errorString()));
        return;
    }
    // §13.135: Strip // line comments before parsing so JSON with comments loads
    applyImportedWorkflowBytes(ComfyUIUtils::stripJsonLineComments(f.readAll()), QString());
}

void ComfyUIRemoteDock::slotRandomSeed()
{
    m_d->spinSeed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
}
