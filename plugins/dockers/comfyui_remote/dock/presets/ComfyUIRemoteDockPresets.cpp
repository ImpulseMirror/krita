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
#include "ComfyTheme.h"

#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QLoggingCategory>
#include <QFileInfo>
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
    bool ok = false;
    QString name = QInputDialog::getText(this, ComfyTr::tr("Save preset"), ComfyTr::tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    saveCustomPresetAsNew(name.trimmed());
}

bool ComfyUIRemoteDock::saveCustomPresetAsNew(const QString &nameIn)
{
    const QString name = nameIn.trimmed();
    if (name.isEmpty()) {
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
    setStatusMessage(ComfyTr::tr("Saved preset \"%1\".", name));
    return true;
}

void ComfyUIRemoteDock::slotSaveCurrentPreset()
{
    if (!m_d->generate.comboPreset) {
        return;
    }
    const int idx = m_d->generate.comboPreset->currentIndex();
    if (idx < firstCustomPresetIndex()) {
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
    refreshCustomWorkflowParameterPanel();
}

void ComfyUIRemoteDock::refreshGraphWorkflowCombo()
{
    if (!m_d->comboGraphWorkflow)
        return;
    const QString prev = m_d->comboGraphWorkflow->currentData().toString();
    QSignalBlocker b(m_d->comboGraphWorkflow);
    m_d->comboGraphWorkflow->clear();
    m_d->comboGraphWorkflow->addItem(ComfyTheme::icon(QStringLiteral("file-json")),
                                    ComfyTr::tr("(unsaved / pasted)"), QString());
    const QStringList files = ComfyUIUtils::listLocalWorkflowJsonFilenames();
    for (const QString &fn : files) {
        m_d->comboGraphWorkflow->addItem(ComfyTheme::icon(QStringLiteral("file-json")),
                                        QFileInfo(fn).completeBaseName(), fn);
    }
    int restore = 0;
    if (!prev.isEmpty()) {
        const int ix = m_d->comboGraphWorkflow->findData(prev);
        if (ix >= 0)
            restore = ix;
    }
    m_d->comboGraphWorkflow->setCurrentIndex(restore);
    const bool hasSelection = restore > 0;
    if (m_d->btnGraphSaveWorkflow)
        m_d->btnGraphSaveWorkflow->setEnabled(m_d->editCustomWorkflow
                                              && !m_d->editCustomWorkflow->toPlainText().trimmed().isEmpty());
    if (m_d->btnGraphDeleteWorkflow)
        m_d->btnGraphDeleteWorkflow->setEnabled(hasSelection);
}

void ComfyUIRemoteDock::slotGraphWorkflowSelected(int index)
{
    if (!m_d->comboGraphWorkflow || !m_d->editCustomWorkflow)
        return;
    if (m_d->btnGraphDeleteWorkflow)
        m_d->btnGraphDeleteWorkflow->setEnabled(index > 0);
    if (index <= 0)
        return;
    const QString fileName = m_d->comboGraphWorkflow->itemData(index).toString();
    if (fileName.isEmpty())
        return;
    const QString path = ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + fileName;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatusMessage(ComfyTr::tr("Could not open workflow: %1", f.errorString()), true);
        return;
    }
    applyImportedWorkflowBytes(ComfyUIUtils::stripJsonLineComments(f.readAll()), QString());
}

void ComfyUIRemoteDock::slotSaveWorkflowToLibrary()
{
    if (!m_d->editCustomWorkflow)
        return;
    const QString json = m_d->editCustomWorkflow->toPlainText().trimmed();
    if (json.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Nothing to save — paste or import a workflow first."), true);
        return;
    }
    QString baseName;
    if (m_d->comboGraphWorkflow && m_d->comboGraphWorkflow->currentIndex() > 0)
        baseName = m_d->comboGraphWorkflow->currentText().trimmed();
    bool ok = false;
    baseName = QInputDialog::getText(this, ComfyTr::tr("Save workflow to file"),
                                     ComfyTr::tr("Workflow name:"), QLineEdit::Normal, baseName, &ok)
                   .trimmed();
    if (!ok || baseName.isEmpty())
        return;
    baseName.replace(QLatin1Char('/'), QLatin1Char('_'));
    baseName.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (!baseName.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        baseName += QStringLiteral(".json");
    const QString path = ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + baseName;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, ComfyTr::tr("Save Workflow"),
                             ComfyTr::tr("Could not write file: %1", f.errorString()));
        return;
    }
    f.write(json.toUtf8());
    f.close();
    refreshGraphWorkflowCombo();
    if (m_d->comboGraphWorkflow) {
        const int ix = m_d->comboGraphWorkflow->findData(baseName);
        if (ix >= 0) {
            QSignalBlocker b(m_d->comboGraphWorkflow);
            m_d->comboGraphWorkflow->setCurrentIndex(ix);
        }
    }
    if (m_d->btnGraphDeleteWorkflow)
        m_d->btnGraphDeleteWorkflow->setEnabled(true);
    setStatusMessage(ComfyTr::tr("Saved workflow \"%1\".", QFileInfo(baseName).completeBaseName()), false);
}

void ComfyUIRemoteDock::slotDeleteWorkflowFromLibrary()
{
    if (!m_d->comboGraphWorkflow || m_d->comboGraphWorkflow->currentIndex() <= 0)
        return;
    const QString fileName = m_d->comboGraphWorkflow->currentData().toString();
    if (fileName.isEmpty())
        return;
    const QString path = ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + fileName;
    const auto answer = QMessageBox::question(
        this, ComfyTr::tr("Delete Workflow"),
        ComfyTr::tr("Are you sure you want to delete the current workflow?") + QLatin1Char('\n') + path,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (!QFile::remove(path)) {
        QMessageBox::warning(this, ComfyTr::tr("Delete Workflow"),
                             ComfyTr::tr("Could not delete file: %1", path));
        return;
    }
    if (m_d->editCustomWorkflow)
        m_d->editCustomWorkflow->clear();
    persistOpenCustomWorkflowToDocument();
    refreshCustomWorkflowParameterPanel();
    refreshGraphWorkflowCombo();
    setStatusMessage(ComfyTr::tr("Deleted workflow."), false);
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
            // Copy into local library when imported from elsewhere.
            if (errStr.isEmpty() && !raw.isEmpty()) {
                QString base = QFileInfo(path).fileName();
                if (!base.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
                    base += QStringLiteral(".json");
                const QString dest = ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + base;
                if (QFileInfo(path).absoluteFilePath() != QFileInfo(dest).absoluteFilePath()) {
                    QFile out(dest);
                    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        out.write(raw);
                }
            }
            QMetaObject::invokeMethod(self.data(), "applyImportedWorkflowBytes", Qt::QueuedConnection, Q_ARG(QByteArray, raw),
                                      Q_ARG(QString, errStr));
            QMetaObject::invokeMethod(self.data(), "refreshGraphWorkflowCombo", Qt::QueuedConnection);
        }).detach();
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, ComfyTr::tr("Import Workflow"), ComfyTr::tr("Could not open file: %1", f.errorString()));
        return;
    }
    // §13.135: Strip // line comments before parsing so JSON with comments loads
    const QByteArray raw = ComfyUIUtils::stripJsonLineComments(f.readAll());
    QString base = QFileInfo(path).fileName();
    if (!base.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        base += QStringLiteral(".json");
    const QString dest = ComfyUIUtils::workflowsStorageDir() + QLatin1Char('/') + base;
    if (QFileInfo(path).absoluteFilePath() != QFileInfo(dest).absoluteFilePath()) {
        QFile out(dest);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(raw);
    }
    applyImportedWorkflowBytes(raw, QString());
    refreshGraphWorkflowCombo();
    if (m_d->comboGraphWorkflow) {
        const int ix = m_d->comboGraphWorkflow->findData(base);
        if (ix >= 0) {
            QSignalBlocker b(m_d->comboGraphWorkflow);
            m_d->comboGraphWorkflow->setCurrentIndex(ix);
        }
        if (m_d->btnGraphDeleteWorkflow)
            m_d->btnGraphDeleteWorkflow->setEnabled(ix > 0);
    }
}

void ComfyUIRemoteDock::slotRandomSeed()
{
    m_d->generate.spinSeed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
}
