/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_THEME_H_
#define COMFY_THEME_H_

#include <QIcon>
#include <QPixmap>
#include <QString>

class QComboBox;
class QWidget;

namespace ComfyResources {
enum class Arch;
}

namespace ComfyTheme {

struct Palette {
    QString base;
    QString green;
    QString yellow;
    QString red;
    QString grey;
    QString highlight;
    QString progressAlt;
    QString active;
    QString line;
    QString lineBase;
};

bool isDarkTheme();
Palette palette();
QString flatComboStyleSheet();
void applyFlatComboStyle(QComboBox *combo);
void applyFlatComboStyle(QWidget *widget);

/// Krita/Breeze fallback when bundled SVG is missing (moved from ComfyUIUtils).
QString kritaIconNameForThemeStem(const QString &stem);

void ensureThemeResourcesLoaded();

/// Python theme.icon(name): bundled data/icons/{name}-{dark|light}.svg|.png from ai_diffusion.
QIcon icon(const QString &stem);

/// Python theme.checkpoint_icon(arch).
QIcon checkpointIcon(ComfyResources::Arch arch, bool warn = false);

QIcon checkpointIconForArchitectureKey(const QString &architectureKey, const QString &fallbackCheckpoint = QString());

/// Python theme.logo() — bundled logo-128.png or interstice-style fallback.
QPixmap logoPixmap(int size = 64);

} // namespace ComfyTheme

#endif
