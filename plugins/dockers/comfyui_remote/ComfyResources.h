/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_RESOURCES_H_
#define COMFY_RESOURCES_H_

#include <QString>

namespace ComfyResources {

/// Diffusion architecture keys (match ai_diffusion/resources.py Arch → string keys).
enum class Arch {
    Unknown,
    Sd15,
    Sdxl,
    Sd3,
    Flux,
    FluxK,
    Flux2_4b,
    Flux2_9b,
    Illu,
    IlluV,
    Chroma,
    Qwen,
    QwenE,
    QwenEP,
    QwenL,
    ZImage,
};

QString archToKey(Arch arch);
Arch archFromKey(const QString &key);
Arch archFromCheckpointName(const QString &checkpoint);
bool archFromCheckpointFilename(const QString &filename, Arch *outArch);

bool supportsCfg(Arch arch);
bool isEditArch(Arch arch);
bool isFluxLike(Arch arch);
bool isSdxlLike(Arch arch);
bool isQwenLike(Arch arch);
bool supportsRegions(Arch arch);
int latentCompressionFactor(Arch arch);

/// Control layer mode keys (ai_diffusion/resources.py ControlMode names).
namespace ControlMode {
extern const char reference[];
extern const char style[];
extern const char composition[];
extern const char face[];
extern const char scribble[];
extern const char line_art[];
extern const char soft_edge[];
extern const char canny_edge[];
extern const char depth[];
extern const char normal[];
extern const char pose[];
extern const char segmentation[];
extern const char blur[];
extern const char stencil[];
extern const char hands[];
extern const char inpaint[];
extern const char universal[];

bool isIpAdapter(const QString &mode);
bool isLines(const QString &mode);
bool isStructural(const QString &mode);
} // namespace ControlMode

/// Default ControlNet filename for ComfyUI ControlNetLoader (first match from bundled models.json / Python search_paths).
QString defaultControlNetFileName(Arch arch, const QString &mode);

} // namespace ComfyResources

#endif
