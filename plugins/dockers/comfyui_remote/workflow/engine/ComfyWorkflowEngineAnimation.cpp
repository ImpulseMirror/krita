/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkflowEngine.h"

namespace ComfyWorkflowEngine {

qint64 animationFrameSeed(const qint64 batchBaseSeed, const int frameIndex, const int batchSeedStep)
{
    const int step = qMax(1, batchSeedStep);
    return batchBaseSeed + static_cast<qint64>(frameIndex) * static_cast<qint64>(step);
}

QJsonObject buildAnimationFrame(const AnimationFrameParams &params)
{
    TextToImageParams gen = params.base;
    gen.seed = animationFrameSeed(params.batchBaseSeed, params.frameIndex, params.batchSeedStep);
    return buildTextToImage(gen);
}

} // namespace ComfyWorkflowEngine
