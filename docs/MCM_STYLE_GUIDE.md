# Mid-Century Modern (MCM) Style Guide

**[English](MCM_STYLE_GUIDE.md) | [Français (Québec)](MCM_STYLE_GUIDE_FR.md)**

---

This style guide outlines the technical specifications and design principles of the **Mid-Century Modern (MCM)** visual design system used in **Bolus Tracking Studio**. This design blends organic retro warmth with clean, high-performance structural layouts to create a premium, cohesive user interface.

Use this document to maintain visual parity when modifying the C++ GUI, updating Python plotting tools, or expanding the application suite.

---

## 1. Typography System

The primary typeface is **Outfit**, a geometric, clean, and highly legible sans-serif font. The C++ application imports it in two weights (`Outfit-Medium` and `Outfit-Bold`) and configures it with high-DPI rendering parameters to avoid pixelation.

### Core Font Roles
*   **Body & Standard Controls**: `Outfit-Medium` at `16.0f` size.
*   **Headers & Section Titles**: `Outfit-Bold` at `18.0f` size.
*   **Splash Screen Title**: `Outfit-Bold` at `36.0f` size.
*   **Splash Screen Badges**: `Outfit-Bold` at `28.0f` base size (dynamically scaled).

### High-DPI Configuration (Dear ImGui)
To ensure crisp text rendering across high-density displays (such as Apple Retina displays), the font config utilizes oversampling and pixel snapping:
```cpp
ImFontConfig font_config;
font_config.OversampleH = 3;
font_config.OversampleV = 3;
font_config.PixelSnapH = true;
```

### Multilingual Font Merging
To support localized translations, additional font sets are merged into the primary Outfit font stack at the same sizes:
*   **Chinese & Japanese**: Common Simplified Chinese and Japanese glyph ranges merged dynamically.
*   **Korean**: Standard Korean glyph ranges merged dynamically.
*   **Cyrillic (Russian, Ukrainian)**: Unicode blocks `0x0400` to `0x052F` merged.
*   **Klingon (pIqaD)**: Standard Klingon private use area (`0xF8D0` to `0xF8FF`) merged.

---

## 2. Color Palettes

The MCM color system is divided into three distinct contexts: the standard C++ Dear ImGui application theme, the interactive retro splash screen, and the ImPlot scientific visualization style.

### A. C++ GUI Theme (Dear ImGui)

These normalized float colors are applied to ImGui style variables to define the main application windows, panels, inputs, and button states.

| Color Name | Hex Code | Normalized Float (RGBA) | Core Functional UI Role |
| :--- | :--- | :--- | :--- |
| **Warm Charcoal** | `#2E2E2B` | `(0.18f, 0.18f, 0.17f, 1.00f)` | Main window background (`WindowBg`, `ScrollbarBg`) |
| **Panel Charcoal** | `#383833` | `(0.22f, 0.22f, 0.20f, 0.95f)` | Container elements, panels, menu bars (`ChildBg`, `MenuBarBg`) |
| **Popup Base** | `#333330` | `(0.20f, 0.20f, 0.19f, 0.98f)` | Context menus, modals, and popups (`PopupBg`) |
| **Warm Cream** | `#F2F0E6` | `(0.95f, 0.94f, 0.90f, 1.00f)` | Primary text (`Text`) |
| **Muted Sand** | `#99948C` | `(0.60f, 0.58f, 0.55f, 1.00f)` | Disabled text, passive placeholders (`TextDisabled`) |
| **Field Charcoal** | `#42403B` | `(0.26f, 0.25f, 0.23f, 1.00f)` | Text input fields, checkbox frames (`FrameBg`) |
| **Field Hovered** | `#524D47` | `(0.32f, 0.30f, 0.28f, 1.00f)` | Hovered input fields (`FrameBgHovered`) |
| **Muted Bronze** | `#595247` | `(0.35f, 0.32f, 0.28f, 0.50f)` | Layout borders, separators (`Border`, `Separator`) |
| **Sage / Avocado** | `#616B59` | `(0.38f, 0.42f, 0.35f, 1.00f)` | Default buttons, active tabs (`Button`, `TabActive`) |
| **Sage Hovered** | `#75856B` | `(0.46f, 0.52f, 0.42f, 1.00f)` | Hovered buttons and headers (`ButtonHovered`) |
| **Burnt Terracotta** | `#E08C40` | `(0.88f, 0.55f, 0.25f, 1.00f)` | Active states, checkbox marks, slider grabs (`ButtonActive`, `CheckMark`) |
| **Warm Sand** | `#D9CCB3` | `(0.85f, 0.80f, 0.70f, 1.00f)` | Slider grabs, plot lines default (`SliderGrab`, `PlotLines`) |

### B. Retro Splash Screen Palette

Used in the custom canvas-drawn intro screen, these colors provide a vibrant, nostalgic analog look.

*   **Deep Space Charcoal**: `#131316` / `RGBA(19, 19, 22, 255)` (Canvas backdrop)
*   **Retro Grid Charcoal**: `RGBA(40, 40, 48, 80)` (Perspective grid lines)
*   **Retro Accent Cream**: `#F4EAD4` / `RGBA(244, 234, 212, 255)` (Main text, highlights, vessel borders)
*   **Retro Mustard**: `#E6AD45` / `RGBA(230, 173, 69, 255)` (Yellow accents, outer vessel borders)
*   **Retro Terracotta**: `#D95D39` / `RGBA(217, 93, 57, 255)` (Orange highlight ripples, active cell core)
*   **Dark Oxide Red**: `#8A2522` / `RGBA(138, 37, 34, 255)` (Deep cell details, shadows)
*   **Retro Teal**: `#3A6073` / `RGBA(58, 96, 115, 255)` (Text drop shadow, badge backgrounds)
*   **Retro Teal Light**: `#52849B` / `RGBA(82, 132, 155, 255)` (Spline tracking tick indicators)
*   **Vessel Ribbon Backdrop**: `RGBA(35, 55, 65, 90)` (Semi-transparent inner vessel path)

### C. ImPlot Scientific Data Colors

These specific colors define the data traces, curves, and interactive threshold markers in the analysis plotting panel.

*   **Raw Data Trace**: `#D9C79E` with 70% opacity / `ImVec4(0.85f, 0.78f, 0.62f, 0.70f)` (Warm Gold/Brass line, 1.5px thickness)
*   **Denoised Data Trace**: `#5EA3A3` / `ImVec4(0.37f, 0.64f, 0.64f, 1.0f)` (Muted Sage/Teal line, 1.5px thickness)
*   **Heuristic Fit Curve**: `#4582B5` with 50% opacity / `ImVec4(0.27f, 0.51f, 0.71f, 0.5f)` (Steel Blue line, 1.5px thickness)
*   **Final Mathematical Fit**: `#E0732E` / `ImVec4(0.88f, 0.45f, 0.18f, 1.0f)` (Terracotta Orange line, 2.5px thickness)
*   **Draggable Threshold Markers & Tags**:
    *   **Onset**: `#8C9E73` / `(0.55f, 0.62f, 0.45f, 1.0f)` (Green vertical line)
    *   **Peak**: `#EBB84C` / `(0.92f, 0.72f, 0.30f, 1.0f)` (Yellow vertical line)
    *   **End**: `#CC5238` / `(0.80f, 0.32f, 0.22f, 1.0f)` (Red vertical line)
    *   **Baseline**: `#AE7AAE` / `(0.68f, 0.48f, 0.68f, 1.0f)` (Purple horizontal line)

---

## 3. Layout, Rounding & Spacing

Mid-Century Modern design relies heavily on rounded organic corners balanced against strict border outlines.

```
┌───────────────────────────────────────────────┐
│ Window rounding: 14px                         │
│  ┌─────────────────────────────────────────┐  │
│  │ Child panel rounding: 12px              │  │
│  │  ┌────────────────┐ ┌────────────────┐  │  │
│  │  │ Frame: 10px    │ │ Button: 10px   │  │  │
│  │  └────────────────┘ └────────────────┘  │  │
│  └─────────────────────────────────────────┘  │
└───────────────────────────────────────────────┘
```

### Rounding Ratios (Dear ImGui)
*   **Main Application Windows**: `WindowRounding = 14.0f`
*   **Child Container Panels**: `ChildRounding = 12.0f`
*   **Inputs & Input Fields**: `FrameRounding = 10.0f`
*   **Modals & Popups**: `PopupRounding = 12.0f`
*   **Scrollbars & Sliders**: `ScrollbarRounding = 10.0f`
*   **Tabs & Handles**: `TabRounding = 8.0f`
*   **Slider Grabs**: `GrabRounding = 8.0f`

### Borders (Clean Outlines)
Every primary container type uses active outlines (`1px` thickness) using the Muted Bronze color (`#595247` at 50% opacity) to structure the interface:
```cpp
style.WindowBorderSize = 1.0f;
style.ChildBorderSize = 1.0f;
style.FrameBorderSize = 1.0f;
style.PopupBorderSize = 1.0f;
```

### Grid Spacing & Padding
*   **Window Padding**: `16px` horizontal $\times$ `16px` vertical (inner buffer of windows).
*   **Frame Padding**: `8px` horizontal $\times$ `6px` vertical (padding inside buttons/inputs).
*   **Item Spacing**: `12px` horizontal $\times$ `10px` vertical (gaps between adjacent controls).

---

## 4. Retro Interactive Graphic Effects

The custom C++ intro screen showcases advanced MCM graphic calculations. Developers extending the application should replicate these concepts:

### 1. Retro Double-Offset Shadow Typography
To produce an analog screen-print feel, titles are rendered three times with contrasting color offsets:
*   **Layer 1 (Bottom Shadow)**: Terracotta (`#D95D39`), offset by `+5px` (horizontal) and `+5px` (vertical).
*   **Layer 2 (Middle Shadow)**: Teal (`#3A6073` at 80% opacity), offset by `-4px` (horizontal) and `-4px` (vertical).
*   **Layer 3 (Foreground)**: Warm Retro Cream (`#F4EAD4`), positioned at the exact coordinates `(0, 0)`.

```
[Layer 1: Terracotta Shadow] ──> Offset (+5px, +5px)
  [Layer 2: Teal Shadow] ───────> Offset (-4px, -4px)
    [Layer 3: Cream Text] ──────> Base (0, 0)
```

### 2. Quadratic Perspective Grid
To draw a classic sci-fi synthwave grid, vertical positions of horizontal grid lines are calculated using a quadratic scale. This creates a realistic simulation of perspective depth stretching towards the horizon:
```cpp
float grid_y = height * 0.7f; // horizon line
int num_horiz = 8;
for (int i = 0; i < num_horiz; ++i) {
    float ratio = (float)i / (num_horiz - 1);
    float y = grid_y + (height - grid_y) * (ratio * ratio); // quadratic compression
    draw_list->AddLine(ImVec2(0, y), ImVec2(width, y), grid_color);
}
```

### 3. Harmonic Resonance Animation (Rumble & Pulse)
The active cell utilizes custom sine wave frequencies to coordinate a visual rumble (screen shake) with a breathing pulse scale:
```cpp
pulse_scale = 1.0f + 0.3f * intensity * sinf(elapsed * 25.0f);
rumble_x = 8.0f * intensity * sinf(elapsed * 45.0f);
rumble_y = 8.0f * intensity * cosf(elapsed * 37.0f);
```

---

## 5. Extension Guidelines

### A. Python (Tkinter & Matplotlib)
When developing Python-based scripts, update the standard tkinter variables to match the MCM palette:

```python
# Configure a dark MCM background in Tkinter
self.bg_color = "#2E2E2B"         # Warm Charcoal
self.panel_color = "#383833"      # Panel Charcoal
self.text_color = "#F2F0E6"       # Warm Cream
self.accent_color = "#E08C40"     # Burnt Terracotta
self.button_color = "#616B59"     # Sage Green

self.root.configure(bg=self.bg_color)

# Custom Style configuration for ttk widgets
self.style = ttk.Style()
self.style.configure("TFrame", background=self.bg_color)
self.style.configure("TLabel", background=self.bg_color, foreground=self.text_color)
```

For Python Matplotlib figures, inject the style parameters into `rcParams` or configure axes directly:

```python
import matplotlib.pyplot as plt

plt.rcParams['figure.facecolor'] = '#2E2E2B'
plt.rcParams['axes.facecolor'] = '#383833'
plt.rcParams['text.color'] = '#F2F0E6'
plt.rcParams['axes.labelcolor'] = '#F2F0E6'
plt.rcParams['xtick.color'] = '#99948C'
plt.rcParams['ytick.color'] = '#99948C'
plt.rcParams['grid.color'] = '#595247'
plt.rcParams['grid.alpha'] = 0.3

# Plot curves matching ImPlot Trace specs
ax.plot(t, y_raw, color='#D9C79E', alpha=0.7, label='Raw Data')      # Warm Gold
ax.plot(t, y_den, color='#5EA3A3', alpha=1.0, label='Denoised')      # Muted Teal
ax.plot(t, y_fit, color='#E0732E', linewidth=2.5, label='Fit')       # Terracotta
```

### B. Web Applications (HTML & CSS)
For modern web interfaces, implement the style system using CSS Custom Properties and utility classes:

```css
:root {
  --mcm-bg-window: #2e2e2b;
  --mcm-bg-panel: #383833;
  --mcm-bg-popup: #333330;
  --mcm-text-primary: #f2f0e6;
  --mcm-text-muted: #99948c;
  --mcm-border: rgba(89, 82, 71, 0.5);
  
  --mcm-color-sage: #616b59;
  --mcm-color-terracotta: #e08c40;
  --mcm-color-sand: #d9ccb3;
  
  --mcm-round-window: 14px;
  --mcm-round-child: 12px;
  --mcm-round-frame: 10px;
}

body {
  background-color: var(--mcm-bg-window);
  color: var(--mcm-text-primary);
  font-family: 'Outfit', sans-serif;
}

.mcm-panel {
  background-color: var(--mcm-bg-panel);
  border: 1px solid var(--mcm-border);
  border-radius: var(--mcm-round-child);
  padding: 16px;
}

/* Retro double-offset text shadow in CSS */
.mcm-retro-title {
  color: #f4ead4;
  font-weight: 800;
  text-shadow: 
    -4px -4px 0px rgba(58, 96, 115, 0.8), /* Layer 2: Retro Teal */
     5px  5px 0px rgba(217, 93, 57, 1.0);  /* Layer 1: Retro Terracotta */
}
```
