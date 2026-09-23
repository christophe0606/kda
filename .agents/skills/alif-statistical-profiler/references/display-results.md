# Display function loads

Use the decoded report from the same capture. No board access, rebuild, or ELF
is needed for visualization. Verify timing/validation/rejection diagnostics first.
All examples run from the KDA root; replace `build/profile-2s/report` for other runs.

## Offline interactive HTML (upstream tool)

```powershell
uv run --with plotly==6.3.0 python .agents/skills/alif-statistical-profiler/scripts/visualize_profiler_report.py --report build/profile-2s/report --html
```

This creates `dashboard.html` and `samples.perfetto.json` in the report directory.
The copied `scripts/requirements-visualization.txt` pins upstream's optional
Plotly dependency; an existing Python environment with those requirements can
run the script directly instead of uv.

Open `dashboard.html` in a desktop browser. It embeds Plotly JavaScript and works
offline without a server or CDN. A wide window is best for the plots and long
symbols; a narrow app sidebar may crop the original dashboard's fixed margins.
The dashboard contains:

- A bar chart of the top 20 function symbols by exclusive sample share.
- A complete function-name table with hit counts and percentages.
- Individual PC samples over time, with symbol, PC, LR and index on hover.
- PMU event-rate plots only when valid PMU data was captured.
- Capture diagnostics and artifact hashes.

Hover over bars for exact shares/hits. Drag to zoom and double-click to reset;
legend clicks hide/isolate functions. The whole-capture load chart/table do not
change when the timeline is zoomed. The plot toolbar's **Download plot as a PNG**
button exports a picture directly from the browser. PMU-disabled notices are
expected for KDA's default capture and do not invalidate PC sampling.

## Simple PNG with symbols and percentages

```powershell
uv run --with matplotlib python .agents/skills/alif-statistical-profiler/scripts/plot_function_load.py --report build/profile-2s/report --title 'Function load on Alif E8 - M55_HP'
```

This KDA helper creates `function-load.png` with symbol labels, percentages and
hit counts. `--output PATH` chooses another PNG filename; `--top N` limits displayed
symbols and combines the remainder as Other. Set the title to identify the measured
core/build; do not label an HE report as HP. It reads the upstream report schema
through the adjacent visualization module and requires Matplotlib only.

Inspect the image for legible, unclipped symbol names before delivering it.
Embed the PNG using its absolute filesystem path and link the HTML separately.
The PNG is a static whole-capture overview; use HTML/Perfetto for time exploration.

## Perfetto without optional Python dependencies

```powershell
uv run python .agents/skills/alif-statistical-profiler/scripts/visualize_profiler_report.py --report build/profile-2s/report
```

Open the generated `samples.perfetto.json` in a local/approved Perfetto UI via
**Open trace file**. Events represent individual PC observations, not function
execution spans or reconstructed call stacks. No upload is needed. For simple
tabular output, `functions.csv` already contains symbols, hits and percentages.

The upstream exporter overwrites `dashboard.html` / `samples.perfetto.json` at its
destination. Use `--output NEW_DIRECTORY` to preserve an older export. The PNG
helper similarly overwrites its selected filename. Keep derived artifacts in
`build/` alongside the capture identity, not in the skill itself.

`visualize_profiler_report.py` and `requirements-visualization.txt` are unchanged
copies from upstream commit `ad5342b73780d60bcb379caac0c6a6125663ec67` (`host/`).
The included upstream license applies to them. `plot_function_load.py` is the
small project-specific static-chart helper. HTML, Perfetto and PNG were exercised
using the real 2000-sample E8 capture (74.8% heavy, 25.2% light).
