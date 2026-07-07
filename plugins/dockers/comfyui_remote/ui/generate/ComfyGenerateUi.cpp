/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGenerateUi.h"

#include "ComfyLocalization.h"
#include "ComfyRegionLink.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIUtils.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyWorkflowEngine.h"

#include <QAction>
#include <QMenu>
#include <QSignalBlocker>

#include <KSharedConfig>

#include <kis_image.h>
#include <kis_layer.h>
#include <kis_selection.h>
#include <KisViewManager.h>

#include <QObject>
#include <optional>

namespace ComfyGenerateUi {

namespace {
bool dockHasPartialSelection(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->viewManager)
        return false;
    KisSelectionSP sel = d->viewManager->selection();
    if (!sel || !sel->pixelSelection())
        return false;
    const QRect r = sel->pixelSelection()->selectedExactRect();
    if (r.isEmpty())
        return false;
    KisImageSP img = d->viewManager->image();
    if (!img)
        return true;
    return !ComfyUIUtils::isSelectionEntireDocument(img, d->viewManager);
}

QString currentInpaintModeKey(ComfyUIRemoteDock::Private *d)
{
    if (!d->inpaint.comboInpaintMode)
        return QStringLiteral("automatic");
    return d->inpaint.comboInpaintMode->currentData().toString();
}

/// Upstream `Document.selection_bounds` — any non-empty selection (includes full canvas).
bool dockHasSelection(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->viewManager)
        return false;
    KisSelectionSP sel = d->viewManager->selection();
    if (!sel || !sel->pixelSelection())
        return false;
    return !sel->pixelSelection()->selectedExactRect().isEmpty();
}

/// Upstream `DocumentModel.resolve_inpaint_mode()`.
QString resolveInpaintModeKeyForGenerate(ComfyUIRemoteDock::Private *d)
{
    const QString mode = currentInpaintModeKey(d);
    if (mode != QLatin1String("automatic"))
        return mode;
    if (!d->viewManager)
        return QStringLiteral("fill");
    KisImageSP image = d->viewManager->image();
    if (!image || !dockHasSelection(d))
        return QStringLiteral("fill");
    KisSelectionSP sel = d->viewManager->selection();
    if (!sel || !sel->pixelSelection())
        return QStringLiteral("fill");
    const QRect r = sel->pixelSelection()->selectedExactRect();
    if (r.isEmpty())
        return QStringLiteral("fill");
    return ComfyUIUtils::detectInpaintMode(image->width(), image->height(), r.x(), r.y(), r.width(), r.height());
}

void setInpaintModeKey(ComfyUIRemoteDock::Private *d, const QString &mode)
{
    if (!d->inpaint.comboInpaintMode)
        return;
    const int ix = d->inpaint.comboInpaintMode->findData(mode);
    if (ix >= 0)
        d->inpaint.comboInpaintMode->setCurrentIndex(ix);
}

QString inpaintModeLabel(ComfyUIRemoteDock::Private *d, const QString &mode)
{
    if (!d->inpaint.comboInpaintMode)
        return mode;
    const int ix = d->inpaint.comboInpaintMode->findData(mode);
    return ix >= 0 ? d->inpaint.comboInpaintMode->itemText(ix) : mode;
}

QString linkedEditStyleIdForPreset(ComfyUIRemoteDock::Private *d)
{
    if (!d->generate.comboPreset || d->generate.comboPreset->currentIndex() <= 0)
        return QString();
    const QVariant data = d->generate.comboPreset->currentData();
    const QString styleId = data.isValid() && !data.toString().isEmpty()
        ? data.toString()
        : QString(QStringLiteral("custom:") + d->generate.comboPreset->currentText());
    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
        return st->linkedEditStyle;
    return QString();
}


} // namespace

void setupInpaintMenus(ComfyUIRemoteDock *dock)
{

    auto mk = [dock](const QString &mode, const QString &text, const QString &icon, std::optional<bool> edit) {
        QAction *a = new QAction(text, dock);
        a->setIcon(ComfyTheme::icon(icon));
        a->setIconVisibleInMenu(true);
        QObject::connect(a, &QAction::triggered, dock, [dock, mode, edit]() {
            setInpaintModeKey(dock->m_d.data(), mode);
            if (edit.has_value() && dock->m_d->generate.checkEditMode) {
                dock->m_d->generate.checkEditMode->setChecked(*edit);
                KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("EditMode", *edit);
                dock->refreshRegionsList();
            }
            updateOptions(dock);
        });
        return a;
    };

    dock->m_d->generate.menuGenerate = new QMenu(dock);
    dock->m_d->generate.menuGenerate->addAction(mk(QStringLiteral("automatic"), ComfyTr::tr("Generate"),
                                    QStringLiteral("workspace-generation"), false));
    dock->m_d->generate.menuGenerate->addAction(mk(QStringLiteral("automatic"), ComfyTr::tr("Edit"), QStringLiteral("edit"), true));

    dock->m_d->inpaint.menuInpaint = new QMenu(dock);
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("automatic"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("automatic")),
                                  QStringLiteral("inpaint-automatic"), false));
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("fill"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("fill")),
                                  QStringLiteral("inpaint-fill"), false));
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("expand"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("expand")),
                                  QStringLiteral("inpaint-expand"), false));
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("add_object"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("add_object")),
                                  QStringLiteral("inpaint-add_object"), false));
    dock->m_d->inpaint.menuInpaint->addAction(
        mk(QStringLiteral("remove_object"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("remove_object")),
           QStringLiteral("inpaint-remove_object"), false));
    dock->m_d->inpaint.menuInpaint->addAction(
        mk(QStringLiteral("replace_background"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("replace_background")),
           QStringLiteral("inpaint-replace_background"), false));
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("add_object"), ComfyTr::tr("Edit"), QStringLiteral("edit"), true));
    dock->m_d->inpaint.menuInpaint->addAction(mk(QStringLiteral("custom"), inpaintModeLabel(dock->m_d.data(), QStringLiteral("custom")),
                                  QStringLiteral("inpaint-custom"), std::nullopt));

    dock->m_d->generate.menuGenerateRegion = new QMenu(dock);
    dock->m_d->generate.menuGenerateRegion->addAction(
        mk(QStringLiteral("automatic"), ComfyTr::tr("Generate Region"), QStringLiteral("generate-region"), false));
    dock->m_d->generate.menuGenerateRegion->addAction(mk(QStringLiteral("custom"), ComfyTr::tr("Generate Region (Custom)"),
                                         QStringLiteral("inpaint-custom"), std::nullopt));

    dock->m_d->generate.menuRefine = new QMenu(dock);
    dock->m_d->generate.menuRefine->addAction(mk(QStringLiteral("automatic"), ComfyTr::tr("Refine"), QStringLiteral("refine"), false));
    dock->m_d->generate.menuRefine->addAction(mk(QStringLiteral("automatic"), ComfyTr::tr("Edit"), QStringLiteral("edit"), true));

    dock->m_d->generate.menuRefineSelection = new QMenu(dock);
    dock->m_d->generate.menuRefineSelection->addAction(
        mk(QStringLiteral("automatic"), ComfyTr::tr("Refine"), QStringLiteral("refine"), false));
    dock->m_d->generate.menuRefineSelection->addAction(
        mk(QStringLiteral("automatic"), ComfyTr::tr("Edit"), QStringLiteral("edit"), true));
    dock->m_d->generate.menuRefineSelection->addAction(
        mk(QStringLiteral("custom"), ComfyTr::tr("Refine (Custom)"), QStringLiteral("inpaint-custom"), std::nullopt));

    dock->m_d->generate.menuRefineRegion = new QMenu(dock);
    dock->m_d->generate.menuRefineRegion->addAction(
        mk(QStringLiteral("automatic"), ComfyTr::tr("Refine Region"), QStringLiteral("refine-region"), false));
    dock->m_d->generate.menuRefineRegion->addAction(mk(QStringLiteral("custom"), ComfyTr::tr("Refine Region (Custom)"),
                                        QStringLiteral("inpaint-custom"), std::nullopt));

    dock->m_d->generate.menuEdit = new QMenu(dock);
    dock->m_d->generate.menuEdit->addAction(mk(QStringLiteral("automatic"), ComfyTr::tr("Edit"), QStringLiteral("edit"), true));
    dock->m_d->generate.menuEdit->addAction(
        mk(QStringLiteral("custom"), ComfyTr::tr("Edit (Custom)"), QStringLiteral("inpaint-custom"), std::nullopt));

}

void showInpaintModeMenu(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->generate.btnGenerate || !dock->m_d->inpaint.btnInpaintMode)
        return;

    const bool isEdit = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    const bool regionOnly = dock->m_d->generate.checkRegionOnly && dock->m_d->generate.checkRegionOnly->isChecked();
    const bool hasSelection = dockHasSelection(dock->m_d.data());
    const int strengthPct = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100;
    const QString ckpt = dock->checkpointForGenerate();
    const bool archIsEdit = ComfyUIUtils::isArchEdit(ckpt);
    const bool canEdit = ComfyUIUtils::hasLinkedEditStyle(linkedEditStyleIdForPreset(dock->m_d.data()));

    QMenu *menu = dock->m_d->generate.menuGenerate;
    // FAITHFUL_PORT: generation.py show_inpaint_menu()
    if (!isEdit && archIsEdit) {
        menu = dock->m_d->generate.menuEdit;
    } else if (strengthPct >= 100) {
        if (regionOnly)
            menu = dock->m_d->generate.menuGenerateRegion;
        else if (hasSelection)
            menu = dock->m_d->inpaint.menuInpaint;
        else
            menu = dock->m_d->generate.menuGenerate;
    } else {
        if (regionOnly)
            menu = dock->m_d->generate.menuRefineRegion;
        else if (hasSelection)
            menu = dock->m_d->generate.menuRefineSelection;
        else
            menu = dock->m_d->generate.menuRefine;
    }

    if (!menu)
        return;

    const QList<QAction *> actions = menu->actions();
    if (menu == dock->m_d->inpaint.menuInpaint && actions.size() >= 2)
        actions.at(actions.size() - 2)->setEnabled(canEdit);
    else if ((menu == dock->m_d->generate.menuRefine || menu == dock->m_d->generate.menuRefineSelection) && actions.size() >= 2)
        actions.at(1)->setEnabled(canEdit);

    const int width = dock->m_d->generate.btnGenerate->width() + dock->m_d->inpaint.btnInpaintMode->width();
    menu->setFixedWidth(qMax(width, 160));
    const QPoint pos(0, dock->m_d->generate.btnGenerate->height());
    menu->exec(dock->m_d->generate.btnGenerate->mapToGlobal(pos));

}

void reEnableUi(ComfyUIRemoteDock *dock)
{

    if (dock->m_d->inpaint.btnInpaint)
        dock->m_d->inpaint.btnInpaint->setEnabled(true);
    if (dock->m_d->generate.btnGenerate)
        dock->m_d->generate.btnGenerate->setEnabled(true);
    updateOptions(dock);

}

void updateOptions(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->canvas || !dock->m_d->generate.btnGenerate)
        return;
    KisImageSP image = dock->m_d->canvas->image().toStrongRef();
    if (!image)
        return;

    const QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(dock->m_d.data());
    const bool hasRegions = !regs.isEmpty();
    bool hasActiveRegion = false;
    if (dock->m_d->viewManager && hasRegions) {
        const int idx = ComfyRegionLink::findRegionIndexForLayer(
            regs, dock->m_d->viewManager->image(), dock->m_d->viewManager->activeLayer(), ComfyRegionLink::LinkMode::Any);
        hasActiveRegion = idx >= 0;
    }
    const bool regionOnly =
        hasRegions && hasActiveRegion && dock->m_d->generate.checkRegionOnly && dock->m_d->generate.checkRegionOnly->isChecked();
    const bool isEdit = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    const bool hasSelection = dockHasSelection(dock->m_d.data());
    const int strengthPct = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100;
    const bool isCustom = currentInpaintModeKey(dock->m_d.data()) == QLatin1String("custom");
    const QString resolvedMode = resolveInpaintModeKeyForGenerate(dock->m_d.data());
    const QString ckpt = dock->checkpointForGenerate();
    const QString linkedEditStyleId = linkedEditStyleIdForPreset(dock->m_d.data());
    const bool canToggleEdit =
        ComfyUIUtils::canToggleEditMode(ckpt, linkedEditStyleId);
    QString styleArch;
    if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            styleArch = st->architecture;
    }
    const ComfyResources::Arch genArch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
    const bool qwenLayered = genArch == ComfyResources::Arch::QwenL;
    const bool strengthFull = qwenLayered || strengthPct >= 100;

    if (dock->m_d->generate.layerCountRow)
        dock->m_d->generate.layerCountRow->setVisible(qwenLayered);
    if (dock->m_d->inpaint.strengthSliderWidget)
        dock->m_d->inpaint.strengthSliderWidget->setVisible(!qwenLayered);
    if (dock->m_d->generate.spinStrength)
        dock->m_d->generate.spinStrength->setVisible(!qwenLayered);

    if (dock->m_d->inpaint.btnRegionMask) {
        dock->m_d->inpaint.btnRegionMask->setVisible(hasRegions);
        dock->m_d->inpaint.btnRegionMask->setEnabled(hasActiveRegion);
        dock->m_d->inpaint.btnRegionMask->setIcon(ComfyTheme::icon(regionOnly ? QStringLiteral("region-alpha-active")
                                                                : QStringLiteral("region-alpha")));
        if (dock->m_d->generate.checkRegionOnly) {
            QSignalBlocker b(dock->m_d->inpaint.btnRegionMask);
            dock->m_d->inpaint.btnRegionMask->setChecked(dock->m_d->generate.checkRegionOnly->isChecked());
        }
    }

    if (dock->m_d->inpaint.customInpaintRowWidget)
        dock->m_d->inpaint.customInpaintRowWidget->setVisible(isCustom && (hasSelection || regionOnly));

    QString text;
    QString iconName;
    // Upstream generation.py update_generate_options()
    if (!hasSelection && !regionOnly) {
        if (dock->m_d->inpaint.btnInpaintMode)
            dock->m_d->inpaint.btnInpaintMode->setVisible(canToggleEdit);
        if (isEdit) {
            iconName = QStringLiteral("edit");
            text = ComfyTr::tr("Edit");
        } else if (strengthFull) {
            iconName = QStringLiteral("workspace-generation");
            text = ComfyTr::tr("Generate");
        } else {
            iconName = QStringLiteral("refine");
            text = ComfyTr::tr("Refine");
        }
    } else {
        if (dock->m_d->inpaint.btnInpaintMode)
            dock->m_d->inpaint.btnInpaintMode->setVisible(true);
        text = ComfyTr::tr("Generate");
        if (isEdit)
            text = ComfyTr::tr("Edit");
        else if (!strengthFull)
            text = ComfyTr::tr("Refine");
        if (regionOnly)
            text += QLatin1Char(' ') + ComfyTr::tr("Region");
        if (isCustom)
            text += QLatin1Char(' ') + ComfyTr::tr("(Custom)");

        if (strengthFull && !isEdit) {
            if (isCustom) {
                iconName = QStringLiteral("inpaint-custom");
            } else if (regionOnly) {
                iconName = QStringLiteral("generate-region");
            } else {
                iconName = QStringLiteral("inpaint-") + resolvedMode;
                text = inpaintModeLabel(dock->m_d.data(), resolvedMode);
            }
        } else if (!isEdit) {
            if (isCustom)
                iconName = QStringLiteral("inpaint-custom");
            else if (regionOnly)
                iconName = QStringLiteral("refine-region");
            else
                iconName = QStringLiteral("refine");
        } else {
            iconName = isCustom ? QStringLiteral("inpaint-custom") : QStringLiteral("edit");
        }
    }

    dock->m_d->generate.btnGenerate->setText(text);
    if (!iconName.isEmpty())
        dock->m_d->generate.btnGenerate->setIcon(ComfyTheme::icon(iconName));

    if (dock->m_d->inpaint.btnInpaintMode && dock->m_d->generate.btnGenerate)
        dock->m_d->inpaint.btnInpaintMode->setFixedHeight(ComfyUiStyle::Spacing::primaryButtonHeight - 3);

}

} // namespace ComfyGenerateUi
