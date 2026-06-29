/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_annotation.h>
#include <kis_types.h>

using ComfyDockShellInternal::ComfyPromptPlainTextEdit;
using ComfyDockShellInternal::LiveSpinnerWidget;
using ComfyDockShellInternal::StrengthSpinBox;
using ComfyDockShellInternal::setComboCurrentItemData;


namespace ComfyDockUiBuilderGenerateInternal {

void buildInpaintSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    // §13.206 / P4.1: InpaintMode — all seven Python modes with theme icons
    d->inpaint.customInpaintRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *customInpaintLay = new QHBoxLayout(d->inpaint.customInpaintRowWidget);
    customInpaintLay->setContentsMargins(0, 0, 0, 0);
    auto addInpaintComboItem = [](QComboBox *cb, const QString &label, const QString &data, const char *iconStem) {
        cb->addItem(ComfyTheme::icon(QString::fromUtf8(iconStem)), label, data);
    };
    d->inpaint.comboInpaintMode = new QComboBox(shell.genGroup);
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Default (Auto-detect)"), QStringLiteral("automatic"), "inpaint-automatic");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Fill"), QStringLiteral("fill"), "inpaint-fill");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Expand"), QStringLiteral("expand"), "inpaint-expand");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Add Content"), QStringLiteral("add_object"), "inpaint-add_object");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Remove Content"), QStringLiteral("remove_object"), "inpaint-remove_object");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Replace Background"), QStringLiteral("replace_background"),
                        "inpaint-replace_background");
    addInpaintComboItem(d->inpaint.comboInpaintMode, ComfyTr::tr("Generate (Custom)"), QStringLiteral("custom"), "inpaint-custom");
    d->inpaint.comboInpaintMode->setToolTip(
        ComfyTr::tr("Automatic: expand if selection touches canvas edge, else fill. Other modes set fill semantics and prompt instructions."));
    setComboCurrentItemData(d->inpaint.comboInpaintMode, QStringLiteral("automatic"), 0);
    QObject::connect(d->inpaint.comboInpaintMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        if (d->inpaint.comboFillMode) {
            const QString mode = d->inpaint.comboInpaintMode->currentData().toString();
            if (mode != QLatin1String("automatic")) {
                QSignalBlocker b(d->inpaint.comboFillMode);
                setComboCurrentItemData(d->inpaint.comboFillMode, ComfyUIUtils::defaultFillKindForInpaintMode(mode), 2);
            }
        }
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->saveInpaintWorkspaceToDocument();
        dock->schedulePersistDocumentDefaults();
        dock->updateGenerateOptions();
    });
    // comboInpaintMode stays off-layout (mode chosen via ▼ menu); not added to customInpaintRowWidget.
    // §13.188: FillMode UI — five options (None, Neutral, Blur, Border, Inpaint); replace/green internal only
    d->inpaint.comboFillMode = new QComboBox(shell.genGroup);
    const QIcon fillIcon = ComfyTheme::icon(QStringLiteral("fill"));
    d->inpaint.comboFillMode->addItem(ComfyTheme::icon(QStringLiteral("fill-empty")),
                                ComfyTr::tr("None"),
                                QStringLiteral("none"));
    d->inpaint.comboFillMode->addItem(fillIcon, ComfyTr::tr("Neutral"), QStringLiteral("neutral"));
    d->inpaint.comboFillMode->addItem(fillIcon, ComfyTr::tr("Blur"), QStringLiteral("blur"));
    d->inpaint.comboFillMode->addItem(fillIcon, ComfyTr::tr("Border"), QStringLiteral("border"));
    d->inpaint.comboFillMode->addItem(fillIcon, ComfyTr::tr("Inpaint"), QStringLiteral("inpaint"));
    d->inpaint.comboFillMode->setToolTip(ComfyTr::tr("Pre-fill the selected region before diffusion"));
    setComboCurrentItemData(d->inpaint.comboFillMode, QStringLiteral("blur"), 2);
    QObject::connect(d->inpaint.comboFillMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->saveInpaintWorkspaceToDocument();
        dock->schedulePersistDocumentDefaults();
    });
    customInpaintLay->addWidget(d->inpaint.comboFillMode, 1);
    // §13.169 / §13.194: Inpaint context (Python InpaintContext; JSON uses underscores)
    d->inpaint.comboInpaintContext = new QComboBox(shell.genGroup);
    addInpaintComboItem(d->inpaint.comboInpaintContext, ComfyTr::tr("Automatic Context"), QStringLiteral("automatic"),
                        "context-automatic");
    addInpaintComboItem(d->inpaint.comboInpaintContext, ComfyTr::tr("Selection Bounds"), QStringLiteral("mask_bounds"),
                        "context-mask");
    addInpaintComboItem(d->inpaint.comboInpaintContext, ComfyTr::tr("Entire Image"), QStringLiteral("entire_image"),
                        "context-image");
    d->inpaint.comboInpaintContext->setToolTip(
        ComfyTr::tr("Part of the image around the selection which is used as context."));
    d->inpaint.comboInpaintContext->setMinimumContentsLength(20);
    d->inpaint.comboInpaintContext->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    d->inpaintRt.inpaintContextKey = QStringLiteral("automatic");
    d->inpaint.comboInpaintContext->setCurrentIndex(0);
    QObject::connect(d->inpaint.comboInpaintContext, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        ComfyUIUtils::decodeInpaintContextComboData(d->inpaint.comboInpaintContext->currentData(),
                                                    &d->inpaintRt.inpaintContextKey, &d->inpaintRt.inpaintContextLayerId);
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->saveInpaintWorkspaceToDocument();
        dock->schedulePersistDocumentDefaults();
    });
    customInpaintLay->addWidget(d->inpaint.comboInpaintContext, 1);
    // §13.107 / §13.169: CustomInpaint toggles (Python: Seamless / Focus)
    d->inpaint.checkInpaintUseModel = new ComfySwitchWidget(shell.genGroup);
    {
        QLabel *seamlessLabel = new QLabel(ComfyTr::tr("Seamless"), d->inpaint.customInpaintRowWidget);
        customInpaintLay->insertWidget(0, d->inpaint.checkInpaintUseModel);
        customInpaintLay->insertWidget(1, seamlessLabel);
        d->inpaint.seamlessRowWidget = nullptr;
    }
    d->inpaint.checkInpaintUseModel->setToolTip(ComfyTr::tr("Generate content which blends into the surroundings"));
    d->inpaint.checkInpaintUseModel->setChecked(true);
    d->inpaintRt.inpaintPersistUseModel = true;
    QObject::connect(d->inpaint.checkInpaintUseModel, &QAbstractButton::toggled, dock, [dock, d](bool on) {
        d->inpaintRt.inpaintPersistUseModel = on;
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->saveInpaintWorkspaceToDocument();
        dock->schedulePersistDocumentDefaults();
    });
    d->inpaint.checkInpaintUsePromptFocus = new ComfySwitchWidget(shell.genGroup);
    {
        QLabel *focusLabel = new QLabel(ComfyTr::tr("Focus"), d->inpaint.customInpaintRowWidget);
        customInpaintLay->insertWidget(2, d->inpaint.checkInpaintUsePromptFocus);
        customInpaintLay->insertWidget(3, focusLabel);
        d->inpaint.focusRowWidget = nullptr;
    }
    d->inpaint.checkInpaintUsePromptFocus->setToolTip(
        ComfyTr::tr("Focus generation on the masked area using prompt conditioning (SD 1.5 / SDXL)."));
    d->inpaint.checkInpaintUsePromptFocus->setChecked(false);
    d->inpaintRt.inpaintPersistUsePromptFocus = false;
    QObject::connect(d->inpaint.checkInpaintUsePromptFocus, &QAbstractButton::toggled, dock, [dock, d](bool on) {
        d->inpaintRt.inpaintPersistUsePromptFocus = on;
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->saveInpaintWorkspaceToDocument();
        dock->schedulePersistDocumentDefaults();
    });
    // Python CustomInpaint.edit_mode_switch — visible in custom bar; checkEditMode stays hidden state.
    d->inpaint.editModeSwitch = new ComfySwitchWidget(d->inpaint.customInpaintRowWidget);
    {
        QLabel *editLabel = new QLabel(ComfyTr::tr("Edit"), d->inpaint.customInpaintRowWidget);
        editLabel->setToolTip(ComfyTr::tr("Use instruction-based editing (linked edit style)."));
        d->inpaint.editModeSwitch->setToolTip(editLabel->toolTip());
        customInpaintLay->insertWidget(4, d->inpaint.editModeSwitch);
        customInpaintLay->insertWidget(5, editLabel);
    }
    if (d->generate.checkEditMode) {
        d->inpaint.editModeSwitch->setChecked(d->generate.checkEditMode->isChecked());
        QObject::connect(d->inpaint.editModeSwitch, &QAbstractButton::toggled, dock, [dock, d](bool checked) {
            if (!d->generate.checkEditMode)
                return;
            d->generate.checkEditMode->setChecked(checked);
        });
        QObject::connect(d->generate.checkEditMode, &QCheckBox::toggled, dock, [d](bool checked) {
            if (!d->inpaint.editModeSwitch)
                return;
            QSignalBlocker b(d->inpaint.editModeSwitch);
            d->inpaint.editModeSwitch->setChecked(checked);
        });
    }
    QObject::connect(d->generate.comboCheckpoint, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        dock->updateInpaintControlsForArch();
    });
    if (d->generate.comboCheckpoint->lineEdit()) {
        QObject::connect(d->generate.comboCheckpoint->lineEdit(), &QLineEdit::editingFinished, dock, [dock, d]() {
            dock->updateInpaintControlsForArch();
        });
    }
    if (d->generate.checkEditMode) {
        QObject::connect(d->generate.checkEditMode, &QCheckBox::toggled, dock, [dock, d](bool) { dock->updateInpaintControlsForArch(); });
    }
    dock->updateInpaintControlsForArch();
    genContentLayout->addWidget(d->inpaint.customInpaintRowWidget);
    d->inpaint.customInpaintRowWidget->setVisible(false);

    ComfyTheme::applyFlatComboStyle(d->inpaint.comboInpaintMode);
    ComfyTheme::applyFlatComboStyle(d->inpaint.comboFillMode);
    ComfyTheme::applyFlatComboStyle(d->inpaint.comboInpaintContext);
    ComfyTheme::applyToolbarComboStyle(d->generate.comboPreset);
    ComfyTheme::applyFlatComboStyle(d->generate.comboCheckpoint);
    ComfyTheme::applyFlatComboStyle(d->generate.comboQuality);
    ComfyTheme::applyFlatComboStyle(d->generate.comboQueueMode);
    ComfyTheme::applyFlatComboStyle(d->generate.comboSampler);

}

} // namespace ComfyDockUiBuilderGenerateInternal
