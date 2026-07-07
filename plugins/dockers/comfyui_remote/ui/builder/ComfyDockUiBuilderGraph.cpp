/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyDockUiBuilder.h"
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



namespace ComfyDockUiBuilder {

void buildGraphWorkspace(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    d->graphPlaceholderWidget = new QWidget(shell.genGroup);
    d->graphWorkflowEditorLayout = new QVBoxLayout(d->graphPlaceholderWidget);
    QLabel *graphLabel = new QLabel(ComfyTr::tr("Paste ComfyUI API JSON below, then click Generate (results in History)."));
    graphLabel->setWordWrap(true);
    d->graphWorkflowEditorLayout->addWidget(graphLabel);
    d->graphWorkflowEditorLayout->addWidget(d->live.checkUseReferenceImage);
    QPushButton *btnGraphLoadWorkflow = new QPushButton(ComfyTr::tr("Load from file…"), d->graphPlaceholderWidget);
    QObject::connect(btnGraphLoadWorkflow, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotLoadWorkflowFromFile);
    d->graphWorkflowEditorLayout->addWidget(btnGraphLoadWorkflow);
    d->graphWorkflowEditorLayout->addWidget(d->editCustomWorkflow);
    d->checkCustomGraphLive = new ComfyCheckBox(ComfyTr::tr("Continuous preview (re-capture each result)"), d->graphPlaceholderWidget);
    d->checkCustomGraphLive->setToolTip(ComfyTr::tr("Graph workspace live mode: re-export canvas and re-submit after each result."));
    QObject::connect(d->checkCustomGraphLive, &QCheckBox::toggled, dock, [dock, d](bool on) {
        d->customGraphLiveActive = on;
        if (on)
            d->customGraphLiveLastFingerprint.clear();
        if (!on && d->customGraphLiveTimer)
            d->customGraphLiveTimer->stop();
    });
    d->graphWorkflowEditorLayout->addWidget(d->checkCustomGraphLive);
    d->customWorkflowParamsGroup->setParent(d->graphPlaceholderWidget);
    d->graphWorkflowEditorLayout->addWidget(d->customWorkflowParamsGroup);
    // §13.170: Open Web UI — open client.url in default browser (QDesktopServices::openUrl)
    QPushButton *btnOpenWebUI = new QPushButton(ComfyTr::tr("Open Web UI"));
    btnOpenWebUI->setToolTip(ComfyTr::tr("Open Web UI to create custom workflows"));
    QObject::connect(btnOpenWebUI, &QPushButton::clicked, dock, [dock, d](bool) {
        QString urlStr = d->editServerUrl->text().trimmed();
        if (urlStr.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Set server URL in Settings first."), true);
            return;
        }
        QUrl url(urlStr);
        if (!url.scheme().isEmpty() && url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
            urlStr = QStringLiteral("http://") + urlStr;
        else if (url.scheme().isEmpty())
            urlStr = QStringLiteral("http://") + urlStr;
        QDesktopServices::openUrl(QUrl(urlStr));
        dock->beginWebWorkflowSwitch();
    });
    d->graphWorkflowEditorLayout->addWidget(btnOpenWebUI);
    QPushButton *btnOpenSettingsForGraph = new QPushButton(ComfyTr::tr("Open Settings"));
    QObject::connect(btnOpenSettingsForGraph, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotConfigureHelp);
    d->graphWorkflowEditorLayout->addWidget(btnOpenSettingsForGraph);
    d->graphPlaceholderWidget->setVisible(false);
    shell.genLayout->addWidget(d->graphPlaceholderWidget);
}


} // namespace ComfyDockUiBuilder
