/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyRegionPromptWidget.h"
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
        << " name=" << (m_d->generate.comboPreset ? m_d->generate.comboPreset->itemText(index) : QString())
        << " firstCustom=" << firstCustomPresetIndex();
    const int firstCustom = firstCustomPresetIndex();
    m_d->generate.btnDeletePreset->setEnabled(index >= firstCustom);
    updateNegativePromptAlertVisibility();  // §13.143: update alert when style changes
    if (index <= 0) {
        persistDocumentDefaultsToSettings();  // §13.194
        updateGenerateOptions();
        updateInpaintControlsForArch();
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
        updateGenerateOptions();
        updateInpaintControlsForArch();
        return;
    }
    // Custom preset: load from config
    QString name = m_d->generate.comboPreset->itemText(index);
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    m_d->generate.editPrompt->setPlainText(cfg.readEntry("Prompt", ""));
    m_d->generate.editNegative->setPlainText(cfg.readEntry("Negative", ""));
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->refreshRootPromptFromDock();
    m_d->generate.spinWidth->setValue(cfg.readEntry("Width", 512));
    m_d->generate.spinHeight->setValue(cfg.readEntry("Height", 512));
    m_d->generate.spinSteps->setValue(cfg.readEntry("Steps", 20));
    m_d->generate.spinCfg->setValue(cfg.readEntry("Cfg", 8.0));
    if (m_d->generate.spinStrength)
        m_d->generate.spinStrength->setValue(qBound(1, cfg.readEntry("Strength", 100), 100));
    m_d->generate.comboSampler->setCurrentText(cfg.readEntry("Sampler", "euler"));
    m_d->generateRt.ksamplerScheduler = cfg.readEntry("Scheduler", QStringLiteral("normal"));
    QString ckpt = cfg.readEntry("Checkpoint", "");
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotPresetChanged loaded custom preset name=" << name
        << " checkpoint=" << ckpt
        << " promptLen=" << m_d->generate.editPrompt->toPlainText().size()
        << " negLen=" << m_d->generate.editNegative->toPlainText().size()
        << " w=" << m_d->generate.spinWidth->value() << " h=" << m_d->generate.spinHeight->value()
        << " steps=" << m_d->generate.spinSteps->value() << " cfg=" << m_d->generate.spinCfg->value();
    if (!ckpt.isEmpty()) {
        int i = m_d->generate.comboCheckpoint->findText(ckpt);
        if (i >= 0) m_d->generate.comboCheckpoint->setCurrentIndex(i);
        else m_d->generate.comboCheckpoint->setCurrentText(ckpt);
    }
    persistDocumentDefaultsToSettings();  // §13.194
    updateGenerateOptions();
    updateInpaintControlsForArch();
}

void ComfyUIRemoteDock::slotSaveAsPreset()
{
    qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveAsPreset ENTER";
    bool ok = false;
    QString name = QInputDialog::getText(this, ComfyTr::tr("Save preset"), ComfyTr::tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveAsPreset: user cancelled or empty name; aborting";
        return;
    }
    saveCustomPresetAsNew(name.trimmed());
}

bool ComfyUIRemoteDock::saveCustomPresetAsNew(const QString &nameIn)
{
    const QString name = nameIn.trimmed();
    if (name.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "saveCustomPresetAsNew: empty name; aborting";
        return false;
    }
    KConfigGroup mainCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    QStringList names = mainCfg.readEntry("PresetNames", QStringList());
    if (names.contains(name)) {
        setStatusMessage(ComfyTr::tr("A preset named \"%1\" already exists.", name), true);
        return false;
    }
    names << name;
    mainCfg.writeEntry("PresetNames", names);
    KConfigGroup presetCfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    presetCfg.writeEntry("Prompt", m_d->generate.editPrompt->toPlainText());
    presetCfg.writeEntry("Negative", m_d->generate.editNegative->toPlainText());
    presetCfg.writeEntry("Width", m_d->generate.spinWidth->value());
    presetCfg.writeEntry("Height", m_d->generate.spinHeight->value());
    presetCfg.writeEntry("Steps", m_d->generate.spinSteps->value());
    presetCfg.writeEntry("Cfg", m_d->generate.spinCfg->value());
    if (m_d->generate.spinStrength)
        presetCfg.writeEntry("Strength", m_d->generate.spinStrength->value());
    presetCfg.writeEntry("Sampler", m_d->generate.comboSampler->currentText());
    presetCfg.writeEntry("Scheduler", m_d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->generateRt.ksamplerScheduler);
    presetCfg.writeEntry("Checkpoint", m_d->generate.comboCheckpoint->currentText());
    presetCfg.writeEntry("UsesNegativePrompt", true);  // §13.143: set false if style/arch ignores negative
    presetCfg.config()->sync();  // FAITHFUL_PORT: was syncing the wrong group's config handle
    mainCfg.config()->sync();
    if (m_d->generate.comboPreset->findText(name) < 0)
        m_d->generate.comboPreset->addItem(name);
    m_d->generate.comboPreset->setCurrentText(name);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "saveCustomPresetAsNew SAVED name=" << name
        << " checkpoint=" << m_d->generate.comboCheckpoint->currentText()
        << " promptLen=" << m_d->generate.editPrompt->toPlainText().size()
        << " negLen=" << m_d->generate.editNegative->toPlainText().size()
        << " w=" << m_d->generate.spinWidth->value() << " h=" << m_d->generate.spinHeight->value()
        << " steps=" << m_d->generate.spinSteps->value() << " cfg=" << m_d->generate.spinCfg->value();
    setStatusMessage(ComfyTr::tr("Saved preset \"%1\".", name));
    return true;
}

void ComfyUIRemoteDock::slotSaveCurrentPreset()
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotSaveCurrentPreset ENTER idx="
        << (m_d->generate.comboPreset ? m_d->generate.comboPreset->currentIndex() : -1)
        << " name="
        << (m_d->generate.comboPreset ? m_d->generate.comboPreset->currentText() : QStringLiteral("<null comboPreset>"))
        << " firstCustomIdx=" << firstCustomPresetIndex();
    if (!m_d->generate.comboPreset) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotSaveCurrentPreset: comboPreset is null; aborting";
        return;
    }
    const int idx = m_d->generate.comboPreset->currentIndex();
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
    const QString name = m_d->generate.comboPreset->currentText();
    KConfigGroup presetCfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
    presetCfg.writeEntry("Prompt", m_d->generate.editPrompt->toPlainText());
    presetCfg.writeEntry("Negative", m_d->generate.editNegative->toPlainText());
    presetCfg.writeEntry("Width", m_d->generate.spinWidth->value());
    presetCfg.writeEntry("Height", m_d->generate.spinHeight->value());
    presetCfg.writeEntry("Steps", m_d->generate.spinSteps->value());
    presetCfg.writeEntry("Cfg", m_d->generate.spinCfg->value());
    if (m_d->generate.spinStrength)
        presetCfg.writeEntry("Strength", m_d->generate.spinStrength->value());
    presetCfg.writeEntry("Sampler", m_d->generate.comboSampler->currentText());
    presetCfg.writeEntry("Scheduler", m_d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->generateRt.ksamplerScheduler);
    presetCfg.writeEntry("Checkpoint", m_d->generate.comboCheckpoint->currentText());
    presetCfg.writeEntry("UsesNegativePrompt", true);
    presetCfg.config()->sync();
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotSaveCurrentPreset SAVED name=" << name
        << " checkpoint=" << m_d->generate.comboCheckpoint->currentText()
        << " promptLen=" << m_d->generate.editPrompt->toPlainText().size()
        << " negLen=" << m_d->generate.editNegative->toPlainText().size();
    setStatusMessage(ComfyTr::tr("Saved preset \"%1\".", name));
}

void ComfyUIRemoteDock::slotDeletePreset()
{
    int idx = m_d->generate.comboPreset->currentIndex();
    if (idx <= 0)
        return;
    if (idx < firstCustomPresetIndex()) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
        const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId);
        if (!st || st->isBuiltin)
            return;
        const QString name = st->name;
        if (!ComfyStyleCollection::instance().deleteUserStyle(styleId))
            return;
        rebuildPresetComboItems();
        m_d->generate.comboPreset->setCurrentIndex(0);
        m_d->labelStatus->setText(ComfyTr::tr("Deleted style \"%1\".", name));
        return;
    }
    QString name = m_d->generate.comboPreset->currentText();
    KConfigGroup mainCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    QStringList names = mainCfg.readEntry("PresetNames", QStringList());
    names.removeAll(name);
    mainCfg.writeEntry("PresetNames", names);
    KSharedConfig::openConfig()->deleteGroup("ComfyUIRemote_Preset_" + name);
    mainCfg.config()->sync();
    m_d->generate.comboPreset->removeItem(idx);
    m_d->generate.comboPreset->setCurrentIndex(0);
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
    m_d->generate.spinSeed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
}
