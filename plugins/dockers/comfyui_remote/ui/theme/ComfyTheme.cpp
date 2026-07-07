/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyTheme.h"
#include "ComfyUiStyle.h"

#include "ComfyResources.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QPalette>
#include <QSvgRenderer>
#include <QWidget>

static void initComfyThemeIconResources()
{
    Q_INIT_RESOURCE(comfy_theme_icons);
}

namespace ComfyTheme {

void ensureThemeResourcesLoaded()
{
    static bool loaded = false;
    if (!loaded) {
        initComfyThemeIconResources();
        loaded = true;
    }
}

namespace {

QString themeSuffix()
{
    return isDarkTheme() ? QStringLiteral("dark") : QStringLiteral("light");
}

QIcon iconFromPath(const QString &path)
{
    if (path.isEmpty())
        return QIcon();
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgRenderer renderer;
        if (path.startsWith(QLatin1Char(':'))) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QIcon();
            renderer.load(file.readAll());
        } else {
            renderer.load(path);
        }
        if (!renderer.isValid())
            return QIcon();
        QSize size = renderer.defaultSize();
        if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
            size = QSize(32, 32);
        QPixmap pix(size);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        renderer.render(&painter);
        painter.end();
        return QIcon(pix);
    }
    const QIcon fileIcon(path);
    return fileIcon.isNull() ? QIcon() : fileIcon;
}

} // namespace

bool isDarkTheme()
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

Palette palette()
{
    const QPalette pal = QGuiApplication::palette();
    Palette p;
    p.base = pal.color(QPalette::Base).name();
    p.active = pal.color(QPalette::Highlight).name();
    if (isDarkTheme()) {
        p.green = QStringLiteral("#30b030");
        p.yellow = QStringLiteral("#c0c030");
        p.red = QStringLiteral("#d07a40");
        p.grey = QStringLiteral("#888");
        p.highlight = QStringLiteral("#8df");
        p.progressAlt = QStringLiteral("#a16207");
    } else {
        p.green = QStringLiteral("#209020");
        p.yellow = QStringLiteral("#706020");
        p.red = QStringLiteral("#c07630");
        p.grey = QStringLiteral("#606060");
        p.highlight = QStringLiteral("#357");
        p.progressAlt = QStringLiteral("#ca8a04");
    }
    p.line = pal.color(QPalette::Window).darker(120).name();
    p.lineBase = pal.color(QPalette::Base).darker(120).name();
    return p;
}

QString flatComboStyleSheet()
{
    return ComfyUiStyle::flatComboStyleSheet();
}

void applyFlatComboStyle(QComboBox *combo)
{
    ComfyUiStyle::applyComboBox(combo);
}

QString toolbarComboStyleSheet()
{
    return ComfyUiStyle::comboBoxStyleSheet();
}

void applyToolbarComboStyle(QComboBox *combo)
{
    ComfyUiStyle::applyComboBox(combo);
}

void applyFlatComboStyle(QWidget *widget)
{
    ComfyUiStyle::applyComboBox(widget);
}

QString kritaIconNameForThemeStem(const QString &stem)
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> h;
        static const struct {
            const char *stem;
            const char *kritaIcon;
        } rows[] = {
            {"workspace-generation", "tools-wizard"},
            {"workspace-upscaling", "view-zoom"},
            {"workspace-live", "view-refresh"},
            {"workspace-animation", "video-x-generic"},
            {"workspace-custom", "project-development-open"},
            {"apply", "dialog-ok"},
            {"apply-layer", "document-edit"},
            {"cancel", "dialog-cancel"},
            {"generate", "tools-wizard"},
            {"refine", "transform-scale"},
            {"refine-region", "transform-crop"},
            {"random", "random"},
            {"seed", "random"},
            {"settings", "configure"},
            {"save", "document-save"},
            {"discard", "edit-delete"},
            {"upload", "upload"},
            {"import", "document-import"},
            {"reset", "view-refresh"},
            {"edit", "edit-copy"},
            {"remove", "list-remove"},
            {"filter", "view-filter"},
            {"more", "overflow-menu"},
            {"queue-active", "run-build"},
            {"queue-inactive", "dialog-ok"},
            {"queue-upload", "network-transmit-receive"},
            {"queue-waiting", "chronometer"},
            {"play", "media-playback-start"},
            {"pause", "media-playback-pause"},
            {"record", "media-record"},
            {"record-active", "media-record"},
            {"region-add", "list-add"},
            {"region-prompt", "insert-text"},
            {"region-alpha", "draw-freehand"},
            {"region-alpha-active", "format-stroke-color"},
            {"root", "folder"},
            {"context", "edit-paste"},
            {"context-automatic", "system-run"},
            {"context-mask", "path-mask-edit"},
            {"context-layer", "layer-visible-on"},
            {"context-image", "image-x-generic"},
            {"fill", "fill-color"},
            {"fill-empty", "draw-eraser"},
            {"inpaint-automatic", "tools-wizard"},
            {"inpaint-fill", "fill-color"},
            {"inpaint-expand", "transform-scale"},
            {"inpaint-add_object", "list-add"},
            {"inpaint-remove_object", "list-remove"},
            {"inpaint-replace_background", "view-preview"},
            {"inpaint-custom", "preferences-desktop-color"},
            {"control-add", "list-add"},
            {"control-generate", "tools-wizard"},
            {"add-pose", "edit-image"},
            {"control-reference", "link"},
            {"control-style", "color-picker-black"},
            {"control-composition", "view-grid"},
            {"control-face", "im-user"},
            {"control-inpaint", "draw-brush"},
            {"control-universal", "applications-graphics"},
            {"control-scribble", "draw-freehand"},
            {"control-line_art", "draw-line"},
            {"control-soft_edge", "blur"},
            {"control-canny_edge", "path-shape"},
            {"control-depth", "view-media-visualization"},
            {"control-normal", "map-flat"},
            {"control-pose", "edit-image"},
            {"control-segmentation", "select-rectangular"},
            {"control-hands", "preferences-desktop-peripherals"},
            {"control-blur", "blur"},
            {"control-stencil", "draw-brush"},
            {"link", "link"},
            {"link-active", "link"},
            {"link-off", "link-off"},
            {"link-disabled", "link-off"},
            {"warning", "dialog-warning"},
            {"alert", "dialog-warning"},
            {"interstice", "internet-web-browser"},
            {"resolution-multiplier", "zoom-original"},
            {"file-json", "text-x-ldif"},
            {"file-kra", "application-x-krita"},
            {"web-connection", "network-connect"},
            {"comfyui", "applications-graphics"},
            {"star", "rating"},
            {"logo-128", "view-preview"},
            {"sd-version-15", "applications-graphics"},
            {"sd-version-xl", "applications-graphics"},
            {"sd-version-3", "applications-graphics"},
            {"sd-version-flux", "applications-graphics"},
            {"sd-version-flux-k", "applications-graphics"},
            {"sd-version-flux-2", "applications-graphics"},
            {"sd-version-illu", "applications-graphics"},
            {"sd-version-illu-v", "applications-graphics"},
            {"sd-version-chroma", "applications-graphics"},
            {"sd-version-qwen", "applications-graphics"},
            {"sd-version-z-image", "applications-graphics"},
            {"sd-version-anima", "applications-graphics"},
            {"sd-version-ernie", "applications-graphics"},
        };
        for (const auto &r : rows)
            h.insert(QString::fromLatin1(r.stem), QString::fromLatin1(r.kritaIcon));
        return h;
    }();
    const QString v = map.value(stem);
    return v.isEmpty() ? QStringLiteral("applications-graphics") : v;
}

QIcon icon(const QString &stem)
{
    ensureThemeResourcesLoaded();
    const QString path = ComfyUIUtils::findBundledThemeIconFile(stem, themeSuffix());
    return iconFromPath(path);
}

QIcon checkpointIcon(ComfyResources::Arch arch, bool warn)
{
    if (warn)
        return icon(QStringLiteral("warning"));
    switch (arch) {
    case ComfyResources::Arch::Sd15:
        return icon(QStringLiteral("sd-version-15"));
    case ComfyResources::Arch::Sdxl:
        return icon(QStringLiteral("sd-version-xl"));
    case ComfyResources::Arch::Sd3:
        return icon(QStringLiteral("sd-version-3"));
    case ComfyResources::Arch::Flux:
        return icon(QStringLiteral("sd-version-flux"));
    case ComfyResources::Arch::FluxK:
        return icon(QStringLiteral("sd-version-flux-k"));
    case ComfyResources::Arch::Flux2_4b:
    case ComfyResources::Arch::Flux2_9b:
        return icon(QStringLiteral("sd-version-flux-2"));
    case ComfyResources::Arch::Illu:
        return icon(QStringLiteral("sd-version-illu"));
    case ComfyResources::Arch::IlluV:
        return icon(QStringLiteral("sd-version-illu-v"));
    case ComfyResources::Arch::Chroma:
        return icon(QStringLiteral("sd-version-chroma"));
    case ComfyResources::Arch::Qwen:
    case ComfyResources::Arch::QwenE:
    case ComfyResources::Arch::QwenEP:
    case ComfyResources::Arch::QwenL:
        return icon(QStringLiteral("sd-version-qwen"));
    case ComfyResources::Arch::ZImage:
        return icon(QStringLiteral("sd-version-z-image"));
    case ComfyResources::Arch::Unknown:
        break;
    }
    return icon(QStringLiteral("warning"));
}

QIcon checkpointIconForArchitectureKey(const QString &architectureKey, const QString &fallbackCheckpoint)
{
    ComfyResources::Arch arch = ComfyResources::archFromKey(architectureKey);
    if (arch == ComfyResources::Arch::Unknown && !fallbackCheckpoint.trimmed().isEmpty())
        arch = ComfyResources::archFromCheckpointName(fallbackCheckpoint);
    return checkpointIcon(arch);
}

QPixmap logoPixmap(int size)
{
    const QString path =
        ComfyUIUtils::findBundledThemeIconFile(QStringLiteral("logo-128"), QStringLiteral("light"));
    if (path.isEmpty()) {
        const QString legacy =
            ComfyUIUtils::pluginInstallDataDir() + QStringLiteral("/icons/logo-128.png");
        QPixmap pix(legacy);
        if (!pix.isNull()) {
            if (size > 0 && (pix.width() != size || pix.height() != size))
                pix = pix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return pix;
        }
        return QPixmap();
    }
    QPixmap pix(path);
    if (size > 0 && !pix.isNull() && (pix.width() != size || pix.height() != size))
        pix = pix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return pix;
}

} // namespace ComfyTheme
