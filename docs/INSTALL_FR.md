# Guide d'installation : Bolus Tracking Studio

**[English](INSTALL.md) | [Français (Québec)](INSTALL_FR.md)**

---

Ce guide décrit comment installer et configurer **Bolus Tracking Studio** sur macOS, Linux et Windows.

---

## Installation sur macOS (Recommandé)

### Option A : Télécharger le DMG (plus simple)

1. Téléchargez le DMG depuis la [dernière version](https://github.com/mrozak4/Bolus_Tracking/releases/latest) :
   - **Apple Silicon** (M1/M2/M3/M4) : `BolusTrackingStudio-*-arm64.dmg`
   - **Intel** : `BolusTrackingStudio-*-x86_64.dmg`
2. Ouvrez le `.dmg` et glissez **Bolus Tracking Studio** vers **Applications**.
3. Au premier lancement, macOS Gatekeeper peut bloquer l'application. Corrigez avec :
   ```bash
   xattr -cr "/Applications/Bolus Tracking Studio.app"
   ```

### Option B : Compiler depuis les sources

#### Prérequis :
```bash
brew install eigen libtiff
```

#### Compilation :
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_gui -j8
```

#### Lancement :
```bash
open "Bolus Tracking Studio.app"
```

#### Créer un DMG :
```bash
./macos/create_dmg.sh              # Compile pour l'architecture courante
./macos/create_dmg.sh --arch arm64  # Apple Silicon
./macos/create_dmg.sh --arch x86_64 # Intel
```

---

## Installation sur Linux

### Prérequis :
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libeigen3-dev libtiff5-dev \
    libglfw3-dev libgl1-mesa-dev zlib1g-dev -y
```

### Compilation :
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_gui -j$(nproc)
```

### Créer un lanceur de bureau :
Créez un fichier à `~/.local/share/applications/bolus_tracking.desktop` :
```ini
[Desktop Entry]
Type=Application
Name=Bolus Tracking Studio
Comment=Studio d'analyse de bolus capillaire et d'ajustement de courbe gamma
Exec=/chemin/vers/Bolus_Tracking/build/bolus_tracking_gui
Icon=/chemin/vers/Bolus_Tracking/resources/app_icon.png
Terminal=false
Categories=Science;ScientificVisualization;
```
*(Remplacez `/chemin/vers/Bolus_Tracking` par le chemin absolu de votre dépôt.)*

---

## Installation sur Windows

### Prérequis :
1. Installez **Visual Studio Community** (2019 ou plus récent) avec la charge de travail **Développement Desktop C++**.
2. Installez **CMake pour Windows** (ajoutez-le au PATH durant l'installation).
3. Installez **vcpkg** et utilisez-le pour installer les dépendances :
   ```cmd
   vcpkg install eigen3 tiff glfw3 zlib
   ```

### Compilation :
```cmd
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target bolus_tracking_gui --config Release
```

---

## Pipeline CLI uniquement (sans interface graphique)

Pour compiler uniquement l'outil en ligne de commande :
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bolus_tracking_cpp -j8
```

Exécutez-le avec :
```bash
./bolus_tracking_cpp --folder /chemin/vers/donnees/sujet
```

---


> ⚠️ L'interface Python (`python/src/bolus_gui.py`) est **obsolète**. Utilisez l'application C++ native.
