/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPollRunnerCommon.h"

namespace ComfyPollRunnerCommon {

HistoryPollOutcome handleHistoryFetch(const ComfyPromptClient::HistoryFetchResult &result,
                                      const PollRunningConfig &running,
                                      const std::function<void(const ComfyPromptClient::HistoryFetchResult &)> &onTerminal)
{
    switch (result.state) {
    case ComfyPromptClient::HistoryState::NetworkError:
    case ComfyPromptClient::HistoryState::ExecutionError:
    case ComfyPromptClient::HistoryState::NoImages:
        if (onTerminal)
            onTerminal(result);
        return HistoryPollOutcome::Handled;
    case ComfyPromptClient::HistoryState::Running:
        if (running.pollCount)
            ++(*running.pollCount);
        if (running.onTick)
            running.onTick();
        if (running.pollCount && *running.pollCount >= running.maxPollCount) {
            if (running.onTimeout)
                running.onTimeout();
            return HistoryPollOutcome::Handled;
        }
        if (running.pollTimer)
            running.pollTimer->start(running.intervalMs);
        return HistoryPollOutcome::Handled;
    case ComfyPromptClient::HistoryState::Done:
        if (result.images.isEmpty()) {
            ComfyPromptClient::HistoryFetchResult noImages = result;
            noImages.state = ComfyPromptClient::HistoryState::NoImages;
            if (onTerminal)
                onTerminal(noImages);
            return HistoryPollOutcome::Handled;
        }
        return HistoryPollOutcome::Ready;
    }
    if (onTerminal)
        onTerminal(result);
    return HistoryPollOutcome::Handled;
}

} // namespace ComfyPollRunnerCommon
