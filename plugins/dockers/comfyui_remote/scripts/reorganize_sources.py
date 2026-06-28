#!/usr/bin/env python3
"""Move flat Comfy* sources into domain folders. Run from repo root or plugin dir."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

PLUGIN = Path(__file__).resolve().parent.parent
REPO = PLUGIN.parent.parent.parent

# Most specific prefix rules first -> relative destination directory
RULES: list[tuple[str, str]] = [
    (r"^ComfyUIRemotePlugin\.", "plugin/"),
    (r"^ComfyPromptClient\.", "network/"),
    (r"^ComfyUploadPipeline\.", "network/"),
    (r"^ComfyDockUiBuilderGenerate", "ui/builder/generate/"),
    (r"^ComfyDockUiBuilder", "ui/builder/"),
    (r"^ComfyGenerateUi\.", "ui/generate/"),
    (r"^ComfyTheme\.", "ui/theme/"),
    (r"^ComfySwitchWidget\.", "ui/widgets/"),
    (r"^ComfyQueueButton\.", "ui/widgets/"),
    (r"^ComfyWorkspaceSelectButton\.", "ui/widgets/"),
    (r"^ComfyPromptResizeHandle\.", "ui/widgets/"),
    (r"^ComfyUIIntervalSlider\.", "ui/widgets/"),
    (r"^ComfyUIPoseLayers\.", "ui/widgets/"),
    (r"^ComfyHistoryListWidget\.", "ui/widgets/"),
    (r"^ComfySettingsDialogBuilder", "settings/"),
    (r"^ComfyWorkflowEngine", "workflow/engine/"),
    (r"^ComfyPrepare", "workflow/prepare/"),
    (r"^ComfyUIWorkflows\.", "workflow/"),
    (r"^ComfyUIUtilsCustomWorkflow", "utils/custom_workflow/"),
    (r"^ComfyUIUtilsDocument", "utils/document/"),
    (r"^ComfyUIUtilsMask", "utils/mask/"),
    (r"^ComfyUIUtils", "utils/"),
    (r"^ComfyGenerateRunner", "runners/generate/"),
    (r"^ComfyInpaintRunner", "runners/inpaint/"),
    (r"^ComfyLiveRunner", "runners/live/"),
    (r"^ComfyUpscaleRunner", "runners/upscale/"),
    (r"^ComfyControlRunner", "runners/control/"),
    (r"^ComfyPollRunnerCommon\.", "runners/"),
    (r"^ComfyUIRemoteDockConnection", "dock/connection/"),
    (r"^ComfyConnectionInternal\.", "dock/connection/"),
    (r"^ComfyUIRemoteDockShellInternal\.", "dock/shell/"),
    (r"^ComfyUIRemoteDockGenerate\.", "dock/generate/"),
    (r"^ComfyUIRemoteDockInpaint", "dock/inpaint/"),
    (r"^ComfyUIRemoteDockLive\.", "dock/live/"),
    (r"^ComfyUIRemoteDockUpscale\.", "dock/upscale/"),
    (r"^ComfyUIRemoteDockControl", "dock/control/"),
    (r"^ComfyUIRemoteDockHistory\.", "dock/history/"),
    (r"^ComfyUIRemoteDockRegions", "dock/regions/"),
    (r"^ComfyUIRemoteDockDocument", "dock/document/"),
    (r"^ComfyUIRemoteDockAnimation\.", "dock/animation/"),
    (r"^ComfyUIRemoteDockWelcome\.", "dock/welcome/"),
    (r"^ComfyUIRemoteDockPromptUi\.", "dock/prompts/"),
    (r"^ComfyUIRemoteDockShortcuts\.", "dock/shortcuts/"),
    (r"^ComfyUIRemoteDockStyles\.", "dock/styles/"),
    (r"^ComfyUIRemoteDockPresets\.", "dock/presets/"),
    (r"^ComfyUIRemoteDockSettings\.", "dock/settings/"),
    (r"^ComfyUIRemoteDockCustomWorkflow\.", "dock/custom_workflow/"),
    (r"^ComfyUIRemoteDockWebWorkflow\.", "dock/custom_workflow/"),
    (r"^ComfyUIRemoteDockPrivate\.", "dock/"),
    (r"^ComfyUIRemoteDock\.", "dock/"),
    (r"^ComfyHistory", "history/"),
    (r"^ComfyControlLayer", "core/control/"),
    (r"^ComfyRegion", "core/regions/"),
    (r"^ComfyStyle", "core/styles/"),
    (r"^ComfyResources\.", "core/"),
    (r"^ComfyFileLibrary\.", "core/"),
    (r"^ComfyLocalization\.", "core/"),
    (r"^ComfyOpenPose\.", "core/"),
]


def dest_for(name: str) -> str:
    for pattern, dest in RULES:
        if re.match(pattern, name):
            return dest
    raise SystemExit(f"no rule for: {name}")


def main() -> int:
    dry = "--dry-run" in sys.argv
    moves: list[tuple[Path, Path]] = []
    for src in sorted(list(PLUGIN.glob("Comfy*.cpp")) + list(PLUGIN.glob("Comfy*.h"))):
        if not src.is_file():
            continue
        rel = dest_for(src.name)
        dst_dir = PLUGIN / rel
        dst = dst_dir / src.name
        if src.resolve() == dst.resolve():
            continue
        if dst.exists():
            raise SystemExit(f"destination exists: {dst}")
        moves.append((src, dst))

    print(f"{'dry-run: ' if dry else ''}{len(moves)} files to move")
    for src, dst in moves:
        print(f"  {src.name} -> {dst.relative_to(PLUGIN)}")
        if not dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            git_path = src.relative_to(REPO).as_posix()
            tracked = (
                subprocess.run(
                    ["git", "ls-files", "--error-unmatch", git_path],
                    cwd=REPO,
                    capture_output=True,
                ).returncode
                == 0
            )
            if tracked:
                subprocess.run(["git", "mv", str(src), str(dst)], check=True, cwd=PLUGIN)
            else:
                src.rename(dst)

    if dry:
        return 0

    # Write include dirs list for CMake
    roots = ["plugin", "network", "core", "ui", "settings", "workflow", "utils", "runners", "dock", "history"]
    dirs: set[Path] = {PLUGIN}
    for root in roots:
        base = PLUGIN / root
        if not base.exists():
            continue
        for p in [base, *base.rglob("*")]:
            if p.is_dir():
                dirs.add(p)
    cmake_snip = PLUGIN / "cmake" / "ComfyIncludeDirs.cmake"
    cmake_snip.parent.mkdir(exist_ok=True)
    lines = ["# Generated by scripts/reorganize_sources.py — all header search paths\n"]
    lines.append("set(COMFYUI_REMOTE_INCLUDE_DIRS\n")
    for d in sorted(dirs, key=lambda p: str(p)):
        lines.append(f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{d.relative_to(PLUGIN).as_posix()}"\n')
    lines.append(")\n")
    cmake_snip.write_text("".join(lines), encoding="utf-8")
    print(f"wrote {cmake_snip.relative_to(PLUGIN)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
