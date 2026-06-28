/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyPromptClient.h"

#include <QTimer>
#include <functional>

namespace ComfyPollRunnerCommon {

struct PollRunningConfig {
    int *pollCount = nullptr;
    int maxPollCount = 300;
    QTimer *pollTimer = nullptr;
    int intervalMs = 1000;
    /// Called on each Running response before timeout check (e.g. live progress).
    std::function<void()> onTick;
    /// Called when pollCount reaches maxPollCount.
    std::function<void()> onTimeout;
};

enum class HistoryPollOutcome {
    Handled,
    Ready,
};

/// Standard history poll dispatch: errors, running retry, timeout.
/// @return Ready when state is Done and images are present; Handled otherwise.
HistoryPollOutcome handleHistoryFetch(const ComfyPromptClient::HistoryFetchResult &result,
                                      const PollRunningConfig &running,
                                      const std::function<void(const ComfyPromptClient::HistoryFetchResult &)> &onTerminal);

} // namespace ComfyPollRunnerCommon
