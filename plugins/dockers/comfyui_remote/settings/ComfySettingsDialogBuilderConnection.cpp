/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilder.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyTheme.h"
#include "ComfySwitchWidget.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyStyleSamplerWidget.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>
#include <QVBoxLayout>

#include <KSharedConfig>
#include <KConfigGroup>

#include <functional>

namespace ComfySettingsDialogBuilder {

void buildConnectionTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QDialog *dlg = ctx.dialog;
        // Connection tab: this native build only supports connecting to a user-managed ComfyUI URL.
        d->connectionStack = new QStackedWidget(dlg);

        QWidget *connectionPage = new QWidget(dlg);
        QVBoxLayout *connectionLayout = new QVBoxLayout(connectionPage);
        QLabel *connTabHeading = new QLabel(ComfyTr::tr("Server Configuration"), connectionPage);
        QFont connTabHeadingFont = connTabHeading->font();
        connTabHeadingFont.setBold(true);
        connTabHeading->setFont(connTabHeadingFont);
        connectionLayout->addWidget(connTabHeading);

        QLabel *serverUrlDesc = new QLabel(ComfyTr::tr("URL used to connect to a running ComfyUI server. Default is 127.0.0.1:8188 (local)."), connectionPage);
        serverUrlDesc->setWordWrap(true);
        connectionLayout->addWidget(serverUrlDesc);
        QFormLayout *connForm = new QFormLayout();
        connForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        connForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        connForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        connForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        connForm->setHorizontalSpacing(12);
        connForm->setVerticalSpacing(6);
        connForm->addRow(ComfyTr::tr("Server URL:"), d->editServerUrl);
        connectionLayout->addLayout(connForm);

        d->btnTest = new QPushButton(ComfyTr::tr("Connect"), connectionPage);
        d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("web-connection")));
        QObject::connect(d->btnTest, &QPushButton::clicked, dock, [dock, d](bool) {
            if (d->isConnected)
                dock->slotDisconnect();
            else {
                d->connectionAutostartActive = false;
                dock->cancelConnectionAutostartRetry();
                dock->slotTestConnection();
            }
        });
        connectionLayout->addWidget(d->btnTest);

        d->labelConnectionStatus = new QLabel(connectionPage);
        d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        connectionLayout->addWidget(d->labelConnectionStatus);

        // §4.4 / §7.4: Detected base models — list of architectures with supported/missing status (populated when connected)
        QLabel *detectedModelsHeading = new QLabel(ComfyTr::tr("Detected base models:"), connectionPage);
        connectionLayout->addWidget(detectedModelsHeading);
        d->labelDetectedModels = new QLabel(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."), connectionPage);
        d->labelDetectedModels->setWordWrap(true);
        d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        connectionLayout->addWidget(d->labelDetectedModels);
        QLabel *connHelp = new QLabel(
            ComfyTr::tr("Install the required custom nodes and models on your ComfyUI server. Check the client.log file for more details."),
            connectionPage);
        connHelp->setWordWrap(true);
        connectionLayout->addWidget(connHelp);
        connectionLayout->addStretch();

        d->connectionStack->addWidget(connectionPage);
        stack->addWidget(d->connectionStack);

        KConfigGroup connectionCfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const QString initialMode = connectionCfg.readEntry("ServerMode", QStringLiteral("external"));
        if (initialMode != QLatin1String("external")) {
            connectionCfg.writeEntry("ServerMode", QStringLiteral("external"));
            KSharedConfig::openConfig()->sync();
        }

}


} // namespace ComfySettingsDialogBuilder
