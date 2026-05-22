# Suivi du bolus : Pipeline Python

**[English](README_Python_Pipeline.md) | [Français (Québec)](README_Python_Pipeline_FR.md)**

---

Ce document explique comment exécuter le pipeline de suivi de bolus automatisé en Python. Ce pipeline remplace le flux de travail manuel de l'interface graphique MATLAB, permettant un traitement par lots sans tête (headless) sur n'importe quel système (y compris votre machine d'alignement sous Linux).

## Prérequis

Avant d'exécuter les scripts Python, vous devez convertir vos anciens fichiers MATLAB `MaskObj.mat` dans un format propre que Python peut lire.

1. **Convertir les masques (étape unique) :**
   Ouvrez MATLAB dans ce répertoire et exécutez le script de conversion :
   ```matlab
   run('matlab/convert_masks_for_python.m')
   ```
   Cela va automatiquement trouver chaque fichier `MaskObj.mat` dans vos dossiers et créer un fichier dupliqué nommé `adjusted_<OriginalName>.mat`. **Vous utiliserez ces fichiers `adjusted_*.mat` pour le pipeline Python.**

## Configuration

Le pipeline est désormais entièrement conteneurisé avec Docker ! Vous n'avez pas besoin d'installer de dépendances Python localement si vous préférez l'éviter.

Si vous exécutez simplement le script `run_pipeline.sh`, il va automatiquement compiler le conteneur Docker en utilisant les versions figées dans `requirements.txt` et exécuter le traitement des données à l'intérieur.

*(Si vous préférez exécuter l'application localement sans Docker, vous pouvez toujours utiliser l'approche classique `python3 -m venv .venv` et installer les dépendances à partir de `requirements.txt`).*

## Exécution du pipeline

Le script principal est `python/src/batch_process.py`. Il prend en entrée une pile d'images TIFF alignées, un fichier de masque MAT ajusté et un fichier de métadonnées TXT.

### Utilisation
Vous pouvez soit fournir un dossier spécifique pour détecter automatiquement tous les fichiers correspondants, soit les spécifier individuellement.

**Pour détecter automatiquement les fichiers dans un dossier :**
```bash
python python/src/batch_process.py --folder <chemin_dossier> --outdir <repertoire_sortie>
```
*(Le script trouvera et associera automatiquement les images TIFF, les fichiers de métadonnées `.txt` et les masques `adjusted_*.mat` sur la base de la convention de nommage `bolusX_condition`).*

**Pour spécifier les fichiers individuellement :**
```bash
python python/src/batch_process.py --tiff <chemin_tif> --mask <chemin_masque_mat> --meta <chemin_txt> --outdir <repertoire_sortie>
```

### Exemple (Script avec Docker)
Voici comment traiter automatiquement l'ensemble du jeu de données `sample-subject-2259` à l'aide du script :

```bash
./run_pipeline.sh sample-subject-2259
```
Ce script va :
1. Exécuter MATLAB en arrière-plan pour convertir les masques.
2. Compiler le conteneur Docker (si ce n'est pas déjà fait).
3. Transmettre le dossier au conteneur Docker pour exécuter l'analyse Python.
4. Enregistrer directement les fichiers de résultats CSV dans votre répertoire actuel !

### Exemple manuel (Python local)
Si vous exécutez le script Python manuellement (sans le script `.sh`), vous pouvez utiliser :

*(Note : Si vous obteniez une erreur `FileNotFoundError` précédemment, c'est parce que la commande d'exemple contenait le nom générique `adjusted_maskObj.mat` au lieu du nom réel du fichier, ex. `adjusted_3554_bolus1_baseline_shifted_MaskObj.mat`).*

## Données de sortie
Le script génère un fichier CSV (ex. `3554_bolus1_baseline_123-300_shifted_results.csv`) contenant les paramètres estimés et modélisés pour chaque masque de ROI. Les colonnes de sortie s'alignent avec le pipeline C++ :
- `InitAmp`, `InitT2p`, `InitFWHM`, `InitM`, `InitSNR`, `InitCNR` (Estimations initiales et métriques de bruit).
- `F_Amp`, `F_T2p`, `F_FWHM`, `F_M`, `F_SNR`, `F_CNR` (Paramètres de courbe Gamma finaux et métriques).
- `AUC`, `AUCn`, `OnT`, `OnTSc`, `TTlb`, `TTm`, `TThb` (Cinétique calculée : aire sous les courbes, temps de début, temps de transit et intervalles de confiance).
- `QC_Flag`, `Fit_Source`, `VesType` (Indicateurs de contrôle de qualité, origine de la source des paramètres et classification suggérée des types de vaisseaux).

---

## Interface graphique interactive Python (Studio Tkinter et Matplotlib)

L'interface graphique Python fournit un tableau de bord interactif pour parcourir visuellement les jeux de données, sélectionner les ROI, cliquer sur les tracés pour ajuster les marqueurs, lancer les modélisations et enregistrer les résultats.

### Comment lancer l'interface graphique Python :
1. Créez un environnement virtuel Python et installez les dépendances :
   ```bash
   # macOS / Linux
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r requirements.txt
   python python/src/bolus_gui.py
   
   # Windows (PowerShell)
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   python python/src/bolus_gui.py
   ```
2. **Instructions d'utilisation** :
   - **Charger les données du sujet** : Cliquez sur **📁 Open Subject Folder** et sélectionnez le dossier cible.
   - **Sélectionner le jeu de données** : Choisissez le triplet de données dans le menu déroulant.
   - **Sélectionner la ROI** : Naviguez entre les différentes régions d'intérêt des capillaires.
   - **Ajustement interactif des marqueurs** : Cliquez sur **Adjust Markers** puis cliquez sur le graphique pour ajuster les points de début, de pic et de fin. La courbe se mettra à jour instantanément !
   - **Enregistrer et exporter** : Cliquez sur **💾 Save & Export Results** pour enregistrer le fichier CSV et exporter une capture d'écran haute résolution.

---

## Configuration des contraintes de paramètres sous Python

Lorsque vous exécutez le script de traitement par lots `python/src/batch_process.py`, vous pouvez transmettre des limites personnalisées pour contraindre les paramètres d'ajustement à des valeurs physiologiquement plausibles :
- `--min-amp` (par défaut : `1e-6`)
- `--max-amp` (par défaut : `1023.0` - correspondant à la limite du numériseur de microscope 10 bits)
- `--min-t2p` (par défaut : `1e-6`)
- `--max-t2p` (par défaut : limité dynamiquement à la durée de la fenêtre de modélisation)
- `--min-fwhm` (par défaut : `0.5` seconde - correspondant aux vitesses de transit rapides du colorant)
- `--max-fwhm` (par défaut : limité dynamiquement à la durée de la fenêtre de modélisation)

Par exemple, pour contraindre le temps au pic (TAP) entre 2.0 et 8.0 secondes :
```bash
python python/src/batch_process.py --folder sample-subject-2259 --min-t2p 2.0 --max-t2p 8.0
```

### Niveaux de statut de contrôle de qualité (`QC_Flag`) :
- **`PASS`** : Modélisation réussie, aucun paramètre à moins de 1 % des bornes absolues du solveur, $F\_CNR > 5.0$, $F\_LMH \in [0.5, 15.0]\text{ s}$, et $F\_TAP \in [0.1, 10.0]\text{ s}$.
- **`WARN`** : Modélisation réussie, mais un ou plusieurs paramètres ont atteint les limites d'avertissement, $F\_CNR \in [3.0, 5.0]$, ou un paramètre est proche d'une frontière de recherche du solveur (évalué par rapport aux bornes de recherche relâchées `Amplitude: [1.0, max_amp]`, `TAP: [0.01, 12.0]` et `LMH: [0.1, 20.0]` si une passe d'ajustement de secours a été exécutée).
- **`FAIL`** : Modélisation divergente, retour de valeur non définie (`NaN`), ou $F\_CNR < 3.0$.
