# Guide d'installation : Bolus Tracking Studio

**[English](INSTALL.md) | [Français (Québec)](INSTALL_FR.md)**

---

Ce guide décrit comment installer et configurer **Bolus Tracking Studio** sur macOS, Linux et Windows.

---

## Installation de l'interface Electron (Recommandé — Toutes les plateformes)

L'interface graphique interactive principale fonctionne sur Electron (Chromium). C'est la méthode recommandée pour toutes les plateformes.

### Prérequis :
- **Node.js** ≥ 18 et **npm** ≥ 9 ([télécharger](https://nodejs.org/))
- Le binaire C++ `bolus_server` (voir les instructions de compilation ci-dessous)

### Compiler le dorsal C++ :
```bash
mkdir -p build && cd build
cmake .. && make bolus_server -j4
```

### Installer et lancer :
```bash
cd gui
npm install
npm start
```

Voir **[gui/README_FR.md](../gui/README_FR.md)** pour la documentation complète.

---

## Hérité : Installation de l'interface native

> ⚠️ **L'interface graphique native Dear ImGui est obsolète.** Les sections ci-dessous sont conservées à titre de référence. Utilisez l'interface Electron ci-dessus.

## Installation sur macOS (Recommandé)

Sur macOS, vous pouvez compiler un paquet d'application natif exécutable (`.app`) avec une icône personnalisée.

### Script d'installation automatique :
Ouvrez votre terminal, accédez au répertoire du projet et exécutez le script d'installation pour macOS :
```bash
bash install_macos.sh
```

Ce script va :
1. Compiler l'application graphique haute performance en C++.
2. Construire la structure du paquet d'application macOS `BolusTrackingStudio.app`.
3. Créer une icône d'application à partir du fichier `resources/app_icon.png`.
4. Copier le paquet d'application dans votre dossier d'applications (`/Applications` ou le dossier utilisateur local `~/Applications` si vous n'avez pas les droits d'écriture) pour une indexation automatique dans le **Launchpad**.

### Lancement de l'application :
* **Option A (Launchpad)** : Appuyez sur `F4` ou cliquez sur l'icône du Launchpad dans votre Dock, recherchez **Bolus Tracking Studio** et cliquez sur l'icône de l'application.
* **Option B (Finder)** : Ouvrez votre dossier Applications et double-cliquez sur **Bolus Tracking Studio** (représenté par l'icône personnalisée montrant un capillaire et une courbe mathématique).
* **Option C (Terminal)** : Exécutez l'application depuis le terminal :
  ```bash
  open /Applications/BolusTrackingStudio.app
  # ou le chemin de repli utilisateur local :
  open ~/Applications/BolusTrackingStudio.app
  ```

---

## Installation sur Linux

Sur Linux, vous pouvez compiler l'application et ajouter un raccourci de lancement sur votre bureau.

### Prérequis :
Assurez-vous que CMake, un compilateur C++17 et les bibliothèques de développement pour GLFW, OpenGL et LibTIFF sont installés sur votre système. *(Requis uniquement pour l'interface native héritée, pas pour l'interface Electron.)*
Sur Ubuntu/Debian, installez-les via :
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libtiff5-dev -y
```

### Compiler l'application :
```bash
mkdir -p build && cd build
cmake -DBUILD_GUI=ON ..
make -j$(nproc)
```

### Créer un raccourci de lancement sur le bureau :
Créez un fichier à `~/.local/share/applications/bolus_tracking.desktop` avec le contenu suivant :
```ini
[Desktop Entry]
Type=Application
Name=Bolus Tracking Studio
Comment=Capillary Bolus Tracking & Gamma Curve Fitting Studio
Exec=/chemin/vers/Bolus_Tracking/build/bolus_tracking_gui
Icon=/chemin/vers/Bolus_Tracking/resources/app_icon.png
Terminal=false
Categories=Science;ScientificVisualization;
```
*(Remplacez `/chemin/vers/Bolus_Tracking` par le chemin absolu vers votre répertoire).*

---

## Installation sur Windows

Sur Windows, vous pouvez compiler l'application graphique à l'aide de Visual Studio et créer un raccourci exécutable sur le bureau.

### Prérequis :
1. Installez **Visual Studio Community** (2019 ou plus récent) avec la charge de travail **Développement Desktop en C++** sélectionnée.
2. Téléchargez et installez **CMake pour Windows** (assurez-vous d'ajouter CMake au PATH du système lors de la configuration).

### Compiler l'application :
1. Ouvrez l'invite de commande des outils de développement pour Visual Studio.
2. Accédez à votre dossier de projet :
   ```cmd
   mkdir build
   cd build
   cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_GUI=ON ..
   cmake --build . --config Release
   ```
3. L'exécutable sera créé sous `build/Release/bolus_tracking_gui.exe`.

### Créer un raccourci sur le bureau :
1. Faites un clic droit sur le bureau et choisissez **Nouveau > Raccourci**.
2. Parcourez les fichiers pour cibler le chemin de `build/Release/bolus_tracking_gui.exe`.
3. Nommez le raccourci **Bolus Tracking Studio**.
4. Pour définir l'icône personnalisée :
   * Faites un clic droit sur le raccourci et sélectionnez **Propriétés**.
   * Cliquez sur **Changer d'icône...** et ciblez `resources/app_icon.png` (ou convertissez `resources/app_icon.png` en `app_icon.ico` à l'aide d'un convertisseur en ligne et sélectionnez-le).
