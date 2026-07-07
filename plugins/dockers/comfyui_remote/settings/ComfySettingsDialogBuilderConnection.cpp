/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilder.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyFormUi.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <KSharedConfig>
#include <KConfigGroup>

namespace ComfySettingsDialogBuilder {

void buildConnectionTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QDialog *dlg = ctx.dialog;

    d->connectionStack = new QStackedWidget(dlg);

    ComfyFormUi::ScrollTab tab = ComfyFormUi::createScrollTab(dlg, ComfyTr::tr("Server Configuration"));
    tab.innerLayout->addWidget(ComfyFormUi::addLineEditBlock(
        tab.inner,
        ComfyTr::tr("Server URL"),
        ComfyTr::tr("URL used to connect to a running ComfyUI server. Default is 127.0.0.1:8188 (local)."),
        d->editServerUrl));

    d->btnTest = new QPushButton(ComfyTr::tr("Connect"), tab.inner);
    d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("web-connection")));
    ComfyUiStyle::applyPrimaryButton(d->btnTest);
    QObject::connect(d->btnTest, &QPushButton::clicked, dock, [dock, d](bool) {
        if (d->isConnected)
            dock->slotDisconnect();
        else {
            d->connectionAutostartActive = false;
            dock->cancelConnectionAutostartRetry();
            dock->slotTestConnection();
        }
    });
    tab.innerLayout->addWidget(d->btnTest);

    d->labelConnectionStatus = new QLabel(tab.inner);
    ComfyUiStyle::styleDescription(d->labelConnectionStatus);
    tab.innerLayout->addWidget(d->labelConnectionStatus);

    tab.innerLayout->addWidget(ComfyFormUi::makeBoldHeader(tab.inner, ComfyTr::tr("Detected base models:")));
    d->labelDetectedModels = new QLabel(
        ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."), tab.inner);
    d->labelDetectedModels->setWordWrap(true);
    ComfyUiStyle::styleDescription(d->labelDetectedModels);
    tab.innerLayout->addWidget(d->labelDetectedModels);

    auto *connHelp = new QLabel(
        ComfyTr::tr("Install the required custom nodes and models on your ComfyUI server. Check the client.log file for more details."),
        tab.inner);
    connHelp->setWordWrap(true);
    tab.innerLayout->addWidget(connHelp);
    tab.innerLayout->addStretch();

    d->connectionStack->addWidget(tab.page);
    stack->addWidget(d->connectionStack);

    KConfigGroup connectionCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    const QString initialMode = connectionCfg.readEntry("ServerMode", QStringLiteral("external"));
    if (initialMode != QLatin1String("external")) {
        connectionCfg.writeEntry("ServerMode", QStringLiteral("external"));
        KSharedConfig::openConfig()->sync();
    }
}

} // namespace ComfySettingsDialogBuilder
