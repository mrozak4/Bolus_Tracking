# Guide complet d'exécution du pipeline de suivi de bolus

**[English](RUNNING_GUIDE.md) | [Français (Québec)](RUNNING_GUIDE_FR.md)**

Ce guide est conçu tant pour les **utilisateurs humains (même sans expérience en programmation)** que pour les **agents de codage IA** afin de configurer, d'exécuter et de maintenir facilement le pipeline de suivi de bolus et son interface graphique interactive.

---

## 1. Comment télécharger les fichiers depuis GitHub

Vous pouvez télécharger les fichiers du dépôt GitHub en utilisant l'une des deux méthodes suivantes :

### Option A : Téléchargement sous forme de fichier ZIP (Aucune expérience requise)
1. Ouvrez votre navigateur web et accédez à la page du dépôt sur GitHub.
2. Cliquez sur le bouton vert **`<> Code`** situé en haut à droite de la liste des fichiers.
3. Sélectionnez **Download ZIP** dans le menu déroulant.
4. Une fois le téléchargement terminé, localisez le fichier ZIP sur votre ordinateur et extrayez-le (décompressez-le).

### Option B : Cloner via Git (Pour les développeurs et agents IA)
Ouvrez votre terminal (macOS/Linux) ou PowerShell (Windows) et exécutez :
```bash
git clone https://github.com/mrozak4/Bolus_Tracking.git
cd Bolus_Tracking
```

---

## 2. Flux de travail recommandé : Exécuter le pipeline C++ avec Docker

> [!IMPORTANT]
> **L'utilisation de Docker est la méthode fortement recommandée pour exécuter le pipeline C++.**
> Elle garantit que toutes les versions de bibliothèques sont identiques, évite les conflits de dépendances et ne nécessite aucune installation de compilateur C++ ou de packages sur votre machine hôte.

### Prérequis (Configuration unique)
Assurez-vous que Docker est installé et en cours d'exécution sur votre système :
- **macOS / Windows** : Téléchargez et lancez [Docker Desktop](https://www.docker.com/products/docker-desktop/).
- **Linux (Ubuntu/Debian)** : Exécutez :
  ```bash
  sudo apt-get update && sudo apt-get install docker.io -y
  sudo systemctl start docker && sudo systemctl enable docker
  sudo usermod -aG docker $USER  # Déconnectez-vous et reconnectez-vous après cela
  ```

---

### Exécuter le pipeline parallèle C++ avec Docker
Le pipeline C++ est un moteur de calcul autonome conçu pour une vitesse maximale. Il s'exécute entièrement dans son propre conteneur Docker avec un temps système nul lié à Python.

Pour traiter un dossier cible (ex: `sample-subject-2259`) en utilisant le pipeline C++ avec Docker :
```bash
bash run_pipeline_cpp.sh sample-subject-2259
```

> [!TIP]
> **Génération de graphiques et correction de dérive** : Par défaut, le pipeline C++ génère uniquement le fichier CSV des résultats pour maximiser la vitesse. Pour générer des graphiques SVG de qualité publication pour toutes les ROI capillaires, ajoutez l'argument `--plot` :
> ```bash
> bash run_pipeline_cpp.sh sample-subject-2259 --plot
> ```
> Par défaut, la correction de dérive linéaire de la ligne de base utilise les **15,0** premières secondes. Si vous souhaitez modifier cette durée (par exemple à 10 secondes), ajoutez `--drift 10` :
> ```bash
> bash run_pipeline_cpp.sh sample-subject-2259 --plot --drift 10
> ```
> **Configuration des limites de modélisation et de contrôle de qualité** : Vous pouvez surcharger les limites physiologiques par défaut et les seuils de CQ directement depuis les arguments de ligne de commande (par exemple pour imposer un seuil de $T_{2p}$ minimal de `2.0` secondes ou des avertissements personnalisés de RCB) :
> ```bash
> bash run_pipeline_cpp.sh sample-subject-2259 --min-t2p 2.0 --qc-cnr-min 6.0
> ```
> Les graphiques SVG seront enregistrés dans un sous-répertoire `plots_cpp` à l'intérieur du dossier du sujet.

**Traitement par lots de plusieurs sujets** : Vous pouvez traiter plusieurs sujets à la fois en ciblant un répertoire parent contenant plusieurs dossiers de sujets. Le pipeline balaiera récursivement chaque dossier pour y trouver les fichiers `.tif`, `_rois.txt` et les fichiers de métadonnées associés :
```bash
# Traiter tous les sujets sous le répertoire actuel
bash run_pipeline_cpp.sh . --plot
```

#### Commande manuelle (sans le script d'enveloppe) :
Si vous souhaitez exécuter la commande Docker manuellement :
```bash
# 1. Compiler l'image C++
docker build -t bolus_tracking_cpp -f Dockerfile.cpp .

# 2. Exécuter le traitement C++ (monte le répertoire actuel sur /data dans le conteneur)
docker run --rm -v "$(pwd):/data" bolus_tracking_cpp --folder /data/sample-subject-2259
```

---

### Paramètres de qualité de modélisation et limites de triage :
| Paramètre | Description | Seuil d'avertissement (`AVERT.`) | Seuil d'échec (`ÉCHEC`) | Bornes absolues du résolveur (Limites strictes) |
| :--- | :--- | :--- | :--- | :--- |
| **Amplitude** | Hauteur maximale du pic de la courbe | Proche des bornes du résolveur | — | `[1e-6, 1023.0]` |
| **Temps au pic (TAP)** | Durée entre le début et le pic | `< 0.1 s` ou `> 10.0 s` ou proche borne | — | `[1e-6, durée de la fenêtre]` |
| **Largeur à mi-hauteur (LMH)** | Largeur de la courbe à mi-amplitude maximale | `< 0.5 s` ou `> 15.0 s` ou proche borne | — | `[0.5, durée de la fenêtre]` |
| **RCB (Rapport contraste-bruit)** | Rapport contraste-bruit (Pic / Écart-type ligne de base) | `[3.0, 5.0]` | `< 3.0` | — |

#### Personnalisation des bornes et des seuils de CQ depuis la ligne de commande
Vous pouvez configurer dynamiquement les bornes d'optimisation absolues et les seuils d'avertissement/échec lors de l'exécution du pipeline parallèle C++ en utilisant les indicateurs suivants :

| Option en ligne de commande | Paramètre ciblé | Valeur par défaut | Description |
| :--- | :--- | :--- | :--- |
| **`--min-amp <val>`** | Amplitude minimale absolue | `1e-6` | Borne inférieure stricte pour l'amplitude du résolveur. |
| **`--max-amp <val>`** | Amplitude maximale absolue | `1023.0` | Borne supérieure stricte (plage 10 bits du microscope). |
| **`--min-t2p <val>`** | TAP minimal absolu | `1e-6` | Borne inférieure stricte pour le temps au pic du résolveur. |
| **`--max-t2p <val>`** | TAP maximal absolu | `Durée du balayage` | Borne supérieure stricte pour le temps au pic du résolveur. |
| **`--min-fwhm <val>`** | LMH minimale absolue | `0.5` | Borne inférieure stricte (transit capillaire minimal réaliste). |
| **`--max-fwhm <val>`** | LMH maximale absolue | `Durée du balayage` | Borne supérieure stricte pour la LMH du résolveur. |
| **`--qc-amp-fail <val>`** | Seuil d'amplitude pour FAIL | `1.0` | Les modélisations avec une amplitude inférieure sont marquées `FAIL`. |
| **`--qc-t2p-max <val>`** | Seuil d'avertissement pour le TAP | `10.0` | Les modélisations avec un TAP supérieur sont marquées `WARN`. |
| **`--qc-t2p-fail <val>`** | Seuil d'échec pour le TAP | `50.0` | Les modélisations avec un TAP supérieur sont marquées `FAIL`. |
| **`--qc-fwhm-max <val>`** | Seuil d'avertissement pour la LMH | `15.0` | Les modélisations avec une LMH supérieure sont marquées `WARN`. |
| **`--qc-fwhm-fail <val>`** | Seuil d'échec pour la LMH | `100.0` | Les modélisations avec une LMH supérieure sont marquées `FAIL`. |
| **`--qc-cnr-min <val>`** | Seuil d'avertissement pour le RCB | `5.0` | Les modélisations avec un RCB inférieur sont marquées `WARN`. |
| **`--qc-cnr-fail <val>`** | Seuil d'échec pour le RCB | `3.0` | Les modélisations avec un RCB inférieur sont marquées `FAIL`. |

Par exemple, pour traiter un jeu de données avec un seuil d'avertissement de la LMH personnalisé de `20.0` secondes et un seuil d'avertissement du RCB minimal de `6.0` :
```bash
bash run_pipeline_cpp.sh sample-subject-2259 --qc-fwhm-max 20.0 --qc-cnr-min 6.0
```

* **`PASS`** : Modélisation réussie sans atteindre les bornes, RCB > 5.0, LMH entre 0.5–15.0 s, et TAP entre 0.1–10.0 s.
- **`WARN`** : Modélisation réussie, mais un ou plusieurs paramètres ont atteint les limites d'avertissement (ex: LMH > 15 s, RCB entre 3.0 et 5.0, ou paramètre proche d'une borne).
- **`FAIL`** : Modélisation divergente, retour de valeur non définie (`NaN`), ou RCB < 3.0.

---

## 3. Exécuter l'interface graphique interactive C++ : Dear ImGui & ImPlot Studio

Comme les applications graphiques requièrent un accès à l'affichage système, elles doivent être exécutées localement sur votre système d'exploitation hôte. L'interface graphique C++ est un tableau de bord visuel haute performance basé sur le moteur de modélisation C++. Elle utilise Dear ImGui et ImPlot pour afficher les signaux, réviser et trier les cas problématiques, ajuster les paramètres de modélisation et rogner dynamiquement les plages de données.

#### Comment compiler et lancer l'interface graphique C++ :

##### Option A : Script d'installation automatique (macOS uniquement)
Si vous êtes sur macOS, vous pouvez automatiquement compiler, empaqueter et installer l'application native dans votre dossier d'applications avec son icône personnalisée :
```bash
bash install_macos.sh
```
Ce script compile l'application, génère le paquet `BolusTrackingStudio.app` et l'installe dans `/Applications/` (ou `~/Applications/` si non accessible) pour un lancement direct depuis le **Launchpad**, le Finder ou Spotlight.

##### Option B : Compilation manuelle (macOS, Linux, Windows)
1. Assurez-vous que CMake, un compilateur C++17 et la bibliothèque LibTIFF sont installés.
2. Compilez les fichiers localement :
   ```bash
   mkdir -p build && cd build
   cmake -DBUILD_GUI=ON ..
   make -j4
   ```
3. Lancez l'application :
   ```bash
   ./bolus_tracking_gui
   ```
   *(Vous pouvez facultativement passer le chemin d'un fichier CSV en paramètre pour le charger directement : `./bolus_tracking_gui /chemin/vers/results_cpp.csv`)*

#### Principales fonctionnalités de l'interface graphique :
* **Barre latérale de la file d'attente de triage** : Passez en revue les ROI. Cochez "Show WARN/FAIL only" pour filtrer la liste et vous concentrer exclusivement sur les cas nécessitant une intervention.
* **Ajustement interactif des marqueurs** : Faites glisser les trois lignes verticales directement sur le graphique :
  * **Vert** : Temps de début du bolus (Onset)
  * **Jaune** : Temps de pic (Peak)
  * **Rouge** : Temps de fin de premier passage (End)
* **Rognage à la volée de la fenêtre de modélisation** : Déplacez les crochets bleu (début) et magenta (fin) en bas du graphique pour restreindre la fenêtre d'ajustement (ex: exclure un bruit de ligne de base ou un pic de recirculation tardif).
* **Zoom et réinitialisation** : Double-cliquez sur le tracé pour réinitialiser les axes, ou cliquez sur le bouton **Undo Crop** pour restaurer la fenêtre de données complète.
* **Remodélisation manuelle (Re-fit Manual)** : Cliquez pour relancer une modélisation contrainte utilisant vos marqueurs déplacés manuellement comme valeurs d'initialisation et vos limites de rognage comme fenêtre de modélisation active. Les paramètres exportés restent toujours calés par rapport à l'échelle de temps absolue non rognée.
* **Modélisation automatique par lots** : Chargez un dossier, cliquez sur **Run Auto Fit Batch** pour modéliser toutes les ROI automatiquement et mettre à jour la file en temps réel.
* **Ajustement de l'intensité du débruitage** : Ajustez le curseur **Denoise Strength** (0.5x à 3.0x) pour lisser interactivement le signal brut avant l'ajustement.
* **Rétablir les valeurs d'origine (Revert to Original)** : Cliquez sur ce bouton pour annuler les corrections manuelles sur la ROI sélectionnée, restaurer les paramètres automatiques d'origine et effacer les marqueurs manuels.
* **Réinitialiser toutes les modifications (Reset All)** : Dans la barre de menu supérieure, cliquez sur ce bouton pour restaurer l'intégralité du jeu de données chargé à son état d'origine. Une boîte de dialogue de confirmation évite les pertes accidentelles.
* **Vider les données du sujet (Clear Subject)** : Dans la barre de menu supérieure, cliquez sur ce bouton pour décharger les fichiers CSV, TIFF et ROI, ramenant l'interface graphique à son écran d'accueil d'origine.
* **Traduction bilingue (OQLF Compliant)** : Cliquez sur les drapeaux **EN / FR** dans la barre de menu supérieure pour basculer instantanément l'interface graphique entre l'anglais canadien et le français du Québec conforme aux normes de l'Office québécois de la langue française.

---

### Guide de l'opérateur : Triage et correction des modélisations dans l'interface graphique

Si des capillaires sont marqués `AVERT.` ou `ÉCHEC` après l'exécution par lots, utilisez l'interface graphique C++ pour les inspecter et les corriger manuellement :

1. **Isoler les cas problématiques** : Cliquez sur **Load Subject Data** ou importez le fichier CSV de résultats. Cochez la case **"Show WARN/FAIL only"** en haut de la barre latérale pour masquer les ROI réussies (`CONFORME`).
2. **Inspecter le signal brut** : Cliquez sur une ROI marquée pour charger son tracé. Identifiez le problème :
   * *Bruit de ligne de base* : Fluctuations rapides avant l'arrivée du bolus.
   * *Recirculation* : Deuxième montée ou descente très lente du signal après le passage du bolus.
   * *Mauvaise sélection automatique* : La détection automatique s'est calée sur un pic de bruit au lieu du bolus réel.
3. **Définir une fenêtre de rognage (Cropping)** :
   * Déplacez les crochets bleu et magenta. Par exemple, s'il y a une recirculation tardive importante, glissez le crochet droit (magenta) vers la gauche pour l'exclure de l'ajustement.
   * Si la ligne de base avant le bolus est bruitée, glissez le crochet gauche (bleu) vers la droite.
4. **Positionner manuellement les marqueurs temporels** :
   * Glissez les trois lignes colorées sur le graphique :
     * **Ligne verte** : Début du bolus.
     * **Ligne jaune** : Pic du bolus.
     * **Ligne rouge** : Fin du premier passage.
5. **Lancer la remodélisation manuelle** :
   * Cliquez sur **Re-fit Manual**. Le résolveur C++ s'exécute *uniquement* à l'intérieur de la fenêtre rognée en utilisant vos marqueurs comme paramètres initiaux. Le graphique et le statut de la ROI se mettent à jour instantanément.
6. **Sauvegarder et exporter** :
   * Une fois les ajustements terminés, cliquez sur **Save Final CSV**. Les paramètres révisés sont enregistrés. Les temps calculés restent calibrés par rapport à l'échelle temporelle d'origine non rognée.

---

## 4. Flux de travail alternatif : Exécuter localement sans Docker

Si vous ne pouvez pas utiliser Docker, vous pouvez configurer et exécuter les pipelines directement sur votre machine locale :

### Pipeline C++ (Local)
Assurez-vous d'avoir installé CMake, un compilateur compatible C++17, Eigen3 et la bibliothèque libtiff.
```bash
# Compiler et exécuter
mkdir -p build && cd build
cmake ..
make -j4
cd ..
./build/bolus_tracking_cpp --folder sample-subject-2259
```

Pour générer les graphiques SVG associés, ajoutez l'argument `--plot` :
```bash
./build/bolus_tracking_cpp --folder sample-subject-2259 --plot
```
Pour modifier la durée de la ligne de base utilisée pour corriger la dérive (ex: 10 secondes au lieu de 15) :
```bash
./build/bolus_tracking_cpp --folder sample-subject-2259 --plot --drift 10
```

---

## 5. Description technique des fichiers du projet

* `bolus_gui.cpp` : L'interface graphique C++ interactive sous Dear ImGui et ImPlot.
* `bolus_gui.py` : L'interface graphique Python de référence basée sur Tkinter et Matplotlib.
* `run_pipeline.sh` : Script de contrôle principal configurant l'environnement virtuel Python et lançant le traitement.
* `batch_process.py` : Script Python principal lisant les images TIFF, extrayant le signal des ROI et enregistrant les résultats et graphiques.
* `bolus_tracking.py` : Moteur mathématique sous Python (débruitage, estimation, modélisation Gamma et optimisation).
* `run_pipeline_cpp.sh` : Script Bash pour compiler et lancer le pipeline parallèle en C++.
* `bolus_tracking_cpp.cpp` : Code C++ central implémentant l'interpolation par spline cubique, l'ajustement de courbe par Levenberg-Marquardt (Eigen) et l'exécution multithread.
* `bolus_tracking_cpp.hpp` : En-tête C++ déclarant les structures de données.
* `test_bolus_tracking_cpp.cpp` : Suite de tests unitaires C++.
* `CMakeLists.txt` & `Dockerfile.cpp` : Fichiers de configuration CMake et Docker pour le C++.
* `test_bolus_parity.py` & `test_bolus_tracking.py` : Tests unitaires et tests de parité numériques sous Python.
* `BolusTrack_InteractiveEdit.m` : Interface graphique MATLAB héritée pour ajuster les courbes.
* `gammaFun.m` : Définition MATLAB de la fonction Gamma.

---

## 6. Structure des entrées et sorties

### Données d'entrée attendues
Le pipeline scanne le plan de travail à la recherche des dossiers de sujets (ex: `sample-subject-2259`). Chaque dossier doit contenir :
1. **Une image au format TIFF** (ex : `bolus1_baseline.tif`) : Le film fluorescent 3D du passage du bolus.
2. **Un fichier de métadonnées TXT** (ex : `bolus1_baseline.txt`) : Contenant la fréquence d'acquisition (ex : `Fr = 8.16` ou `FrameRate = 8.16`).
3. **Les masques de ROI MATLAB** (ou les coordonnées converties `_rois.txt`) : Définissant les contours géométriques des capillaires.

### Données de sortie générées
Une fois l'exécution terminée, le dossier du sujet contient :
1. **Un fichier de résultats CSV** (`bolus1_baseline_results_cpp.csv` pour le C++ ou `bolus1_baseline_results.csv` pour Python) : Tableau contenant l'amplitude, le temps au pic (TAP), la LMH, la ligne de base, l'AUC, l'AUCn et les temps de transit calculés pour chaque ROI.
2. **Un sous-dossier de graphiques** (`plots_cpp/` pour le C++ ou `plots/` pour Python) : Si activé, contient les figures SVG haute résolution des modélisations.
