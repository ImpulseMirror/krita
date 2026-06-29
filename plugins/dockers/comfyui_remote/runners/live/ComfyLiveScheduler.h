/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>

namespace ComfyLiveRunnerInternal {

struct LiveSchedulerState
{
    QByteArray lastFingerprint;
    qint64 lastChangeMs = 0;
    qint64 oldestChangeMs = 0;
    bool hasChanges = true;
    qint64 generationStartMs = 0;
    QList<qint64> generationTimesMs;

    void reset();
    bool shouldGenerate(const QByteArray &fingerprint, qint64 nowMs);
    void notifyGenerationStarted(qint64 nowMs);
    void notifyGenerationFinished(qint64 nowMs);
    int gracePeriodMs() const;
};

} // namespace ComfyLiveRunnerInternal
