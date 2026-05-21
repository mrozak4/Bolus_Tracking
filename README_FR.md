# Studio de suivi de bolus capillaire et de modélisation de courbe Gamma

**[English](README.md) | [Français (Québec)](README_FR.md)**

---

Bienvenue dans le dépôt **Studio de suivi de bolus capillaire et de modélisation de courbe Gamma**. Ce projet propose une suite complète d'outils MATLAB, Python et C++ conçus pour extraire, analyser et modéliser mathématiquement la cinétique de transit de colorants fluorescents dans les capillaires cérébraux pour la recherche sur le couplage neurovasculaire.

---

## 1. Aperçu du projet

Lors d'expériences d'imagerie fonctionnelle, des bolus de colorant fluorescent sont injectés dans le système vasculaire. En suivant l'intensité de fluorescence moyenne (MFI) dans les régions d'intérêt (ROI) des capillaires, nous pouvons mesurer la cinétique de transit.

Ce dépôt fournit des outils pour :
1. **Débruiter et suréchantillonner** les données brutes des séries temporelles de MFI.
2. **Estimer les marqueurs temporels** : Automatiser l'identification du début du bolus, de l'amplitude maximale et de la fin du transit (premier passage) en utilisant des heuristiques robustes basées sur les dérivées.
3. **Ajuster les fonctions de distribution Gamma** : Exécuter un ajustement de courbe robuste en deux passes (moindres carrés linéaires suivis d'une régression robuste pondérée de type Cauchy IRLS) pour calculer des propriétés comme l'amplitude, le temps de pic ($T_{2p}$), la largeur à mi-hauteur (FWHM) et l'aire sous la courbe (AUC).
4. **Vérifier la parité** : Aligner les modélisations automatisées de Python avec les résultats hérités de MATLAB.
5. **Modifier manuellement les modélisations** : Ajuster de manière interactive les courbes d'ajustement à l'aide d'interfaces graphiques modernes.

> [!TIP]
> **Pipeline recommandé** : Bien que nous maintenions un résolveur Python de référence, nous recommandons fortement d'utiliser l'**implémentation C++** parallélisée pour le traitement par lots à l'échelle d'une cohorte. Elle s'exécute **environ 11 fois plus vite** en répartissant les calculs sur tous les cœurs de processeur disponibles.

---

## 2. Guides des répertoires et des guides secondaires

Nous fournissons une documentation détaillée pour chaque partie du code. Veuillez vous référer aux fichiers ci-dessous selon votre flux de travail :

* **[RUNNING_GUIDE.md](RUNNING_GUIDE_FR.md)** : **Commencez ici.** Un guide complet et accessible pour configurer et exécuter le pipeline de traitement C++ et lancer l'interface graphique interactive. Couvre macOS, Linux, Windows et Docker.
* **[INSTALL.md](INSTALL_FR.md)** : **Instructions d'installation.** Configuration, compilation, empaquetage et installation de l'application graphique (incluant les paquets d'application macOS `.app` et les icônes personnalisées).
* **[PARITY_REPORT.md](PARITY_REPORT_FR.md)** : **Rapport de performance et de parité.** Compare les vitesses d'exécution et les résultats numériques des implémentations Python et C++, et fournit des recommandations.
* **[README_Python_Pipeline.md](README_Python_Pipeline_FR.md)** : **Guide de référence Python.** Détaille l'implémentation Python, les fonctions de perte personnalisées (pondérations de Cauchy), l'interface graphique Python et les paramètres d'optimisation numérique.
* **[README_BolusAnalysis.md](README_BolusAnalysis_FR.md)** : Aperçu général de la cinétique des bolus et des modèles mathématiques associés.
* **[README_ApplyRegistrationToMask.md](README_ApplyRegistrationToMask_FR.md)** : Décrit le script MATLAB d'alignement d'images (`ApplyRegistrationToMask.m`) utilisé pour déformer les masques de ROI afin de suivre les mouvements spatiaux au fil du temps.

---

## 3. Structure des fichiers du projet

### Outils parallèles et d'interface graphique en C++ (Recommandé)
* **`run_pipeline_cpp.sh`** : Script enveloppe en ligne de commande pour compiler et exécuter le pipeline C++ en parallèle avec Docker ou localement.
* **`bolus_tracking_cpp.cpp`** : Code source C++ principal implémentant le suréchantillonner par spline, l'ajustement de courbe de Levenberg-Marquardt (via Eigen) et le traitement multithread.
* **`bolus_tracking_cpp.hpp`** : Fichier d'en-tête C++ contenant les structures de données.
* **`bolus_gui.cpp`** : Interface graphique interactive multiplateforme en C++ construite avec Dear ImGui et ImPlot pour inspecter et corriger manuellement les modélisations.
* **`test_bolus_tracking_cpp.cpp`** : Suite de tests unitaires C++ vérifiant les calculs mathématiques, les modèles d'ajustement et les cas limites.
* **`CMakeLists.txt`** : Fichier de configuration de compilation CMake.
* **`Dockerfile.cpp`** : Configuration Docker pour compiler, tester et conteneuriser le pipeline C++.

### Outils de référence et d'interface graphique en Python
* **`bolus_gui.py`** : Interface Python interactive haut de gamme pour parcourir visuellement les jeux de données, sélectionner les ROI, ajuster les marqueurs graphiques et exporter les résultats.
* **`run_pipeline.sh`** : Script enveloppe pour configurer l'environnement virtuel Python et exécuter le pipeline par lots de référence.
* **`batch_process.py`** : Scanne les répertoires pour identifier les triplets (image TIFF, masque MAT, métadonnées TXT), extrait les signaux, exécute les modélisations et enregistre les CSV.
* **`bolus_tracking.py`** : Algorithmes mathématiques centraux (filtrage, suréchantillonnage, détection du début/pic/fin et modélisation de courbe).
* **`test_bolus_parity.py`** : Suite de tests de parité vérifiant les sorties numériques de Python par rapport à MATLAB.

### Outils patrimoniaux basés sur MATLAB
* **`BolusTrack_InteractiveEdit.m`** : Interface graphique MATLAB pour visualiser et ajuster manuellement les courbes de bolus.
* **`gammaFun.m`** : Implémentation standard MATLAB de la fonction Gamma.
* **`ApplyRegistrationToMask.m` & `GlobalShiftMask.m`** : Alignement et recalage des masques de ROI sur les images.
* **`calcFWHM.m`, `denoiseTrace.m`, `findMaskObjInData.m`, `parseFrameRateFromMetadata.m`** : Fonctions auxiliaires MATLAB.

---

## 4. Résumé de démarrage rapide

Pour des instructions détaillées, veuillez consulter le **[RUNNING_GUIDE.md](RUNNING_GUIDE_FR.md)**.

### Exécuter le traitement automatique par lots (C++ parallélisé recommandé)
Pour compiler et exécuter le pipeline C++ haute performance via Docker :
```bash
bash run_pipeline_cpp.sh
```
Cela traite tous les sujets en parallèle. Pour exécuter plutôt le pipeline Python de référence :
```bash
bash run_pipeline.sh
```

### Lancer l'interface graphique C++

#### Sur macOS (Paquet d'application exécutable)
Compiler, empaqueter et installer l'interface graphique en tant qu'application macOS native accessible depuis le Launchpad :
```bash
bash install_macos.sh
open /Applications/BolusTrackingStudio.app  # Ou dans ~/Applications/
```

#### Sur Linux, Windows ou configuration manuelle sur macOS
Compiler et exécuter l'exécutable localement :
```bash
mkdir -p build && cd build
cmake -DBUILD_GUI=ON ..
make -j4
./bolus_tracking_gui
```
Cela lance le tableau de bord haute performance construit sur Dear ImGui et ImPlot. Vous pouvez y sélectionner des dossiers, trier les ROI par statut (CONFORME/AVERT./ÉCHEC), glisser les marqueurs verticaux (début du bolus, pic, fin), rogner la fenêtre de modélisation à la volée, sauvegarder et restaurer automatiquement l'état de session via des fichiers `.gui_state`, ajuster interactivement le débruitage, réinitialiser ou rétablir les états automatiques, vider le plan de travail et exporter les paramètres.

### Lancer l'interface graphique Python
Exécuter :
```bash
.venv/bin/python bolus_gui.py
```
Cela ouvre la fenêtre Tkinter pour charger les jeux de données, parcourir les capillaires, déplacer les marqueurs par simple clic et sauvegarder les résultats.

---

## 5. Format des données de sortie

Le pipeline par lots génère des fichiers CSV contenant les métriques suivantes pour chaque ROI capillaire :

| Colonne | Description |
| :--- | :--- |
| **ROI** | Identifiant (1-indexé) de la région d'intérêt capillaire. |
| **SubjNum** | Identifiant du sujet extrait du chemin des données. |
| **Exp** | Nom de la condition expérimentale (ex. `bolus1_baseline`). |
| **InitAmp** / **F_Amp** | Estimation initiale et finale de l'amplitude maximale du bolus (en unités d'intensité UA). |
| **InitT2p** / **F_T2p** | Estimation initiale et finale du temps de pic (en **secondes**). |
| **InitFWHM** / **F_FWHM** | Estimation initiale et finale de la largeur à mi-hauteur (en **secondes**). |
| **InitM** / **F_M** | Estimation initiale et finale de la valeur moyenne de la ligne de base (en **UA**). |
| **InitSNR** / **F_SNR** | Rapport signal-sur-bruit de la ligne de base ($\text{Moyenne} / \text{Écart-type du bruit}$). |
| **InitCNR** / **F_CNR** | Rapport contraste-sur-bruit du bolus ($\text{Amplitude du bolus} / \text{Écart-type du bruit}$). |
| **Click1_Start_T** | Temps de début de la ligne de base (en **secondes**). |
| **Click2_Onset_T** | Temps de début du bolus (en **secondes**). |
| **Click3_Peak_T** | Temps du pic du bolus (en **secondes**). |
| **Click4_End_T** | Temps de fin du premier passage / dégagement du bolus (en **secondes**). |
| **AUC** | Intégration trapézoïdale de la courbe de bolus modélisée. |
| **AUCn** | Intégration trapézoïdale de la courbe normalisée (min-max). |
| **TTlb** | Limite inférieure du temps de transit (borne inférieure de l'intervalle de confiance à 95 % du temps de pic par rapport au début). |
| **TTm** | Temps de transit du pic (temps de pic relatif au début). |
| **TThb** | Limite supérieure du temps de transit (borne supérieure de l'intervalle de confiance à 95 % du temps de pic par rapport au début). |
| **OnT** | Temps de début relatif au début de la fenêtre de modélisation (seuil de 0,1 du signal normalisé). |
| **OnTSc** | Temps de début dans le balayage (relatif au début du bolus le plus précoce mesuré dans la séance). |
| **ROISize** | Taille du masque de la région d'intérêt (en pixels). |
| **Denoise_RMS** | Écart quadratique moyen (RMS) du bruit éliminé lors du débruitage (en **UA**). |
| **VesType** | Classification suggérée pour le type de vaisseau (`A` pour artériole, `V` pour veinule, `C` pour capillaire, `U` pour inconnu). |
| **QC_Flag** | Indicateur de contrôle de qualité (`CONFORME`, `AVERT.` ou `ÉCHEC`). |
| **Fit_Source** | Source des paramètres d'ajustement (`auto` pour automatique, `population_prior` pour loi a priori ou `manual` pour manuel). |

---

## 6. Limites physiologiques des paramètres et contrôle de qualité

Pour prévenir les modélisations physiquement aberrantes (valeurs infinies/négatives, pics de transit trop lents, etc.), le pipeline impose des limites strictes basées sur des critères physiologiques et des vérifications de contrôle de qualité (CQ).

### Bornes de contraintes par défaut :
* **Amplitude (`F_Amp`)** : Contrainte entre `1e-6` et `1023.0`. La borne supérieure correspond à la plage dynamique d'un numériseur de microscope 10 bits.
* **Temps de pic (`F_T2p`)** : Contraint entre `1e-6` et la **durée de la fenêtre de modélisation**. Cela garantit que le pic se situe à l'intérieur de la fenêtre d'enregistrement.
* **Largeur à mi-hauteur (`F_FWHM`)** : Contrainte entre `0.5` seconde et la **durée de la fenêtre de modélisation**. La limite inférieure de `0.5` seconde représente la vitesse maximale physiologiquement réaliste de passage du colorant dans un capillaire.
* **Décalage de la ligne de base (`F_M`)** : Contraint dynamiquement en fonction de l'écart-type du bruit de fond estimé pour éviter les divergences de l'optimiseur.

### Niveaux de statut de contrôle de qualité (`QC_Flag`) :
- **`CONFORME`** : Modélisation réussie, aucun paramètre à moins de 1 % des bornes absolues du résolveur, $F\_CNR > 5.0$, $F\_FWHM \in [0.5, 15.0]\text{ s}$, et $F\_T2p \in [0.1, 10.0]\text{ s}$.
- **`AVERT.`** : Modélisation réussie, mais un ou plusieurs paramètres ont atteint les limites d'avertissement, $F\_CNR \in [3.0, 5.0]$, ou un paramètre est proche d'une frontière de recherche.
- **`ÉCHEC`** : Modélisation divergente, retour de valeur non définie (`NaN`), ou $F\_CNR < 3.0$.

### Classifications suggérées de types de vaisseaux (`VesType`) :
Les modélisations déclarées valides sont classées selon les critères suivants :
- **Artériole (`A`)** : Début précoce ($OnT < 1.8\text{ s}$) et temps de transit rapide ($TTm < 3.0\text{ s}$).
- **Veinule (`V`)** : Début tardif ($OnT > 3.0\text{ s}$) ou temps de transit prolongé ($TTm > 4.5\text{ s}$).
- **Capillaire (`C`)** : Cas situés dans les plages intermédiaires.
- **Inconnu (`U`)** : Modélisations ayant échoué ou retournant des valeurs non définies.

Ces limites de classification sont appliquées de manière identique dans le pipeline C++, le script Python de traitement par lots et l'interface graphique interactive. Des options de contournement sont également disponibles via la ligne de commande (ex : `--min-t2p`, `--max-amp`, etc.).
