/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyResources.h"

#include <QVector>

namespace ComfyResources {

namespace ControlMode {
const char reference[] = "reference";
const char style[] = "style";
const char composition[] = "composition";
const char face[] = "face";
const char scribble[] = "scribble";
const char line_art[] = "line_art";
const char soft_edge[] = "soft_edge";
const char canny_edge[] = "canny_edge";
const char depth[] = "depth";
const char normal[] = "normal";
const char pose[] = "pose";
const char segmentation[] = "segmentation";
const char blur[] = "blur";
const char stencil[] = "stencil";
const char hands[] = "hands";
const char inpaint[] = "inpaint";
const char universal[] = "universal";

bool isIpAdapter(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    return m == QLatin1String(reference) || m == QLatin1String(face) || m == QLatin1String(style)
           || m == QLatin1String(composition);
}

bool isLines(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    return m == QLatin1String(scribble) || m == QLatin1String(line_art) || m == QLatin1String(soft_edge)
           || m == QLatin1String(canny_edge);
}

bool isStructural(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String(inpaint) || isIpAdapter(m))
        return false;
    return true;
}
} // namespace ControlMode

QString archToKey(Arch arch)
{
    switch (arch) {
    case Arch::Sd15:
        return QStringLiteral("sd15");
    case Arch::Sdxl:
        return QStringLiteral("sdxl");
    case Arch::Sd3:
        return QStringLiteral("sd3");
    case Arch::Flux:
        return QStringLiteral("flux");
    case Arch::FluxK:
        return QStringLiteral("flux_k");
    case Arch::Flux2_4b:
        return QStringLiteral("flux2_4b");
    case Arch::Flux2_9b:
        return QStringLiteral("flux2_9b");
    case Arch::Illu:
        return QStringLiteral("illu");
    case Arch::IlluV:
        return QStringLiteral("illu_v");
    case Arch::Chroma:
        return QStringLiteral("chroma");
    case Arch::Qwen:
        return QStringLiteral("qwen");
    case Arch::QwenE:
        return QStringLiteral("qwen_e");
    case Arch::QwenEP:
        return QStringLiteral("qwen_e_p");
    case Arch::QwenL:
        return QStringLiteral("qwen_l");
    case Arch::ZImage:
        return QStringLiteral("zimage");
    default:
        return QStringLiteral("unknown");
    }
}

Arch archFromKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("sd15"))
        return Arch::Sd15;
    if (k == QLatin1String("sdxl"))
        return Arch::Sdxl;
    if (k == QLatin1String("sd3"))
        return Arch::Sd3;
    if (k == QLatin1String("flux_k"))
        return Arch::FluxK;
    if (k == QLatin1String("flux2_4b"))
        return Arch::Flux2_4b;
    if (k == QLatin1String("flux2_9b"))
        return Arch::Flux2_9b;
    if (k == QLatin1String("flux"))
        return Arch::Flux;
    if (k == QLatin1String("illu_v"))
        return Arch::IlluV;
    if (k == QLatin1String("illu"))
        return Arch::Illu;
    if (k == QLatin1String("chroma"))
        return Arch::Chroma;
    if (k == QLatin1String("qwen_e_p"))
        return Arch::QwenEP;
    if (k == QLatin1String("qwen_e"))
        return Arch::QwenE;
    if (k == QLatin1String("qwen_l"))
        return Arch::QwenL;
    if (k == QLatin1String("qwen"))
        return Arch::Qwen;
    if (k == QLatin1String("zimage"))
        return Arch::ZImage;
    return Arch::Unknown;
}

QString archDisplayName(Arch arch)
{
    switch (arch) {
    case Arch::Sd15:
        return QStringLiteral("SD 1.5");
    case Arch::Sdxl:
        return QStringLiteral("SD XL");
    case Arch::Sd3:
        return QStringLiteral("SD 3");
    case Arch::Flux:
        return QStringLiteral("Flux");
    case Arch::FluxK:
        return QStringLiteral("Flux Kontext");
    case Arch::Flux2_4b:
        return QStringLiteral("Flux 2 Klein 4B");
    case Arch::Flux2_9b:
        return QStringLiteral("Flux 2 Klein 9B");
    case Arch::Illu:
        return QStringLiteral("Illustrious");
    case Arch::IlluV:
        return QStringLiteral("Illustrious v-prediction");
    case Arch::Chroma:
        return QStringLiteral("Chroma");
    case Arch::Qwen:
        return QStringLiteral("Qwen");
    case Arch::QwenE:
        return QStringLiteral("Qwen Edit");
    case Arch::QwenEP:
        return QStringLiteral("Qwen Edit Plus");
    case Arch::QwenL:
        return QStringLiteral("Qwen Layered");
    case Arch::ZImage:
        return QStringLiteral("Z-Image");
    default:
        return archToKey(arch);
    }
}

QString architectureKeyDisplayName(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("auto"))
        return QStringLiteral("Automatic");
    const Arch a = archFromKey(k);
    if (a != Arch::Unknown)
        return archDisplayName(a);
    return key;
}

QVector<QString> validArchitectureKeysForResolvedArch(Arch resolved)
{
    if (isSdxlLike(resolved))
        return {QStringLiteral("auto"), QStringLiteral("sdxl"), QStringLiteral("illu"), QStringLiteral("illu_v")};
    if (isFlux2(resolved))
        return {QStringLiteral("auto"), QStringLiteral("flux2_4b"), QStringLiteral("flux2_9b")};
    if (resolved == Arch::Flux || resolved == Arch::FluxK)
        return {QStringLiteral("auto"), QStringLiteral("flux"), QStringLiteral("flux_k")};
    if (isQwenLike(resolved))
        return {QStringLiteral("auto"), QStringLiteral("qwen"), QStringLiteral("qwen_e"), QStringLiteral("qwen_e_p"), QStringLiteral("qwen_l")};
    QVector<QString> keys;
    keys.append(QStringLiteral("auto"));
    if (resolved != Arch::Unknown) {
        const QString k = archToKey(resolved);
        if (k != QLatin1String("unknown"))
            keys.append(k);
    }
    return keys;
}

Arch archFromCheckpointName(const QString &checkpoint)
{
    Arch a = Arch::Unknown;
    archFromCheckpointFilename(checkpoint, &a);
    return a;
}

bool archFromCheckpointFilename(const QString &filename, Arch *outArch)
{
    if (!outArch)
        return false;
    const QString lower = filename.trimmed().toLower();
    if (lower.isEmpty()) {
        *outArch = Arch::Unknown;
        return false;
    }
    if (lower.contains(QLatin1String("flux_k")) || lower.contains(QLatin1String("flux-k"))
        || lower.contains(QLatin1String("kontext"))) {
        *outArch = Arch::FluxK;
        return true;
    }
    if (lower.contains(QLatin1String("qwen")) && lower.contains(QLatin1String("layered"))) {
        *outArch = Arch::QwenL;
        return true;
    }
    if (lower.contains(QLatin1String("qwen")) && lower.contains(QLatin1String("edit"))) {
        if (lower.contains(QLatin1String("2509")) || lower.contains(QLatin1String("2511")))
            *outArch = Arch::QwenEP;
        else
            *outArch = Arch::QwenE;
        return true;
    }
    if (lower.contains(QLatin1String("qwen"))) {
        *outArch = Arch::Qwen;
        return true;
    }
    if ((lower.contains(QLatin1String("flux2")) || lower.contains(QLatin1String("klein")))
         && lower.contains(QLatin1String("flux"))) {
        if (lower.contains(QLatin1String("9b")))
            *outArch = Arch::Flux2_9b;
        else
            *outArch = Arch::Flux2_4b;
        return true;
    }
    if (lower.contains(QLatin1String("zimage")) || lower.contains(QLatin1String("z-image"))) {
        *outArch = Arch::ZImage;
        return true;
    }
    if (lower.contains(QLatin1String("chroma"))) {
        *outArch = Arch::Chroma;
        return true;
    }
    if (lower.contains(QLatin1String("illustrious")) || lower.contains(QLatin1String("illu"))) {
        if (lower.contains(QLatin1String("v-pred")) || lower.contains(QLatin1String("vpred")))
            *outArch = Arch::IlluV;
        else
            *outArch = Arch::Illu;
        return true;
    }
    if (lower.contains(QLatin1String("flux"))) {
        *outArch = Arch::Flux;
        return true;
    }
    if (lower.contains(QLatin1String("sd3"))) {
        *outArch = Arch::Sd3;
        return true;
    }
    if (lower.contains(QLatin1String("sdxl")) || lower.contains(QLatin1String("xl"))
        || lower.contains(QLatin1String("turbo"))) {
        *outArch = Arch::Sdxl;
        return true;
    }
    if (lower.contains(QLatin1String("v1-5")) || lower.contains(QLatin1String("sd1.5"))
        || lower.contains(QLatin1String("sd15")) || lower.contains(QLatin1String("v1.5"))) {
        *outArch = Arch::Sd15;
        return true;
    }
    *outArch = Arch::Unknown;
    return false;
}

bool supportsCfg(Arch arch)
{
    return arch != Arch::Flux && arch != Arch::FluxK;
}

bool isEditArch(Arch arch)
{
    return arch == Arch::FluxK || arch == Arch::QwenE || arch == Arch::QwenEP || arch == Arch::QwenL;
}

bool isFluxLike(Arch arch)
{
    return arch == Arch::Flux || arch == Arch::FluxK || arch == Arch::Flux2_4b || arch == Arch::Flux2_9b;
}

bool isFlux2(Arch arch)
{
    return arch == Arch::Flux2_4b || arch == Arch::Flux2_9b;
}

bool isSdxlLike(Arch arch)
{
    return arch == Arch::Sdxl || arch == Arch::Illu || arch == Arch::IlluV;
}

bool isQwenLike(Arch arch)
{
    return arch == Arch::Qwen || arch == Arch::QwenE || arch == Arch::QwenEP || arch == Arch::QwenL;
}

bool supportsEditInstructions(Arch arch)
{
    return isEditArch(arch) || arch == Arch::Flux2_4b || arch == Arch::Flux2_9b;
}

bool hasControlnetInpaint(Arch arch)
{
    return !defaultControlNetFileName(arch, ControlMode::inpaint).isEmpty();
}

bool supportsRegions(Arch arch)
{
    return arch == Arch::Sd15 || arch == Arch::Sdxl || arch == Arch::Illu || arch == Arch::IlluV;
}

bool supportsClipSkip(Arch arch)
{
    return arch == Arch::Sd15 || arch == Arch::Sdxl || arch == Arch::Illu || arch == Arch::IlluV;
}

bool supportsAttentionGuidance(Arch arch)
{
    return supportsClipSkip(arch);
}

int latentCompressionFactor(Arch arch)
{
    if (arch == Arch::Sd3 || arch == Arch::Flux2_4b || arch == Arch::Flux2_9b)
        return 16;
    return 8;
}

QString defaultControlNetFileName(Arch arch, const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    auto sd15 = [&](const char *file) -> QString {
        if (arch == Arch::Sd15 || arch == Arch::Unknown)
            return QString::fromUtf8(file);
        return QString();
    };
    auto sdxl = [&](const char *file) -> QString {
        if (arch == Arch::Sdxl || arch == Arch::Illu || arch == Arch::IlluV)
            return QString::fromUtf8(file);
        return QString();
    };
    auto flux = [&](const char *file) -> QString {
        if (arch == Arch::Flux || arch == Arch::Flux2_4b || arch == Arch::Flux2_9b)
            return QString::fromUtf8(file);
        return QString();
    };

    if (m == QLatin1String(ControlMode::depth)) {
        if (QString f = sd15("control_lora_rank128_v11f1p_sd15_depth_fp16.safetensors"); !f.isEmpty())
            return f;
        if (QString f = sdxl("control-lora-depth-rank256.safetensors"); !f.isEmpty())
            return f;
        if (QString f = flux("flux1-dev-controlnet-depth.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::canny_edge)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_canny_fp16.safetensors"); !f.isEmpty())
            return f;
        if (QString f = sdxl("control-lora-canny-rank256.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::scribble)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_scribble_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::line_art)) {
        if (QString f = sd15("control_v11p_sd15_lineart_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::soft_edge)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_softedge_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::normal)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_normalbae_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::pose)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_openpose_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::segmentation)) {
        if (QString f = sd15("control_lora_rank128_v11p_sd15_seg_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::blur)) {
        if (QString f = sd15("control_lora_rank128_v11f1e_sd15_tile_fp16.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::inpaint)) {
        if (QString f = sd15("control_v11p_sd15_inpaint_fp16.safetensors"); !f.isEmpty())
            return f;
        if (QString f = flux("flux1-dev-controlnet-inpaint.safetensors"); !f.isEmpty())
            return f;
    }
    if (m == QLatin1String(ControlMode::universal)) {
        if (QString f = sdxl("xinsir-controlnet-union-sdxl-1.0.safetensors"); !f.isEmpty())
            return f;
        if (QString f = flux("FLUX.1-dev-ControlNet-Union-Pro-2.0-fp8.safetensors"); !f.isEmpty())
            return f;
    }
    // Fallback: SDXL/Flux union for unknown structural modes on those arches
    if (arch == Arch::Sdxl || arch == Arch::Illu || arch == Arch::IlluV)
        return QStringLiteral("xinsir-controlnet-union-sdxl-1.0.safetensors");
    if (arch == Arch::Flux || arch == Arch::Flux2_4b || arch == Arch::Flux2_9b)
        return QStringLiteral("FLUX.1-dev-ControlNet-Union-Pro-2.0-fp8.safetensors");
    return QString();
}

QString unionControlNetTypeForMode(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String(ControlMode::pose))
        return QStringLiteral("openpose");
    if (m == QLatin1String(ControlMode::depth) || m == QLatin1String(ControlMode::hands))
        return QStringLiteral("depth");
    if (m == QLatin1String(ControlMode::scribble) || m == QLatin1String(ControlMode::soft_edge))
        return QStringLiteral("hed/pidi/scribble/ted");
    if (m == QLatin1String(ControlMode::line_art) || m == QLatin1String(ControlMode::canny_edge))
        return QStringLiteral("canny/lineart/anime_lineart/mlsd");
    if (m == QLatin1String(ControlMode::normal))
        return QStringLiteral("normal");
    if (m == QLatin1String(ControlMode::segmentation))
        return QStringLiteral("segment");
    if (m == QLatin1String(ControlMode::blur))
        return QStringLiteral("tile");
    return QStringLiteral("auto");
}

bool controlNetUsesUnionTypeNode(const QString &controlNetFileName, const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String(ControlMode::universal))
        return true;
    return controlNetFileName.contains(QStringLiteral("union"), Qt::CaseInsensitive);
}

bool isNunchakuCheckpointFilename(const QString &checkpointFile)
{
    return checkpointFile.contains(QStringLiteral("svdq"), Qt::CaseInsensitive);
}

DualClipLoadSpec defaultDualClipLoadSpec(Arch arch)
{
    DualClipLoadSpec spec;
    switch (arch) {
    case Arch::Sdxl:
    case Arch::Illu:
    case Arch::IlluV:
        spec.clipName1 = QStringLiteral("clip_l.safetensors");
        spec.clipName2 = QStringLiteral("clip_g.safetensors");
        spec.type = QStringLiteral("sdxl");
        break;
    case Arch::Sd3:
        spec.clipName1 = QStringLiteral("clip_l.safetensors");
        spec.clipName2 = QStringLiteral("clip_g.safetensors");
        spec.type = QStringLiteral("sd3");
        break;
    case Arch::Flux:
    case Arch::FluxK:
        spec.clipName1 = QStringLiteral("clip_l.safetensors");
        spec.clipName2 = QStringLiteral("t5xxl_fp16.safetensors");
        spec.type = QStringLiteral("flux");
        break;
    case Arch::Flux2_4b:
        spec.clipName1 = QStringLiteral("qwen_3_4b.safetensors");
        spec.clipName2 = QString();
        spec.type = QStringLiteral("flux2");
        break;
    case Arch::Flux2_9b:
        spec.clipName1 = QStringLiteral("qwen_3_8b.safetensors");
        spec.clipName2 = QString();
        spec.type = QStringLiteral("flux2");
        break;
    case Arch::Chroma:
        spec.clipName1 = QStringLiteral("t5xxl_fp16.safetensors");
        spec.type = QStringLiteral("chroma");
        break;
    case Arch::Qwen:
    case Arch::QwenE:
    case Arch::QwenEP:
    case Arch::QwenL:
        spec.clipName1 = QStringLiteral("qwen_2.5_vl_7b.safetensors");
        spec.type = QStringLiteral("qwen_image");
        break;
    case Arch::ZImage:
        spec.clipName1 = QStringLiteral("qwen_3_4b.safetensors");
        spec.type = QStringLiteral("lumina2");
        break;
    default:
        break;
    }
    return spec;
}

QString defaultZImageFunControlPatchFileName()
{
    return QStringLiteral("Z-Image-Turbo-Fun-Controlnet-Union-2.1-lite-2601-8steps.safetensors");
}

bool supportsIpAdapterWorkflow(Arch arch)
{
    return arch == Arch::Sd15 || isSdxlLike(arch);
}

QString defaultClipVisionFileName(Arch arch)
{
    if (supportsIpAdapterWorkflow(arch))
        return QStringLiteral("clip-vision_vit-h.safetensors");
    return QString();
}

QString defaultIpAdapterFileName(Arch arch, const QString &mode)
{
    Q_UNUSED(mode);
    if (arch == Arch::Sd15)
        return QStringLiteral("ip-adapter_sd15.safetensors");
    if (isSdxlLike(arch))
        return QStringLiteral("ip-adapter_sdxl_vit-h.safetensors");
    return QString();
}

QString defaultIpAdapterFaceFileName(Arch arch)
{
    if (arch == Arch::Sd15)
        return QStringLiteral("ip-adapter-faceid-plusv2_sd15.bin");
    if (isSdxlLike(arch))
        return QStringLiteral("ip-adapter-faceid-plusv2_sdxl.bin");
    return QString();
}

} // namespace ComfyResources
