---
name: build-krita
description: Iteratively aligns the Krita ComfyUI remote docker plugin with the Technical_Specification.md for the Krita AI Diffusion plugin, cataloging each spec section’s completion status, updating the implementation until all required sections are marked complete, and auditing both not_applicable and complete entries so JSON status stays truthful.
---

# Build Krita ComfyUI Docker Plugin to Match Spec

## When to Use This Skill

Use this skill whenever:
- The task involves updating the Krita ComfyUI remote docker plugin (the C++ dock and related code) to match `Technical_Specification.md`.
- You need to check whether the implementation matches the spec exactly and track progress in `Technical_Specification_Completed.json`.
- You are asked to “sync”, “align”, or “port” behavior from the Python `krita-ai-diffusion` plugin to this C++ dock implementation.

## Core Artifacts

- `Technical_Specification.md` at the repo root is the **source of truth** for behavior and UX.
- `Technical_Specification_Completed.json` at the repo root is the **status map and progress summary**:
  - Keys: top-level or numbered spec section identifiers as strings (e.g. `"1"`, `"2"`, `"3.4"` if needed).
  - Values: one of `"not_started"`, `"in_progress"`, `"complete"`, `"not_applicable"`.
  - The `possible_values` array documents the meaning of each status string.
  - The `completion_percentage` and `in_progress_percentage` keys store overall progress as user-facing strings (e.g. `"25%"`, `"3.1%"`), derived from the per-section values. Run `./update_spec_completion.sh` after editing the JSON; those percentages use **applicable sections only** (entries whose value is not `"not_applicable"`) as the denominator so **100%** means every portable subsection is `"complete"`.
- Primary implementation focus:
  - `plugins/dockers/comfyui_remote/ComfyUIRemoteDock.cpp` (and any directly-related headers/sources required to achieve parity).

## Status Value Semantics

- **"complete"**: Implementation behavior and UI **exactly** match the spec for that section (within the constraints of the C++ dock), including naming, defaults, and interaction flow.
- **"in_progress"**: Some but not all behaviors in that section are implemented or aligned; do **not** upgrade to `"complete"` until the entire section is satisfied.
- **"not_started"**: The section has not yet been evaluated or modified in the current port.
- **"not_applicable"**: The section is about Python-only structure, tooling, or artifacts that have no meaningful counterpart for this C++ dock (e.g. pytest layout, websockets submodule packaging) and cannot/should not be reimplemented here. **These should be verified** in the audit pass (step 10): if a section is actually applicable, set it to `"not_started"`; otherwise treat it as **passed** (leave `"not_applicable"`). Sections marked **`"complete"`** are also subject to the same audit pass: stale or mistaken `"complete"` entries must be corrected (see step 10).

## Workflow: Iterative Spec-to-Code Alignment

**Primary goal:** produce a **perfect C++ port** of the Python plugin’s behavior and UX for the ComfyUI docker, matching the spec exactly. Cataloguing status is helpful, but **never more important than implementing and verifying behavior.**

Follow this loop **aggressively, across many sections in a single `/build-krita` invocation**, until all relevant sections are `"complete"` **and every applicable spec section has an explicit status entry in `Technical_Specification_Completed.json`**. Do **not** stop after updating only one subsection if there are still `"not_started"` or `"in_progress"` items you can continue working through in the same turn.

1. **Identify Next Spec Section (and ensure it has a status entry)**
   - Read `Technical_Specification.md` and locate the next numbered section (e.g. `## 1.`, `## 2.`, `### 3.1`) that:
     - Has no entry in `Technical_Specification_Completed.json`, or
     - Is marked `"not_started"` or `"in_progress"`.
   - Prefer working top‑down by section number to keep progress traceable.
   - **If there are any sections with no JSON entry yet, prioritize filling those in first** with a provisional value (`"not_started"` or `"not_applicable"`) before or as soon as you begin analysis, so there are never “missing” sections once touched.
   - **If all applicable sections already have JSON entries**, stop spending cycles on cataloguing and instead pick the next `"not_started"` or `"in_progress"` section and focus on bringing its implementation into alignment with the spec.

2. **Understand Spec Requirements**
   - For the chosen section, extract:
     - **Behavioral requirements** (what the plugin should do).
     - **UI requirements** (labels, titles, tooltips, layouts, default values).
     - **State and persistence requirements** (settings, history, regions, queue state, etc.).
   - Summarize these in your own words before inspecting the C++ code.

3. **Locate Corresponding Implementation**
   - Start with `ComfyUIRemoteDock.cpp`.
   - Use search by:
     - String literals (UI text, labels, tooltips).
     - Concept names (e.g., workspace names, “Live”, “Upscale”, region prompts, queue modes).
     - Data structures and state (history, regions, presets, queue).
   - If needed, open related headers or helper files, but keep this skill focused on the docker plugin and directly-coupled code.

4. **Diff Spec vs Implementation**
   - Explicitly compare spec requirements to current behavior:
     - **Missing features**: Required capabilities not present at all.
     - **Mismatched behavior**: Present but semantics differ (e.g., wrong default, wrong label, different flow, incorrect polling/queue semantics).
     - **Extra behavior**: Safe to keep as long as it does not contradict or confuse the spec’s UX.
   - Treat the spec as authoritative; when in doubt, match the spec rather than preserving legacy behavior.

5. **Implement or Adjust Behavior**
   - Make minimal, targeted changes in C++ to close the gap:
     - Add missing controls, workflow handling, or state.
     - Rename labels/titles/tooltips to match spec text when specified exactly.
     - Adjust defaults (e.g., steps, CFG scale, server URL) to spec-defined defaults.
     - Fix flows such as:
       - Workspace switching (Generate/Upscale/Live/Animation/Regions).
       - Custom ComfyUI workflow loading and execution.
       - Job queueing and polling semantics (timeouts, progress, error messaging).
       - History list and re-run/apply behavior.
       - Region inpainting and mask handling.
   - Avoid introducing speculative behavior that is not justified by the spec or existing implementation.

6. **Validate Behavior**
   - Re-read the relevant spec subsection and confirm each bullet/requirement is now satisfied.
   - If possible, reason through typical user flows in that area (e.g., setting server URL, triggering Generate, running Regions) and check they align with the described UX.
   - If a requirement cannot be implemented due to API limitations, document that in comments **only when the constraint is non-obvious**, and keep the section as `"in_progress"` rather than `"complete"`.

7. **Update `Technical_Specification_Completed.json` (no analysis-only passes)**
   - For **every** section you analyze or modify (even if you only read/triage it with a subagent in this iteration):
     - Ensure there is a JSON key for that section ID (e.g. `"5.3"`); create it if missing.
     - Set or update its value based on the semantics above (`"not_started"`, `"in_progress"`, `"complete"`, `"not_applicable"`).
   - Do **not** leave a section “missing” in the JSON once you have inspected it; at minimum it must be `"not_started"` or `"not_applicable"`.
   - Do **not** mark `"complete"` unless you are confident the behavior matches exactly.
   - If a section mixes Python-only structure with behavior that does map to the C++ dock:
     - You may split tracking into finer-grained keys (e.g., `"3"`, `"3.1"`, `"3.2"`) so that Python-only parts can be `"not_applicable"` while dock-relevant parts become `"complete"`.

8. **Implement, Validate, Then Advance Status**
   - After updating C++ code to close spec gaps for a section, re-check the spec line‑by‑line for that section and:
     - Move its status from `"not_started"` → `"in_progress"` → `"complete"` as you actually implement and verify behavior, not just design it.
     - Keep sections `"in_progress"` when any requirement is still unimplemented or only partially aligned.

9. **Keep Iterating Until Full Coverage (within each `/build-krita` run)**
   - Within a single `/build-krita` invocation, **repeat from step 1 for as many sections as the environment allows**. The agent should:
     - Move through multiple `"not_started"` / `"in_progress"` sections in one go, rather than stopping after the first change.
     - Only pause when external constraints are reached (e.g., response/tool limits) or when there is no more meaningful work left (all relevant sections are `"complete"` / `"not_applicable"` **and** any **step 10** audit batch planned for this run is finished).
   - Overall, repeat from step 1 for the next section until:
     - All behaviorally-relevant sections are `"complete"`, and
     - Non-applicable Python-only infrastructure sections are explicitly `"not_applicable"`, and
     - **Every numbered section in `Technical_Specification.md` that is applicable to the dock has a corresponding entry in `Technical_Specification_Completed.json`**.

10. **Audit pass: verify `not_applicable` and `complete`**
   - Periodically, after large refactors, when the spec changes, or when no `"not_started"` / `"in_progress"` sections remain, run one or both audits below. Treat **JSON status as claims that must be justified** against `Technical_Specification.md` and the current C++ implementation.

   **10a. `not_applicable` sections**
   - **Go over sections marked `"not_applicable"`** (in batches by chapter if needed: 2.x, 3.x, 4.x, 13.x, etc.):
     - **Re-read the spec** for that section (title and body in `Technical_Specification.md`).
     - **Decide:** Does this section describe behavior, UX, settings, or data that the C++ dock could or should implement (even partially)?
       - **If yes (actually applicable):** Set the section’s status to **`"not_started"`** in `Technical_Specification_Completed.json`. It will be picked up in the main loop (steps 1–9).
       - **If no (truly not applicable):** **Pass** — leave **`"not_applicable"`**.
   - **Examples of “actually applicable”:** UI text, defaults, or flows the dock could implement but was previously assumed not applicable (e.g. a 13.x widget that maps to the dock).
   - **Examples of “truly not applicable” (pass):** Python package layout, pytest layout, Git submodules, `pyproject.toml`, websockets bundle, managed server installer, cloud-only APIs with no C++ equivalent, docs build (Astro), or script-only tooling outside the dock.

   **10b. `complete` sections (re-verify, do not trust the label blindly)**
   - **Go over sections marked `"complete"`** using the same rigor as steps 4–6: **re-read the full spec subsection**, then **spot-check or systematically compare** the implementation (UI strings, defaults, flows, persistence) in the docker plugin sources.
   - **If the section still fully matches the spec:** **Pass** — leave **`"complete"`** (optionally note nothing to fix).
   - **If anything is missing, wrong, or only partially aligned** (spec updated, regression, or the original mark was optimistic):
     - **Implement** the minimal fixes needed (step 5), then re-validate (step 6); only after that restore **`"complete"`**.
     - If you cannot finish fixes in this run, set the section to **`"in_progress"`** (or **`"not_started"`** if untouched) until parity is restored — **do not leave `"complete"` on a section that fails the checklist.**
   - **Coverage strategy:** In one invocation, audit as many `"complete"` sections as practical (e.g. rotate by chapter, or prioritize areas that recently changed in spec or code). Over successive `/build-krita` runs, aim to **re-touch every `"complete"` key** on a reasonable cadence so drift does not accumulate silently.

## Guardrails and Priorities

- **Perfect port first**: Always prioritize implementing and validating behavior so that each section can be marked `"complete"`; do not assume the user will manually fix or wire anything outside this skill.
- **Spec is authoritative**: Prefer changes that increase fidelity to `Technical_Specification.md` even if they differ from the current C++ dock behavior.
- **No premature completion**: Only mark `"complete"` when you have systematically checked the entire section against the implementation.
- **Complete is not permanent without proof**: `"complete"` in JSON is only valid while the implementation matches the spec; use **step 10b** to catch false completes and regressions.
- **Minimize collateral changes**: Avoid refactors that are not required to achieve spec parity; they belong outside this skill’s scope.
- **Focus on the docker plugin**: Do not expand the scope to unrelated Krita components unless the spec explicitly requires cross-plugin behavior.

## Examples

### Example: Align Titles and Defaults (Section 1)

1. Read Overview and Purpose in `Technical_Specification.md` and note:
   - Dock title must be "AI Image Generation".
   - Settings dialog title must be "Configure Image Diffusion".
   - Default ComfyUI server URL should be "127.0.0.1:8188".
2. Search `ComfyUIRemoteDock.cpp` for the current window and dialog titles and server URL configuration.
3. Update the C++ code so these strings and defaults match exactly.
4. Set `"1"` in `Technical_Specification_Completed.json` to `"in_progress"` or `"complete"` depending on whether all Overview behavior, not just titles/defaults, now matches.

### Example: Mark Python-only Structure as Not Applicable (Section 2)

1. Read Project Structure in `Technical_Specification.md` and identify Python-only pieces (package layout, tests, websockets submodule).
2. Confirm there is no plausible mapping for these parts to the C++ docker plugin.
3. In `Technical_Specification_Completed.json`, mark `"2"` as `"not_applicable"` while continuing work on later, behavior-focused sections.

### Example: Verify not_applicable — reclassify or pass (Step 10a)

1. List all keys in `Technical_Specification_Completed.json` whose value is `"not_applicable"` (e.g. 2.1, 3.2, 13.37).
2. For section **13.33 InitialSetupWidget (first-time server choice)**: read the spec. It describes the first-run UI where the user picks Online Service / Local Managed Server / Custom ComfyUI. The C++ dock can have equivalent UI (e.g. a setup page or connection tab with the same choices). → **Actually applicable.** Set `"13.33"` to `"not_started"`.
3. For section **2.10 Git submodules**: read the spec. It describes `.gitmodules` and the websockets/debugpy submodules. The C++ build does not use Python submodules. → **Truly not applicable.** Leave `"2.10"` as `"not_applicable"` (pass).

### Example: Re-verify complete — pass, fix, or downgrade (Step 10b)

1. Pick a section marked `"complete"` (e.g. `"5"` — generation workspace). Read that subsection in `Technical_Specification.md` line by line.
2. Compare to the dock: labels, defaults, queue behavior, error strings. Suppose the spec now requires a tooltip on a control that the dock lacks → **not actually complete.**
3. Add the tooltip (or other fix), re-validate, then keep `"5"` as `"complete"`. If you only document the gap without fixing, set `"5"` to `"in_progress"` until the next pass.

