# Workflow golden fixtures (P1.8)

Committed `*.api.json` files are the canonical ComfyUI API workflow graphs for
`ComfyWorkflowEngine` builders (SD1.5 template family).

## Regenerate C++ fixtures

```bash
python3 plugins/dockers/comfyui_remote/scripts/export_workflow_fixture.py
```

Fixtures include `SamplerCustomAdvanced` graphs (GAP-A); output is pre-normalized to match `ComfyWorkflowNormalize`.

Or from a built test binary:

```bash
COMFY_WRITE_GOLDEN=1 ./ComfyWorkflowEngineGoldenTest
```

## Compare in tests

`ComfyWorkflowEngineGoldenTest` builds each workflow with fixed parameters,
normalizes node IDs (sort by `class_type` + inputs, renumber 1..N), and compares
to the matching `*.api.json`.
