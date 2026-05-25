/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyStyleCollection.h"
#include "ComfyUIUtils.h"

#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QLoggingCategory>
#include <thread>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

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
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotPresetChanged index=" << index
        << " name=" << (m_d->comboPreset ? m_d->comboPreset->itemText(index) : QString())
        << " firstCustom=" << firstCustomPresetIndex();
    const int firstCustom = firstCustomPresetIndex();
    m_d->btnDeletePreset->setEnabled(index >= firstCustom);
    updateNegativePromptAlertVisibility();  // §13.143: update alert when style changes
    if (index <= 0) {
        persistDocumentDefaultsToSettings();  // §13.194
        return; // None
    }
    if (index < firstCustom) {
        const bool showBuiltin =
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
        const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
        const int styleIdx = index - 1;
        if (styleIdx >= 0 && styleIdx < styles.size())
            applyComfyStyleEntry(*styles.at(styleIdx));
        persistDocumentDefaultsToSettings();  // §13.194
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
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotPresetChanged loaded custom preset name=" << name
        << " checkpoint=" << ckpt
        << " promptLen=" << m_d->editPrompt->toPlainText().size()
        << " negLen=" << m_d->editNegative->toPlainText().size()
        << " w=" << m_d->spinWidth->value() << " h=" << m_d->spinHeight->value()
        << " steps=" << m_d->spinSteps->value() << " cfg=" << m_d->spinCfg->value();
    if (!ckpt.isEmpty()) {
        int i = m_d->comboCheckpoint->findText(ckpt);
        if (i >= 0) m_d->comboCheckpoint->setCurrentIndex(i);
        else m_d->comboCheckpoint->setCurrentText(ckpt);
    }
    persistDocumentDefaultsToSettings();  // §13.194
}

void ComfyUIRemoteDock::slotSaveAsPreset()
{
    qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveAsPreset ENTER";
    QString name = QInputDialog::getText(this, ComfyTr::tr("Save preset"), ComfyTr::tr("Preset name:"), QLineEdit::Normal, QString());
    if (name.trimmed().isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveAsPreset: user cancelled or empty name; aborting";
        return;
    }
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
    presetCfg.config()->sync();  // FAITHFUL_PORT: was syncing the wrong group's config handle
    mainCfg.config()->sync();
    if (m_d->comboPreset->findText(name) < 0)
        m_d->comboPreset->addItem(name);
    m_d->comboPreset->setCurrentText(name);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotSaveAsPreset SAVED name=" << name
        << " checkpoint=" << m_d->comboCheckpoint->currentText()
        << " promptLen=" << m_d->editPrompt->toPlainText().size()
        << " negLen=" << m_d->editNegative->toPlainText().size()
        << " w=" << m_d->spinWidth->value() << " h=" << m_d->spinHeight->value()
        << " steps=" << m_d->spinSteps->value() << " cfg=" << m_d->spinCfg->value();
    setStatusMessage(ComfyTr::tr("Saved preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotSaveCurrentPreset()
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotSaveCurrentPreset ENTER idx="
        << (m_d->comboPreset ? m_d->comboPreset->currentIndex() : -1)
        << " name="
        << (m_d->comboPreset ? m_d->comboPreset->currentText() : QStringLiteral("<null comboPreset>"))
        << " firstCustomIdx=" << firstCustomPresetIndex();
    if (!m_d->comboPreset) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveCurrentPreset: comboPreset is null; aborting";
        return;
    }
    const int idx = m_d->comboPreset->currentIndex();
    if (idx < firstCustomPresetIndex()) {
        qCWarning(KIS_COMFYUI_REMOTE)
            << "slotSaveCurrentPreset: refusing to save over a built-in style (idx=" << idx
            << " < firstCustomIdx=" << firstCustomPresetIndex() << ")";
        // FAITHFUL_PORT: surface the rejection in the status bar too — on Android
        // the QMessageBox::information dialog can appear behind the Settings
        // dialog stack and the user sees nothing.
        setStatusMessage(
            ComfyTr::tr("Built-in styles cannot be modified. Use the add (+) button first."), true);
        QMessageBox::information(
            this,
            ComfyTr::tr("Save preset"),
            ComfyTr::tr("Built-in styles cannot be modified. Use the add (+) button to save as a new custom preset, or select a custom preset first."));
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
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotSaveCurrentPreset SAVED name=" << name
        << " checkpoint=" << m_d->comboCheckpoint->currentText()
        << " promptLen=" << m_d->editPrompt->toPlainText().size()
        << " negLen=" << m_d->editNegative->toPlainText().size();
    setStatusMessage(ComfyTr::tr("Saved preset \"%1\".", name));
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
    m_d->labelStatus->setText(ComfyTr::tr("Deleted preset \"%1\".", name));
}

void ComfyUIRemoteDock::applyImportedWorkflowBytes(const QByteArray &raw, const QString &openError)
{
    if (!openError.isEmpty()) {
        QMessageBox::warning(this, ComfyTr::tr("Import Workflow"), ComfyTr::tr("Could not open file: %1", openError));
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
            setStatusMessage(ComfyTr::tr("Loaded workflow from file."));
    } else {
        setStatusMessage(ComfyTr::tr("Loaded workflow from file."));
    }
    persistOpenCustomWorkflowToDocument();
}

void ComfyUIRemoteDock::slotLoadWorkflowFromFile()
{
    // §13.167 / §13.202: Title "Import Workflow", filter Workflow Files (*.json);;All Files (*), initial dir user's home
    QString path = QFileDialog::getOpenFileName(this, ComfyTr::tr("Import Workflow"), QDir::homePath(), ComfyTr::tr("Workflow Files (*.json);;All Files (*)"));
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
        QMessageBox::warning(this, ComfyTr::tr("Import Workflow"), ComfyTr::tr("Could not open file: %1", f.errorString()));
        return;
    }
    // §13.135: Strip // line comments before parsing so JSON with comments loads
    applyImportedWorkflowBytes(ComfyUIUtils::stripJsonLineComments(f.readAll()), QString());
}

void ComfyUIRemoteDock::slotRandomSeed()
{
    m_d->spinSeed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
}
