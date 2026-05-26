# Bolus Tracking Studio — Electron GUI

> **The primary interactive GUI for bolus tracking triage and quality control.**

![Bolus Tracking Studio Screenshot](../docs/app_screenshot.png)

Built on Electron (Chromium) with C++ SVG plot rendering, the studio provides a memory-efficient, cross-platform interface that maintains the exact Mid-Century Modern (MCM) visual design of the original native application.

---

## Prerequisites

| Dependency | Version | Purpose |
|---|---|---|
| **Node.js** | ≥ 18.x | Electron runtime |
| **npm** | ≥ 9.x | Package management |
| **bolus_server** | (built) | C++ backend for TIFF loading, trace computation, fitting, and SVG plot rendering |

The `bolus_server` binary must be built before running the GUI:
```bash
cd .. && mkdir -p build && cd build
cmake .. && make bolus_server -j$(nproc)
```

## Installation

```bash
cd gui
npm install
```

## Running

```bash
npm start
```

This launches the Electron window with a splash screen (THX crescendo + vessel animation), then presents the triage workspace.


## Batch Processing Pipeline

**NEW:** The studio now includes a full batch processing panel built-in, replacing the need to use `run_pipeline_cpp.sh` in the terminal.
Features:
- Select a subject folder via the native file dialog
- Run a **Pre-flight Scan** to validate MATLAB/TIFF files without modifying them
- Run **File Preparation** (Dry Run or Apply) to convert masks
- Run the **Full Pipeline** with live terminal output streamed to the GUI
- Color-coded output (errors red, warnings amber, success green)

---

## Architecture

```
gui/
├── main.js          # Electron main process: spawns bolus_server, IPC bridge, window management
├── preload.js       # contextBridge: secure bolusAPI exposed to renderer
├── renderer.js      # UI logic: splash, sounds, IPC pipeline, ROI management, localization
├── index.html       # Full layout: pre-flight, sidebar, SVG plot, params, modals
├── style.css        # MCM dark theme (pixel-matched to ImGui palette)
├── package.json     # Electron dependency, memory flags, no Plotly/xterm
├── extract_locales.py  # Extracts locale JSONs from bolus_locale.cpp
├── locales/         # 44 JSON locale files (104–114 keys each)
│   ├── en.json
│   ├── fr.json
│   ├── pirate.json
│   ├── yoda.json
│   └── ...
└── README.md        # This file
```

### IPC Protocol

The renderer communicates with `bolus_server` via line-delimited JSON over stdin/stdout:

```json
→ {"id":1, "action":"load_tiff", "params":{"path":"/data/subject-3554"}}
← {"id":1, "ok":true, "data":{"width":512, "height":512, "n_frames":600, "mip_base64":"..."}}
```

Key commands: `ping`, `load_tiff`, `load_rois`, `load_csv`, `compute_traces`, `get_trace`, `render_plot`, `auto_estimate`, `run_fit`, `save_csv`, `parse_framerate`, `convert_mat`.

### Plotting

All plots are rendered in **C++** (not JavaScript). The `render_plot` command returns an SVG string (~87KB) with:
- Warm charcoal background matching the MCM theme
- Raw data points (teal), denoised curve (golden), gamma fit (sage green)
- Axes, grid, legend, and ROI title in burnt orange

No Plotly.js, Chart.js, or any JavaScript chart library is used.

---

## MCM Theme

The CSS palette is a pixel-exact conversion of the ImGui `ImVec4` values from `bolus_gui.cpp`:

| Element | CSS Variable | Hex | ImGui Source |
|---|---|---|---|
| Window Background | `--bg-primary` | `#2e2e2b` | `WindowBg: 0.18, 0.18, 0.17` |
| Panel Background | `--bg-secondary` | `#383833` | `ChildBg: 0.22, 0.22, 0.20` |
| Input Fields | `--bg-elevated` | `#42403b` | `FrameBg: 0.26, 0.25, 0.23` |
| Primary Text | `--text-primary` | `#f2f0e6` | `Text: 0.95, 0.94, 0.90` |
| Muted Text | `--text-muted` | `#99948c` | `TextDisabled: 0.60, 0.58, 0.55` |
| Buttons (sage) | `--btn-sage` | `#616b59` | `Button: 0.38, 0.42, 0.35` |
| Accent (burnt orange) | `--accent-burnt-orange` | `#E08C40` | `0.88, 0.55, 0.25` |
| PASS badge | `--color-pass` | `#8c9e73` | `0.55, 0.62, 0.45` |
| WARN badge | `--color-warn` | `#ebb84d` | `0.92, 0.72, 0.30` |
| FAIL badge | `--color-fail` | `#cc5238` | `0.80, 0.32, 0.22` |
| REVIEW badge | `--color-review` | `#5e8a8a` | `0.37, 0.54, 0.54` |

*Note: Selecting the **Minion (Bello!)** language will trigger a full palette swap to the Denim Blue and Minion Yellow theme (`#0D1B3A`, `#FBD91C`), matching the original C++ GUI easter egg.*

Font: **Outfit** (Google Fonts) — the same MCM geometric sans-serif used in the native app.

---

## Localization

44 languages supported (all from `bolus_locale.cpp`, excluding Ancient Egyptian):

**Real languages**: Afrikaans, Bengali, Bulgarian, Catalan, Chinese (Simplified), Danish, Dutch, English, Esperanto, Finnish, French, Galician, Greek (Ancient), Haitian Creole, Hindi, Indonesian, Inuktitut, Irish, Italian, Japanese, Korean, Latin, Norwegian, Russian, Scots, Serbian, Spanish, Swedish, Tagalog, Tamil, Thai, Turkish, Ukrainian, Vietnamese.

**Novelty languages**: Gen Alpha, Gen Z, Klingon, Leet Speak, Minion, Pirate, Shakespearean, Yoda.

To regenerate locale files from C++ source:
```bash
python3 extract_locales.py
```

---

## Memory Constraints

- **JavaScript heap**: Capped at 256 MB via `--max-old-space-size=256` in package.json
- **TIFF frames**: Freed after trace computation (reclaims 186–314 MB)
- **MIP data**: Transmitted as base64 instead of JSON array
- **Traces**: Retrieved one ROI at a time via `get_trace`, not bulk-serialized
- **Target**: Total memory (Electron + bolus_server) should stay under 400 MB

---

## Sound Assets

The following sound files should be placed in the project's `resources/` directory:

| File | Trigger |
|---|---|
| `thx_crescendo.wav` | Splash screen animation |
| `minion_squeak.wav` | Button clicks and UI interactions |
| `hallelujah.mp3` | Successful CSV save |

Sounds are bundled via `extraResources` in package.json for portability.

---

## Deprecation Notice

This Electron GUI replaces both:
1. **`python/src/bolus_gui.py`** (Python/tkinter/matplotlib) — lightweight but slow, no multi-language support
2. **`cpp/src/bolus_gui.cpp`** (C++/Dear ImGui/GLFW) — fast but requires native graphics libraries

The old files are retained for reference but should not be used for new work.
