/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * §13.30 / §13.40: This plugin requires Krita 5.2.0 or newer (documented minimum;
 * the reference Python implementation does not enforce this at runtime).
 */

#include "ComfyUIRemotePlugin.h"
#include "ComfyUIUtils.h"
#include <kpluginfactory.h>
#include <KoDockFactoryBase.h>
#include <KoDockRegistry.h>
#include "ComfyUIRemoteDock.h"

K_PLUGIN_FACTORY_WITH_JSON(ComfyUIRemotePluginFactory, "kritacomfyuiremote.json", registerPlugin<ComfyUIRemotePlugin>();)

class ComfyUIRemoteDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override {
        return QString("ComfyUIRemote");
    }

    virtual Qt::DockWidgetArea defaultDockWidgetArea() const {
        return Qt::RightDockWidgetArea;
    }

    QDockWidget *createDockWidget() override {
        ComfyUIRemoteDock *dock = new ComfyUIRemoteDock();
        dock->setObjectName(id());
        return dock;
    }

    virtual DockPosition defaultDockPosition() const {
        return DockMinimized;
    }
};

ComfyUIRemotePlugin::ComfyUIRemotePlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    ComfyUIUtils::checkPluginInstallationPath();  // §13.165: warn if not in expected location
    KoDockRegistry::instance()->add(new ComfyUIRemoteDockFactory());
}

ComfyUIRemotePlugin::~ComfyUIRemotePlugin()
{
}

#include "ComfyUIRemotePlugin.moc"
