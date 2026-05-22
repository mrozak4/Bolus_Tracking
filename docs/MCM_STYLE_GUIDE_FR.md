# Guide de style Mid-Century Modern (MCM)

**[English](MCM_STYLE_GUIDE.md) | [Français (Québec)](MCM_STYLE_GUIDE_FR.md)**

---

Ce guide de style décrit les spécifications techniques et les principes de conception du système visuel **Mid-Century Modern (MCM)** utilisé dans **Bolus Tracking Studio**. Cette esthétique combine la chaleur rétro et organique avec des mises en page structurelles épurées et performantes afin de créer une interface utilisateur premium et cohérente.

Utilisez ce document pour maintenir la parité visuelle lors de la modification de l'interface graphique en C++, de la mise à jour des outils de traçage en Python ou de l'extension de la suite d'applications.

---

## 1. Système typographique

La police de caractères principale est **Outfit**, une police sans empattement (sans-serif) géométrique, épurée et très lisible. L'application C++ l'importe en deux graisses (`Outfit-Medium` et `Outfit-Bold`) et la configure avec des paramètres de rendu haute densité (High-DPI) pour éviter toute pixellisation.

### Rôles des polices principales
*   **Corps du texte et contrôles standards** : `Outfit-Medium` à une taille de `16.0f`.
*   **En-têtes et titres de section** : `Outfit-Bold` à une taille de `18.0f`.
*   **Titre de l'écran d'accueil** : `Outfit-Bold` à une taille de `36.0f` avec un effet de double ombre.
*   **Badges de l'écran d'accueil** : `Outfit-Bold` à une taille de base de `28.0f` (mise à l'échelle dynamique).

### Configuration haute densité (Dear ImGui)
Pour garantir un rendu de texte net sur les écrans haute densité (tels que les écrans Retina d'Apple), la configuration de la police utilise le suréchantillonnage et l'alignement des pixels (pixel snapping) :
```cpp
ImFontConfig font_config;
font_config.OversampleH = 3;
font_config.OversampleV = 3;
font_config.PixelSnapH = true;
```

### Fusion de polices multilingues
Pour prendre en charge les traductions localisées, d'autres polices sont fusionnées dynamiquement dans la pile de polices Outfit aux mêmes tailles :
*   **Chinois et japonais** : Fusion dynamique des glyphes communs du chinois simplifié et du japonais.
*   **Coréen** : Fusion dynamique des plages de glyphes coréens standards.
*   **Cyrillique (russe, ukrainien)** : Fusion des blocs Unicode `0x0400` à `0x052F`.
*   **Klingon (pIqaD)** : Fusion de la zone d'utilisation privée standard du Klingon (`0xF8D0` à `0xF8FF`).

---

## 2. Palettes de couleurs

Le système de couleurs MCM est divisé en trois contextes distincts : le thème standard de l'application C++ Dear ImGui, l'écran d'accueil rétro interactif et le style de visualisation scientifique ImPlot.

### A. Thème de l'interface graphique C++ (Dear ImGui)

Ces couleurs flottantes normalisées sont appliquées aux variables de style d'ImGui pour définir les fenêtres principales, les panneaux, les entrées et les états des boutons.

| Nom de la couleur | Code Hex | Flottant normalisé (RGBA) | Rôle fonctionnel principal dans l'interface |
| :--- | :--- | :--- | :--- |
| **Fusain chaud** | `#2E2E2B` | `(0.18f, 0.18f, 0.17f, 1.00f)` | Arrière-plan de la fenêtre principale (`WindowBg`, `ScrollbarBg`) |
| **Fusain de panneau** | `#383833` | `(0.22f, 0.22f, 0.20f, 0.95f)` | Éléments conteneurs, panneaux, barres de menu (`ChildBg`, `MenuBarBg`) |
| **Base de fenêtre surgissante** | `#333330` | `(0.20f, 0.20f, 0.19f, 0.98f)` | Menus contextuels, fenêtres modales et info-bulles (`PopupBg`) |
| **Crème chaleureuse** | `#F2F0E6` | `(0.95f, 0.94f, 0.90f, 1.00f)` | Texte principal (`Text`) |
| **Sable atténué** | `#99948C` | `(0.60f, 0.58f, 0.55f, 1.00f)` | Texte désactivé, indicateurs passifs (`TextDisabled`) |
| **Fusain de champ** | `#42403B` | `(0.26f, 0.25f, 0.23f, 1.00f)` | Champs de saisie de texte, cases à cocher (`FrameBg`) |
| **Fusain survolé** | `#524D47` | `(0.32f, 0.30f, 0.28f, 1.00f)` | Champs de saisie survolés (`FrameBgHovered`) |
| **Bronze atténué** | `#595247` | `(0.35f, 0.32f, 0.28f, 0.50f)` | Bordures de mise en page, séparateurs (`Border`, `Separator`) |
| **Sauge / Avocat** | `#616B59` | `(0.38f, 0.42f, 0.35f, 1.00f)` | Boutons par défaut, onglets actifs (`Button`, `TabActive`) |
| **Sauge survolée** | `#75856B` | `(0.46f, 0.52f, 0.42f, 1.00f)` | Boutons et en-têtes survolés (`ButtonHovered`) |
| **Terre cuite brûlée** | `#E08C40` | `(0.88f, 0.55f, 0.25f, 1.00f)` | États actifs, indicateurs de cases à cocher, curseurs (`ButtonActive`, `CheckMark`) |
| **Sable chaleureux** | `#D9CCB3` | `(0.85f, 0.80f, 0.70f, 1.00f)` | Curseurs de glissement, tracés par défaut (`SliderGrab`, `PlotLines`) |

### B. Palette de l'écran d'accueil rétro

Utilisée dans l'écran d'accueil personnalisé dessiné sur canevas, ces couleurs fournissent un rendu analogique dynamique et nostalgique.

*   **Fusain d'espace profond** : `#131316` / `RGBA(19, 19, 22, 255)` (Fond du canevas)
*   **Fusain de grille rétro** : `RGBA(40, 40, 48, 80)` (Lignes de grille en perspective)
*   **Crème rétro accentuée** : `#F4EAD4` / `RGBA(244, 234, 212, 255)` (Texte principal, reflets, bordures de vaisseaux)
*   **Moutarde rétro** : `#E6AD45` / `RGBA(230, 173, 69, 255)` (Accents jaunes, bordures extérieures de vaisseaux)
*   **Terre cuite rétro** : `#D95D39` / `RGBA(217, 93, 57, 255)` (Ondes de surbrillance orange, noyau de cellule active)
*   **Rouge oxyde foncé** : `#8A2522` / `RGBA(138, 37, 34, 255)` (Détails des cellules profondes, ombres)
*   **Sarcelle rétro** : `#3A6073` / `RGBA(58, 96, 115, 255)` (Ombre portée du texte, arrière-plan des badges)
*   **Sarcelle rétro claire** : `#52849B` / `RGBA(82, 132, 155, 255)` (Indicateurs de graduation des splines)
*   **Fond de ruban de vaisseau** : `RGBA(35, 55, 65, 90)` (Tracé interne du vaisseau semi-transparent)

### C. Couleurs de données scientifiques ImPlot

Ces couleurs spécifiques définissent les tracés de données, les courbes d'ajustement et les marqueurs interactifs dans le panneau graphique d'analyse.

*   **Tracé des données brutes** : `#D9C79E` à 70% d'opacité / `ImVec4(0.85f, 0.78f, 0.62f, 0.70f)` (Ligne or/laiton chaud, épaisseur 1,5px)
*   **Tracé des données débruitées** : `#5EA3A3` / `ImVec4(0.37f, 0.64f, 0.64f, 1.0f)` (Ligne sauge/sarcelle atténuée, épaisseur 1,5px)
*   **Courbe d'ajustement heuristique** : `#4582B5` à 50% d'opacité / `ImVec4(0.27f, 0.51f, 0.71f, 0.5f)` (Ligne bleu acier, épaisseur 1,5px)
*   **Ajustement mathématique final** : `#E0732E` / `ImVec4(0.88f, 0.45f, 0.18f, 1.0f)` (Ligne orange terre cuite, épaisseur 2,5px)
*   **Marqueurs et étiquettes de seuil déplaçables** :
    *   **Début (Onset)** : `#8C9E73` / `(0.55f, 0.62f, 0.45f, 1.0f)` (Ligne verticale verte)
    *   **Pic (Peak)** : `#EBB84C` / `(0.92f, 0.72f, 0.30f, 1.0f)` (Ligne verticale jaune)
    *   **Fin (End)** : `#CC5238` / `(0.80f, 0.32f, 0.22f, 1.0f)` (Ligne verticale rouge)
    *   **Ligne de base (Baseline)** : `#AE7AAE` / `(0.68f, 0.48f, 0.68f, 1.0f)` (Ligne horizontale violette)

---

## 3. Mise en page, arrondi et espacement

Le style Mid-Century Modern repose en grande partie sur des angles arrondis organiques équilibrés par des bordures strictes et bien définies.

```
┌───────────────────────────────────────────────┐
│ Arrondi de fenêtre : 14px                     │
│  ┌─────────────────────────────────────────┐  │
│  │ Arrondi de panneau enfant : 12px        │  │
│  │  ┌────────────────┐ ┌────────────────┐  │  │
│  │  │ Cadre : 10px   │ │ Bouton : 10px  │  │  │
│  │  └────────────────┘ └────────────────┘  │  │
│  └─────────────────────────────────────────┘  │
└───────────────────────────────────────────────┘
```

### Ratios d'arrondi (Dear ImGui)
*   **Fenêtres principales de l'application** : `WindowRounding = 14.0f`
*   **Panneaux conteneurs enfants** : `ChildRounding = 12.0f`
*   **Champs de saisie et formulaires** : `FrameRounding = 10.0f`
*   **Fenêtres modales et surgissantes** : `PopupRounding = 12.0f`
*   **Barres de défilement et curseurs** : `ScrollbarRounding = 10.0f`
*   **Onglets et poignées** : `TabRounding = 8.0f`
*   **Curseurs de glissement** : `GrabRounding = 8.0f`

### Bordures (Contours nets)
Chaque type de conteneur principal utilise des contours actifs (`1px` d'épaisseur) avec la couleur bronze atténuée (`#595247` à 50% d'opacité) pour structurer l'interface :
```cpp
style.WindowBorderSize = 1.0f;
style.ChildBorderSize = 1.0f;
style.FrameBorderSize = 1.0f;
style.PopupBorderSize = 1.0f;
```

### Espacement et marges intérieures
*   **Marges de fenêtre (Window Padding)** : `16px` horizontal $\times$ `16px` vertical.
*   **Marges de cadre (Frame Padding)** : `8px` horizontal $\times$ `6px` vertical (espacement interne des boutons/champs).
*   **Espacement d'éléments (Item Spacing)** : `12px` horizontal $\times$ `10px` vertical (espace entre des contrôles adjacents).

---

## 4. Effets graphiques rétro interactifs

L'écran d'accueil personnalisé de l'application C++ démontre des calculs graphiques MCM avancés. Les développeurs étendant l'application doivent reproduire ces concepts :

### 1. Typographie rétro à double ombre décalée
Pour produire un effet d'impression analogique rétro, les titres sont rendus trois fois avec des décalages de couleur contrastés :
*   **Couche 1 (Ombre inférieure)** : Terre cuite (`#D95D39`), décalée de `+5px` (horizontal) et `+5px` (vertical).
*   **Couche 2 (Ombre intermédiaire)** : Sarcelle (`#3A6073` à 80% d'opacité), décalée de `-4px` (horizontal) et `-4px` (vertical).
*   **Couche 3 (Premier plan)** : Crème rétro (`#F4EAD4`), positionnée aux coordonnées exactes `(0, 0)`.

```
[Couche 1 : Ombre Terre cuite] ──> Décalage (+5px, +5px)
  [Couche 2 : Ombre Sarcelle] ────> Décalage (-4px, -4px)
    [Couche 3 : Texte Crème] ─────> Base (0, 0)
```

### 2. Grille de perspective quadratique
Pour tracer une grille de science-fiction rétro classique, les positions verticales des lignes de grille horizontales sont calculées à l'aide d'une échelle quadratique. Cela crée une simulation réaliste de perspective s'étirant vers l'horizon :
```cpp
float grid_y = height * 0.7f; // Ligne d'horizon
int num_horiz = 8;
for (int i = 0; i < num_horiz; ++i) {
    float ratio = (float)i / (num_horiz - 1);
    float y = grid_y + (height - grid_y) * (ratio * ratio); // Compression quadratique
    draw_list->AddLine(ImVec2(0, y), ImVec2(width, y), grid_color);
}
```

### 3. Animation de résonance harmonique (vibration et pulsation)
La cellule active utilise des fréquences d'ondes sinusoïdales personnalisées pour coordonner une vibration de l'écran (rumble) avec une pulsation d'échelle :
```cpp
pulse_scale = 1.0f + 0.3f * intensity * sinf(elapsed * 25.0f);
rumble_x = 8.0f * intensity * sinf(elapsed * 45.0f);
rumble_y = 8.0f * intensity * cosf(elapsed * 37.0f);
```

---

## 5. Directives pour l'extension du style

### A. Python (Tkinter & Matplotlib)
Lors du développement de scripts basés sur Python, mettez à jour les variables standards de tkinter pour correspondre à la palette MCM :

```python
# Configuration d'un arrière-plan MCM sombre dans Tkinter
self.bg_color = "#2E2E2B"         # Fusain chaud
self.panel_color = "#383833"      # Fusain de panneau
self.text_color = "#F2F0E6"       # Crème chaleureuse
self.accent_color = "#E08C40"     # Terre cuite brûlée
self.button_color = "#616B59"     # Vert sauge

self.root.configure(bg=self.bg_color)

# Configuration de style personnalisée pour les widgets ttk
self.style = ttk.Style()
self.style.configure("TFrame", background=self.bg_color)
self.style.configure("TLabel", background=self.bg_color, foreground=self.text_color)
```

Pour les figures Python Matplotlib, injectez les paramètres de style dans `rcParams` ou configurez les axes directement :

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

# Tracer les courbes correspondant aux spécifications d'ImPlot
ax.plot(t, y_raw, color='#D9C79E', alpha=0.7, label='Données brutes') # Or chaud
ax.plot(t, y_den, color='#5EA3A3', alpha=1.0, label='Débruitées')     # Sarcelle atténuée
ax.plot(t, y_fit, color='#E0732E', linewidth=2.5, label='Ajustement') # Terre cuite
```

### B. Applications Web (HTML & CSS)
Pour les interfaces web modernes, implémentez le système de style en utilisant des propriétés personnalisées CSS et des classes utilitaires :

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

/* Ombre de texte à double décalage rétro en CSS */
.mcm-retro-title {
  color: #f4ead4;
  font-weight: 800;
  text-shadow: 
    -4px -4px 0px rgba(58, 96, 115, 0.8), /* Couche 2 : Sarcelle rétro */
     5px  5px 0px rgba(217, 93, 57, 1.0);  /* Couche 1 : Terre cuite rétro */
}
```
