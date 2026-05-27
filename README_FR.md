# Studio de suivi de bolus capillaire et de modélisation de courbe Gamma

[![Télécharger la dernière version](https://img.shields.io/github/v/release/mrozak4/Bolus_Tracking?label=Télécharger%20la%20dernière%20version&style=for-the-badge&color=E08C40)](https://github.com/mrozak4/Bolus_Tracking/releases/latest)

**[English](README.md) | [Français (Québec)](README_FR.md)**

![Bolus Tracking Studio](docs/app_screenshot_fr.png?v=2.3.1)

### Installation en 1 clic

Cliquez sur le bouton **Télécharger** ci-dessus, puis choisissez le bon fichier pour votre système :

| Plateforme | Fichier à télécharger | Étapes d'installation |
|----------|-----------------|---------------|
| 🍎 **macOS** (Apple Silicon) | `Bolus.Tracking.Studio-2.3.1-arm64.dmg` | (Puces M1/M2/M3/M4) Ouvrir le `.dmg`, glisser dans **Applications**, voir la note ⬇️ |
| 🍎 **macOS** (Intel) | `Bolus.Tracking.Studio-2.3.1.dmg` | (Anciens Mac Intel) Ouvrir le `.dmg`, glisser dans **Applications**, voir la note ⬇️ |
| 🪟 **Windows** | `Bolus.Tracking.Studio.Setup.2.3.1.exe` | Exécuter l'installateur |
| 🐧 **Linux** | `Bolus.Tracking.Studio-2.3.1.AppImage` | `chmod +x *.AppImage` puis exécuter (voir la note ci-dessous ⬇️) |

> [!IMPORTANT]
> **macOS « fichier endommagé et ne peut pas être ouvert »** — L'application n'est pas signée avec un identifiant de développeur Apple, donc macOS Gatekeeper la bloquera. Après avoir glissé l'application dans Applications, ouvrez le **Terminal** et exécutez :
> ```bash
> xattr -cr "/Applications/Bolus Tracking Studio.app"
> ```
> Puis ouvrez l'application normalement. Vous pouvez aussi faire **clic droit → Ouvrir** au premier lancement.

> [!TIP]
> **L'AppImage Linux ne fonctionne pas ?** Si vous obtenez une erreur FUSE ou si l'application ne démarre pas, essayez :
> ```bash
> # Option 1 : Exécuter sans FUSE
> ./Bolus.Tracking.Studio-*.AppImage --appimage-extract-and-run
>
> # Option 2 : Téléchargez le .tar.gz depuis la page des Releases
> tar xzf Bolus.Tracking.Studio-*.tar.gz
> cd bolus-tracking-studio
> ./bolus-tracking-studio
> ```

---

Bienvenue dans le dépôt **Studio de suivi de bolus capillaire et de modélisation de courbe Gamma**. Ce projet propose une suite complète d'outils MATLAB, Python et C++ conçus pour extraire, analyser et modéliser mathématiquement la cinétique de transit de colorants fluorescents dans les capillaires cérébraux pour la recherche sur le couplage neurovasculaire.

---

## 1. Aperçu du projet

Lors d'expériences d'imagerie fonctionnelle, des bolus de colorant fluorescent sont injectés dans le système vasculaire. En suivant l'intensité de fluorescence moyenne (MFI) dans les régions d'intérêt (ROI) des capillaires, nous pouvons mesurer la cinétique de transit.

Ce dépôt fournit des outils pour :
1. **Débruiter et suréchantillonner** les données brutes des séries chronologiques de MFI.
2. **Estimer les marqueurs temporels** : Automatiser l'identification du début du bolus, de l'amplitude maximale et de la fin du transit (premier passage) en utilisant des heuristiques robustes basées sur les dérivées.
3. **Ajuster les fonctions de distribution Gamma** : Exécuter un ajustement de courbe robuste en deux passes (moindres carrés linéaires suivis d'une régression robuste pondérée de type Cauchy IRLS) pour calculer des propriétés comme l'amplitude, le temps au pic ($T_{2p}$), la largeur à mi-hauteur (LMH) et l'aire sous la courbe (AUC).
4. **Vérifier la parité** : Aligner les modélisations automatisées de Python avec les résultats hérités de MATLAB.
5. **Modifier manuellement les modélisations** : Ajuster de manière interactive les courbes d'ajustement à l'aide d'interfaces graphiques modernes.

> [!TIP]
> **Chaîne de traitement recommandée** : Bien que nous maintenions un solveur Python de référence, nous recommandons fortement d'utiliser l'**implémentation C++** parallélisée pour le traitement par lots à l'échelle d'une cohorte. Elle s'exécute **environ 11 fois plus vite** en répartissant les calculs sur tous les cœurs de processeur disponibles.

---

## 2. Guides des répertoires et des guides secondaires

Nous fournissons une documentation détaillée pour chaque partie du code. Veuillez vous référer aux fichiers ci-dessous selon votre flux de travail :

* **[RUNNING_GUIDE.md](docs/RUNNING_GUIDE_FR.md)** : **Commencez ici.** Un guide complet et accessible pour configurer et exécuter la chaîne de traitement C++ et lancer l'interface graphique interactive. Couvre macOS, Linux, Windows et Docker.
* **[INSTALL.md](docs/INSTALL_FR.md)** : **Instructions d'installation.** Configuration, compilation, empaquetage et installation de l'application graphique (incluant les paquets d'application macOS `.app` et les icônes personnalisées).
* **[PARITY_REPORT.md](docs/PARITY_REPORT_FR.md)** : **Rapport de performance et de parité.** Compare les vitesses d'exécution et les résultats numériques des implémentations Python et C++, et fournit des recommandations.
* **[README_Python_Pipeline.md](docs/README_Python_Pipeline_FR.md)** : **Guide de référence Python.** Détaille l'implémentation Python, les fonctions de perte personnalisées (pondérations de Cauchy), l'interface graphique Python et les paramètres d'optimisation numérique.
* **[README_BolusAnalysis.md](docs/README_BolusAnalysis_FR.md)** : Aperçu général de la cinétique des bolus et des modèles mathématiques associés.
* **[README_ApplyRegistrationToMask.md](docs/README_ApplyRegistrationToMask_FR.md)** : Décrit le script MATLAB d'alignement d'images (`ApplyRegistrationToMask.m`) utilisé pour déformer les masques de ROI afin de suivre les mouvements spatiaux au fil du temps.

---

## 3. Structure des fichiers du projet

### Outils parallèles et d'interface graphique en C++ (Recommandé)
* **`run_pipeline_cpp.sh`** : Script enveloppe en ligne de commande pour compiler et exécuter le pipeline C++ en parallèle avec Docker ou localement.
* **Fichiers d'implémentation du pipeline C++** :
  * **`cpp/src/signal_processing.cpp`** : Interpolation par spline, lissage gaussien et détection de valeurs aberrantes.
  * **`cpp/src/bolus_fitting.cpp`** : Ajustement de courbe non linéaire de Levenberg-Marquardt (via Eigen) et estimation automatique des paramètres.
  * **`cpp/src/roi_rasterizer.cpp`** : Rastérisation de polygones pour les masques de ROI capillaires.
  * **`cpp/src/bolus_visualizer.cpp`** : Fonctions de traçage SVG et formatage de la disposition des graduations.
  * **`cpp/src/dataset_processor.cpp`** : Lecture d'images (via LibTIFF), élimination de dérive et filtrage de contrôle de qualité en 3 passes.
  * **`cpp/src/batch_processor.cpp`** : Analyseur de répertoire, analyse des métadonnées de fréquence d'acquisition et appariement de fichiers.
  * **`cpp/src/main.cpp`** : Point d'entrée de l'exécution en ligne de commande.
* **`cpp/include/bolus_tracking_cpp.hpp`** : En-tête C++ unifié déclarant toutes les structures, paramètres et interfaces de classe du pipeline.
* **`cpp/src/bolus_gui.cpp`** : ~~Interface graphique interactive en C++ construite avec Dear ImGui et ImPlot.~~ **OBSOLÈTE** — conservé à titre de référence. Voir `gui/` pour l'interface actuelle.
* **`cpp/tests/test_bolus_tracking_cpp.cpp`** : Suite de tests unitaires C++ vérifiant les calculs mathématiques, les modèles d'ajustement et les cas limites.
* **`CMakeLists.txt`** : Fichier de configuration de compilation CMake.
* **`Dockerfile.cpp`** : Configuration Docker pour compiler, tester et conteneuriser le pipeline C++.

### Interface graphique Electron (outil interactif principal)
* **`gui/`** : L'**interface graphique interactive principale** pour le triage et le contrôle qualité. Construite sur Electron (Chromium) avec rendu SVG en C++ et le thème sombre MCM. Voir **[gui/README_FR.md](gui/README_FR.md)** pour la documentation complète.

### Outils de référence en Python
* **`python/src/bolus_gui.py`** : ~~Interface Python interactive.~~ **OBSOLÈTE** — conservé à titre de référence. Utilisez l'interface graphique Electron.
* **`run_pipeline.sh`** : Script enveloppe pour configurer l'environnement virtuel Python et exécuter le pipeline par lots de référence.
* **`python/src/batch_process.py`** : Scanne les répertoires pour identifier les triplets (image TIFF, masque MAT, métadonnées TXT), extrait les signaux, exécute les modélisations et enregistre les CSV.
* **`python/src/bolus_tracking.py`** : Algorithmes mathématiques centraux (filtrage, suréchantillonnage, détection du début/pic/fin et modélisation de courbe).
* **`python/tests/test_bolus_parity.py`** : Suite de tests de parité vérifiant les sorties numériques de Python par rapport à MATLAB.

### Outils patrimoniaux basés sur MATLAB
* **`matlab/src/BolusTrack_InteractiveEdit.m`** : Interface graphique MATLAB pour visualiser et ajuster manuellement les courbes de bolus.
* **`matlab/src/gammaFun.m`** : Implémentation standard MATLAB de la fonction Gamma.
* **`matlab/src/ApplyRegistrationToMask.m` & `matlab/src/GlobalShiftMask.m`** : Alignement et recalage des masques de ROI sur les images.
* **`matlab/src/calcFWHM.m`, `matlab/src/denoiseTrace.m`, `matlab/src/findMaskObjInData.m`, `matlab/src/parseFrameRateFromMetadata.m`** : Fonctions auxiliaires MATLAB.

---

## 4. Résumé de démarrage rapide

Pour des instructions détaillées, veuillez consulter le **[RUNNING_GUIDE.md](docs/RUNNING_GUIDE_FR.md)**.

### Exécuter le traitement automatique par lots (C++ parallélisé recommandé)
Pour compiler et exécuter le pipeline C++ haute performance via Docker :
```bash
bash run_pipeline_cpp.sh
```
Cela traite tous les sujets en parallèle. Pour exécuter plutôt le pipeline Python de référence :
```bash
bash run_pipeline.sh
```

### Lancer l'interface graphique interactive (Electron — Recommandé)
```bash
cd gui && npm install && npm start
```
Cela lance le Bolus Tracking Studio avec le thème sombre MCM, les graphiques SVG en C++, l'animation d'écran d'accueil, 44 langues et le flux de travail complet de triage. Voir **[gui/README_FR.md](gui/README_FR.md)** pour les détails.

> [!NOTE]
> **Prérequis** : Node.js ≥ 18 et un binaire `bolus_server` compilé (exécutez `cd build && cmake .. && make bolus_server`).

<details>
<summary>Interfaces graphiques héritées (obsolètes)</summary>

#### Ancien : Interface graphique C++ Dear ImGui
```bash
mkdir -p build && cd build
cmake -DBUILD_GUI=ON ..
make -j4
./bolus_tracking_gui
```
> ⚠️ **Obsolète.** Nécessite GLFW, OpenGL et des bibliothèques graphiques natives. Utilisez l'interface Electron.

#### Ancien : Interface graphique Python tkinter
```bash
.venv/bin/python python/src/bolus_gui.py
```
> ⚠️ **Obsolète.** Plus lente, sans prise en charge multilingue. Utilisez l'interface Electron.

</details>

---

## 5. Format des données de sortie

La chaîne de traitement par lots génère des fichiers CSV contenant les métriques suivantes pour chaque ROI capillaire :

| Colonne | Description |
| :--- | :--- |
| **ROI** | Identifiant (1-indexé) de la région d'intérêt capillaire. |
| **SubjNum** | Identifiant du sujet extrait du chemin des données. |
| **Exp** | Nom de la condition expérimentale (ex. `bolus1_baseline`). |
| **InitAmp** / **F_Amp** | Estimation initiale et finale de l'amplitude maximale du bolus (en unités d'intensité UA). |
| **InitT2p** / **F_T2p** | Estimation initiale et finale du temps au pic (en **secondes**). |
| **InitFWHM** / **F_FWHM** | Estimation initiale et finale de la largeur à mi-hauteur (LMH) (en **secondes**). |
| **InitM** / **F_M** | Estimation initiale et finale de la valeur moyenne de la ligne de base (en **UA**). |
| **InitSNR** / **F_SNR** | Rapport signal sur bruit (RSB) de la ligne de base ($\text{Moyenne} / \text{Écart-type du bruit}$). |
| **InitCNR** / **F_CNR** | Rapport contraste-bruit (RCB) du bolus ($\text{Amplitude du bolus} / \text{Écart-type du bruit}$). |
| **Click1_Start_T** | Temps de début de la ligne de base (en **secondes**). |
| **Click2_Onset_T** | Temps de début du bolus (en **secondes**). |
| **Click3_Peak_T** | Temps du pic du bolus (en **secondes**). |
| **Click4_End_T** | Temps de fin du premier passage / dégagement du bolus (en **secondes**). |
| **AUC** | Intégration trapézoïdale de la courbe de bolus modélisée. |
| **AUCn** | Intégration trapézoïdale de la courbe normalisée (min-max). |
| **TTlb** | Limite inférieure du temps de transit (borne inférieure de l'intervalle de confiance à 95 % du temps au pic par rapport au début). |
| **TTm** | Temps de transit du pic (temps au pic relatif au début). |
| **TThb** | Limite supérieure du temps de transit (borne supérieure de l'intervalle de confiance à 95 % du temps au pic par rapport au début). |
| **OnT** | Temps de début (TD) relatif au début de la fenêtre d'ajustement (seuil de 0,1 du signal normalisé). |
| **OnTSc** | Temps de début dans le balayage (relatif au début du bolus le plus précoce mesuré dans la séance). |
| **ROISize** | Taille du masque de la région d'intérêt (en pixels). |
| **Denoise_RMS** | Écart quadratique moyen (RMS) du bruit éliminé lors du débruitage (en **UA**). |
| **VesType** | Classification suggérée pour le type de vaisseau (`A` pour artériole, `V` pour veinule, `C` pour capillaire, `U` pour inconnu). |
| **QC_Flag** | Indicateur de contrôle de qualité (`PASS`, `WARN` ou `FAIL`). |
| **Fit_Source** | Source des paramètres d'ajustement (`auto` pour automatique, `population_prior` pour loi a priori ou `manual` pour manuel). |

---

## 6. Limites physiologiques des paramètres et contrôle de qualité

Pour prévenir les modélisations physiquement aberrantes (valeurs infinies/négatives, pics de transit trop lents, etc.), la chaîne de traitement impose des limites strictes basées sur des critères physiologiques et des vérifications de contrôle de qualité (CQ).

### Bornes de contraintes par défaut :
* **Amplitude (`F_Amp`)** : Contrainte entre `1e-6` et `1023.0`. La borne supérieure correspond à la plage dynamique d'un numériseur de microscope 10 bits.
* **Temps au pic (`F_T2p`)** : Contraint entre `1e-6` et la **durée de la fenêtre d'ajustement**. Cela garantit que le pic se situe à l'intérieur de la fenêtre d'enregistrement.
* **Largeur à mi-hauteur (`F_FWHM`)** : Contrainte entre `0.5` seconde et la **durée de la fenêtre d'ajustement**. La limite inférieure de `0.5` seconde représente la vitesse maximale physiologiquement réaliste de passage du colorant dans un capillaire.
* **Décalage de la ligne de base (`F_M`)** : Contraint dynamiquement en fonction de l'écart-type du bruit de fond estimé pour éviter les divergences de l'optimiseur.

### Niveaux de statut de contrôle de qualité (`QC_Flag`) :
- **`PASS`** : Modélisation réussie, aucun paramètre à moins de 1 % des bornes absolues du solveur, $F\_CNR > 5.0$, $F\_LMH \in [0.5, 15.0]\text{ s}$, et $F\_TAP \in [0.1, 10.0]\text{ s}$.
- **`WARN`** : Modélisation réussie, mais un ou plusieurs paramètres ont atteint les limites d'avertissement, $F\_CNR \in [3.0, 5.0]$, ou un paramètre est proche d'une frontière de recherche du solveur (évalué par rapport aux bornes de recherche relâchées `Amplitude: [1.0, max_amp]`, `TAP: [0.01, 12.0]` et `LMH: [0.1, 20.0]` si une passe d'ajustement de secours a été exécutée).
- **`FAIL`** : Modélisation divergente, retour de valeur non définie (`NaN`), ou $F\_CNR < 3.0$.

### Classifications suggérées de types de vaisseaux (`VesType`) :
Les modélisations déclarées valides sont classées selon les critères suivants :
- **Artériole (`A`)** : Début précoce ($OnT < 1.8\text{ s}$) et temps de transit rapide ($TTm < 3.0\text{ s}$).
- **Veinule (`V`)** : Début tardif ($OnT > 3.0\text{ s}$) ou temps de transit prolongé ($TTm > 4.5\text{ s}$).
- **Capillaire (`C`)** : Cas situés dans les plages intermédiaires.
- **Inconnu (`U`)** : Modélisations ayant échoué ou retournant des valeurs non définies.

Ces limites de classification sont appliquées de manière identique dans la chaîne de traitement C++, le script Python de traitement par lots et l'interface graphique interactive. Des options de contournement sont également disponibles via la ligne de commande (ex : `--min-t2p`, `--max-amp`, etc.).
