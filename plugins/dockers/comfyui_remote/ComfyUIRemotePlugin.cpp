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

#include <QCoreApplication>
#include <QVersionNumber>

namespace {
// §13.30 / §13.40: optional runtime check (Python reference does not enforce; spec allows warning on older Krita).
void warnIfKritaBelowDocumentedMinimum()
{
    const QString raw = QCoreApplication::applicationVersion().trimmed();
    if (raw.isEmpty())
        return;
    const QString verToken = raw.section(QLatin1Char(' '), 0, 0);
    const QVersionNumber v = QVersionNumber::fromString(verToken);
    if (v.isNull())
        return;
    static const QVersionNumber kMin(5, 2, 0);
    if (v < kMin) {
        qWarning() << "ComfyUI Remote plugin: Krita" << raw
                   << "is below the documented minimum (5.2.0). Update Krita for full support.";
    }
}
} // namespace

K_PLUGIN_FACTORY_WITH_JSON(ComfyUIRemotePluginFactory, "kritacomfyuiremote.json", registerPlugin<ComfyUIRemotePlugin>();)

class ComfyUIRemoteDockFactory : public KoDockFactoryBase
{
public:
    // §10.2: same factory id as Python DockWidgetFactory ("imageDiffusion") for saved layout / docs parity
    QString id() const override {
        return QStringLiteral("imageDiffusion");
    }

    virtual Qt::DockWidgetArea defaultDockWidgetArea() const {
        return Qt::RightDockWidgetArea;
    }

    QDockWidget *createDockWidget() override {
        ComfyUIRemoteDock *dock = new ComfyUIRemoteDock();
        dock->setObjectName(id());
        return dock;
    }

    // §10.2: DockRight (visible); was DockMinimized so first open matched “off until menu” — spec wants right dock
    virtual DockPosition defaultDockPosition() const {
        return DockRight;
    }
};

ComfyUIRemotePlugin::ComfyUIRemotePlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    warnIfKritaBelowDocumentedMinimum();
    ComfyUIUtils::migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(); // §10.2: preserve layout after factory id change
    ComfyUIUtils::checkPluginInstallationPath();  // §13.165: warn if not in expected location
    KoDockRegistry::instance()->add(new ComfyUIRemoteDockFactory());
}

ComfyUIRemotePlugin::~ComfyUIRemotePlugin()
{
}

#include "ComfyUIRemotePlugin.moc"
