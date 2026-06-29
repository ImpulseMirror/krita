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

void buildPromptSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->tagKeywordModel = new QStringListModel(dock);
    d->promptTagCompleter = new QCompleter(d->tagKeywordModel, dock);
    d->promptTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    d->promptTagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    d->negativePromptTagCompleter = new QCompleter(d->tagKeywordModel, dock);
    d->negativePromptTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    d->negativePromptTagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    d->generate.editPrompt = new ComfyPromptPlainTextEdit(d->promptTagCompleter);
    d->promptTagCompleter->setWidget(d->generate.editPrompt);
    d->generate.editPrompt->setTabChangesFocus(true);  // §13.196: Tab moves focus, not inserted
    d->generate.editPrompt->installEventFilter(dock);  // §13.196: Shift+Enter → Generate, ShortcutOverride
    d->generate.editPrompt->setPlaceholderText(ComfyTr::tr("Describe the content you want to see, or leave empty."));
    d->generate.editPrompt->setToolTip(ComfyTr::tr(
        "Tip: (word) for emphasis, [word] to reduce strength. Use commas to separate concepts. Shift+Enter to generate. "
        "Ctrl+Space for tag completion (CSV files in Settings → Interface)."));
    d->generate.editPrompt->setMaximumHeight(60);
    {
        d->generate.rootPromptColumnWidget = new QWidget(shell.genGroup);
        QWidget *promptColumn = d->generate.rootPromptColumnWidget;
        QVBoxLayout *promptColLayout = new QVBoxLayout(promptColumn);
        promptColLayout->setContentsMargins(0, 0, 0, 0);
        promptColLayout->setSpacing(0);
        promptColLayout->addWidget(d->generate.editPrompt);
        d->generate.promptResizeHandle = new ComfyPromptResizeHandle(
            d->generate.editPrompt,
            [dock, d](int lines) {
                QJsonObject st = ComfyUIUtils::loadSettingsJson();
                const bool live = d->comboWorkspace && d->comboWorkspace->currentIndex() == 2;
                st.insert(live ? QStringLiteral("prompt_line_count_live") : QStringLiteral("prompt_line_count"), lines);
                ComfyUIUtils::saveSettingsJson(st);
            },
            40,
            promptColumn);
        promptColLayout->addWidget(d->generate.promptResizeHandle);
        genContentLayout->addWidget(promptColumn);
    }

    d->generate.negativePromptBlock = new QWidget(dock);
    QVBoxLayout *negBlockLayout = new QVBoxLayout(d->generate.negativePromptBlock);
    negBlockLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *negativePromptRow = new QHBoxLayout();
    negativePromptRow->addWidget(new QLabel(ComfyTr::tr("Negative prompt:")));
    negativePromptRow->addStretch();
    d->generate.labelNegativePromptAlert = new QLabel(d->generate.negativePromptBlock);
    d->generate.labelNegativePromptAlert->setToolTip(ComfyTr::tr("The selected Style does not use the negative prompt."));
    d->generate.labelNegativePromptAlert->setPixmap(
        ComfyTheme::icon(QStringLiteral("alert")).pixmap(16, 16));
    d->generate.labelNegativePromptAlert->setVisible(false);
    negativePromptRow->addWidget(d->generate.labelNegativePromptAlert);
    negBlockLayout->addLayout(negativePromptRow);
    d->generate.editNegative = new ComfyPromptPlainTextEdit(d->negativePromptTagCompleter);
    d->negativePromptTagCompleter->setWidget(d->generate.editNegative);
    d->generate.editNegative->setTabChangesFocus(true);  // §13.196: Tab moves focus
    d->generate.editNegative->setPlaceholderText(ComfyTr::tr("Describe content you want to avoid."));
    d->generate.editNegative->setToolTip(ComfyTr::tr("Ctrl+Space: tag completion (same lists as positive prompt)."));
    d->generate.editNegative->setMaximumHeight(400);
    d->generate.editNegative->installEventFilter(dock);
    negBlockLayout->addWidget(d->generate.editNegative);
    d->generate.negativeResizeHandle = new ComfyPromptResizeHandle(
        d->generate.editNegative,
        [dock, d](int lines) {
            QJsonObject st = ComfyUIUtils::loadSettingsJson();
            st.insert(QStringLiteral("negative_prompt_line_count"), lines);
            ComfyUIUtils::saveSettingsJson(st);
        },
        28,
        d->generate.negativePromptBlock);
    negBlockLayout->addWidget(d->generate.negativeResizeHandle);
    genContentLayout->addWidget(d->generate.negativePromptBlock);
    dock->updateNegativePromptAlertVisibility();  // §13.143: initial state (e.g. "None" selected)

    // §13.48: Tag autocomplete — shared model, Ctrl+Space in positive/negative prompts
    QObject::connect(d->promptTagCompleter, QOverload<const QString &>::of(&QCompleter::activated), dock, [dock, d](const QString &text) {
        QPlainTextEdit *pos = d->generate.regionPromptWidget
                                  ? d->generate.regionPromptWidget->positivePromptEditor()
                                  : d->generate.editPrompt;
        dock->insertPromptTagCompletion(pos, text);
    });
    QObject::connect(d->negativePromptTagCompleter, QOverload<const QString &>::of(&QCompleter::activated), dock, [dock, d](const QString &text) {
        QPlainTextEdit *neg = d->generate.regionPromptWidget
                                  ? d->generate.regionPromptWidget->negativePromptEditor()
                                  : d->generate.editNegative;
        dock->insertPromptTagCompletion(neg, text);
    });
    dock->refreshPromptTagCompleter();
    QObject::connect(d->generate.editPrompt, &QPlainTextEdit::textChanged, dock, &ComfyUIRemoteDock::updateUpscaleUsePromptLabel);

    d->generate.stepsParametersWidget = new QWidget(dock);
    QHBoxLayout *stepsCfgRow = new QHBoxLayout(d->generate.stepsParametersWidget);
    stepsCfgRow->setContentsMargins(0, 0, 0, 0);
    d->generate.spinSteps = new QSpinBox();
    d->generate.spinSteps->setRange(1, 150);
    d->generate.spinSteps->setValue(20);
    d->generate.spinSteps->setToolTip(ComfyTr::tr("Sampler steps"));
    d->generate.spinCfg = new QDoubleSpinBox();
    d->generate.spinCfg->setRange(1.0, 30.0);
    d->generate.spinCfg->setValue(8.0);
    d->generate.spinCfg->setDecimals(1);
    d->generate.spinCfg->setToolTip(ComfyTr::tr("CFG scale (guidance strength)"));
    d->generate.comboSampler = new QComboBox();
    d->generate.comboSampler->setEditable(true);
    d->generate.comboSampler->addItem("euler");
    d->generate.comboSampler->addItem("euler_ancestral");
    d->generate.comboSampler->addItem("dpmpp_2m");
    d->generate.comboSampler->addItem("dpmpp_2s_ancestral");
    d->generate.comboSampler->addItem("heun");
    d->generate.comboSampler->addItem("dpm_2");
    d->generate.comboSampler->addItem("dpm_2_ancestral");
    d->generate.btnRefreshSamplers = new QPushButton(ComfyTr::tr("Refresh"));
    d->generate.btnRefreshSamplers->setToolTip(ComfyTr::tr("Load sampler list from server"));
    QObject::connect(d->generate.btnRefreshSamplers, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRefreshSamplers);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("Steps:")));
    stepsCfgRow->addWidget(d->generate.spinSteps);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("CFG:")));
    stepsCfgRow->addWidget(d->generate.spinCfg);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("Sampler:")));
    stepsCfgRow->addWidget(d->generate.comboSampler, 1);
    stepsCfgRow->addWidget(d->generate.btnRefreshSamplers);
    genContentLayout->addWidget(d->generate.stepsParametersWidget);

}

} // namespace ComfyDockUiBuilderGenerateInternal
