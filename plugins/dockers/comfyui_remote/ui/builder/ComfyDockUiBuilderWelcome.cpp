/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyDockUiBuilder.h"
#include "ComfyGrid.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
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

void buildWelcomePage(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    // §5.2 Welcome view: logo, title "AI Image\nGeneration", connection status, Configure button, footer links
    d->welcomePage = new QWidget(shell.rootWidget);
    QVBoxLayout *welcomeLayout = new QVBoxLayout(d->welcomePage);
    QLabel *logoLabel = new QLabel(d->welcomePage);
    logoLabel->setFixedSize(64, 64);
    QPixmap logoPix = ComfyTheme::logoPixmap(64);
    if (!logoPix.isNull()) {
        logoLabel->setPixmap(logoPix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoLabel->setStyleSheet(ComfyUiStyle::logoPlaceholderStyleSheet());
    }
    QLabel *welcomeTitle = new QLabel(ComfyTr::tr("AI Image\nGeneration"), d->welcomePage);
    QFont titleFont = welcomeTitle->font();
    titleFont.setPointSize(qMax(12, titleFont.pointSize()));
    welcomeTitle->setFont(titleFont);
    welcomeTitle->setAlignment(Qt::AlignCenter);
    welcomeTitle->setTextFormat(Qt::PlainText);
    auto *headerGrid = new ComfyGridRow(d->welcomePage);
    headerGrid->addWidget(logoLabel, 3);
    headerGrid->addWidget(welcomeTitle, 9, 1);
    welcomeLayout->addWidget(headerGrid);
    welcomeLayout->addSpacing(12);
    // §13.190 order: AutoUpdateWidget (3), NewsWidget (4), ConnectionWidget (5). At most one visible.
    d->welcomeUpdateWidget = new QWidget(d->welcomePage);
    QVBoxLayout *updateLayout = new QVBoxLayout(d->welcomeUpdateWidget);
    d->welcomeUpdateTitleLabel = new QLabel(d->welcomeUpdateWidget);
    d->welcomeUpdateTitleLabel->setWordWrap(true);
    updateLayout->addWidget(d->welcomeUpdateTitleLabel);
    d->welcomeUpdateVersionLabel = new QLabel(d->welcomeUpdateWidget);
    d->welcomeUpdateVersionLabel->setWordWrap(true);
    d->welcomeUpdateVersionLabel->hide();
    updateLayout->addWidget(d->welcomeUpdateVersionLabel);
    d->welcomeUpdateProgressBar = new QProgressBar(d->welcomeUpdateWidget);
    d->welcomeUpdateProgressBar->setRange(0, 0);
    d->welcomeUpdateProgressBar->setTextVisible(false);
    d->welcomeUpdateProgressBar->hide();
    updateLayout->addWidget(d->welcomeUpdateProgressBar);
    d->welcomeCheckAutoUpdate = new ComfyCheckBox(ComfyTr::tr("Check for updates on startup"), d->welcomeUpdateWidget);
    d->welcomeCheckAutoUpdate->setChecked(ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true));
    d->welcomeCheckAutoUpdate->setToolTip(ComfyTr::tr("When enabled, the Welcome view will check for a new plugin version when shown."));
    QObject::connect(d->welcomeCheckAutoUpdate, &QCheckBox::toggled, dock, [dock, d](bool checked) {
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        s.insert(QStringLiteral("auto_update"), checked);
        ComfyUIUtils::saveSettingsJson(s);
        dock->updateWelcomeVisibility();
    });
    updateLayout->addWidget(d->welcomeCheckAutoUpdate);
    d->welcomeUpdateButton = new QPushButton(ComfyTr::tr("Download and Install"), d->welcomeUpdateWidget);
    ComfyUiStyle::applyPrimaryButton(d->welcomeUpdateButton);
    QObject::connect(d->welcomeUpdateButton, &QPushButton::clicked, dock, [dock, d](bool) {
        if (d->pluginUpdateState == ComfyUIRemoteDock::Private::PluginUpdateState::RestartRequired) {
            const QString p = d->updateExtractPath;
            if (!p.isEmpty() && (QFileInfo::exists(p)))
                QDesktopServices::openUrl(QUrl::fromLocalFile(p));
            return;
        }
        dock->startPluginUpdateDownload();
    });
    updateLayout->addWidget(d->welcomeUpdateButton);
    d->welcomeUpdateWidget->hide();
    welcomeLayout->addWidget(d->welcomeUpdateWidget);
    d->welcomeNewsWidget = new QWidget(d->welcomePage);
    QVBoxLayout *newsLayout = new QVBoxLayout(d->welcomeNewsWidget);
    d->welcomeNewsLabel = new QLabel(d->welcomeNewsWidget);
    d->welcomeNewsLabel->setWordWrap(true);
    d->welcomeNewsLabel->setObjectName(QStringLiteral("newsText"));
    newsLayout->addWidget(d->welcomeNewsLabel);
    QPushButton *btnNewsOk = new QPushButton(ComfyTr::tr("Ok"), d->welcomeNewsWidget);
    ComfyUiStyle::applyPrimaryButton(btnNewsOk);
    QObject::connect(btnNewsOk, &QPushButton::clicked, dock, [dock, d](bool) {
        d->hasUnseenNews = false;
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        s.insert(QStringLiteral("last_news"), d->lastNewsDigest);
        ComfyUIUtils::saveSettingsJson(s);
        dock->updateWelcomeVisibility();
    });
    newsLayout->addWidget(btnNewsOk);
    d->welcomeNewsWidget->hide();
    welcomeLayout->addWidget(d->welcomeNewsWidget);
    d->welcomeConnectionWidget = new QWidget(d->welcomePage);
    QVBoxLayout *connWidgetLayout = new QVBoxLayout(d->welcomeConnectionWidget);
    connWidgetLayout->setContentsMargins(0, 0, 0, 0);
    d->welcomeStatusLabel = new QLabel(ComfyTr::tr("Not connected to server."), d->welcomeConnectionWidget);
    d->welcomeStatusLabel->setWordWrap(true);
    d->welcomeErrorLabel = new QLabel(d->welcomeConnectionWidget);
    d->welcomeErrorLabel->setWordWrap(true);
    d->welcomeErrorLabel->setStyleSheet(ComfyUiStyle::warningLabelStyleSheet());
    d->welcomeErrorLabel->hide();
    QPushButton *btnConfigure = new QPushButton(ComfyTr::tr("Configure"), d->welcomeConnectionWidget);
    btnConfigure->setIcon(ComfyTheme::icon(QStringLiteral("settings")));
    ComfyUiStyle::applyPrimaryButton(btnConfigure);
    QObject::connect(btnConfigure, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotConfigureHelp);
    connWidgetLayout->addWidget(d->welcomeStatusLabel);
    connWidgetLayout->addWidget(d->welcomeErrorLabel);
    connWidgetLayout->addWidget(btnConfigure);
    welcomeLayout->addWidget(d->welcomeConnectionWidget);
    welcomeLayout->addStretch();
    shell.mainStack->addWidget(d->welcomePage);
}


} // namespace ComfyDockUiBuilder
