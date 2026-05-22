# Boîte d'outils d'analyse de bolus — Guide d'utilisation

**[English](README_BolusAnalysis.md) | [Français (Québec)](README_BolusAnalysis_FR.md)**

---

## Aperçu

Cette boîte d'outils effectue l'analyse du suivi de bolus de fluorescence sur des séries chronologiques de données en microscopie biphotonique. Elle segmente les régions d'intérêt (ROI) vasculaires à partir des projections d'intensité maximale (MIP), extrait les cinétiques de fluorescence de chaque ROI, et effectue l'ajustement de courbe de fonctions Gamma sur le tracé de passage du bolus pour en dériver les paramètres hémodynamiques.

**Scripts drawROI.m et BolusTrack.m originaux :** Paolo Bazzigaluppi (Janvier 2019)  
**Modifications :** Adrienne Dorr (Avril 2026)

---

## Fichiers

| Fichier | Objectif |
|------|---------|
| `drawROI.m` | Dessiner et enregistrer les ROI polygonales sur une image MIP |
| `ApplyRegistrationToMask.m` | Appliquer les transformations affines (translation + rotation) aux fichiers maskObj existants |
| `GlobalShiftMask.m` | Outil graphique pour appliquer un décalage uniforme en pixels XY à toutes les ROI et enregistrer un nouveau masque |
| `BolusTrack_InteractiveEdit.m` | Interface graphique d'ajustement de bolus avec éditeur de ROI, chargement de métadonnées, débruitage et sauvegarde automatique |
| `BolusTrack.m` (original) | Interface graphique originale de Paolo (conservée comme sauvegarde) |

---

## Flux de travail A) Nouvelles données

1. Recaler les deux fichiers TIFF de bolus sur la pile XYZ. Génère des TIFF décalés et des fichiers de transformation.
2. Générer une MIP à partir d'un bolus recalé (ImageJ). Alterner le bolus de référence selon les sujets.
3. Dessiner les ROI sur la MIP (`drawROI.m`). Enregistrer le fichier `maskObj`.
4. Ajuster le bolus sur lequel les ROI ont été dessinées (`BolusTrack.m`) — charger le TIFF recalé, charger les métadonnées, importer le `maskObj` d'origine. Les ROI doivent correspondre. Tracer les signaux, ajuster et exporter.
5. Décaler les ROI pour le bolus apparié (`GlobalShiftMask.m`) — charger le TIFF apparié recalé, importer le `maskObj` d'origine, appliquer le décalage XY, vérifier et enregistrer le `maskObj` décalé.
6. Ajuster le bolus apparié (`BolusTrack.m`) — importer le `maskObj` décalé obtenu à l'étape 5. Si des ROI individuelles nécessitent encore des ajustements (dérive en Z, gonflement), ajuster localement avant l'extraction du signal.

## Flux de travail B) Correction d'anciens fichiers maskObj

1. Recaler les deux fichiers TIFF de bolus sur la pile XYZ.
2. Transformer l'ancien `maskObj` pour correspondre à l'image recalée (`ApplyRegistrationToMask.m`) — sélectionner le `maskObj`, sélectionner le fichier `.mat` de décalage correspondant, vérifier et enregistrer. Gère la transformation affine complète, y compris la rotation.
3. Si les ROI transformées ne s'alignent pas parfaitement avec le bolus apparié, appliquer une correction XY supplémentaire (`GlobalShiftMask.m`).
4. Ajuster (`BolusTrack.m`) — importer le `maskObj` transformé/décalé, affiner par vaisseau si nécessaire, extraire les tracés, ajuster les courbes et exporter.

## Flux de travail C) Test sans recalage d'image

1. Travailler directement avec les TIFF originaux non recalés et recadrés.
2. Dessiner les ROI sur la MIP d'un bolus (`drawROI.m`).
3. Ajuster ce bolus (`BolusTrack.m`) avec le `maskObj` d'origine.
4. Décaler les ROI pour le bolus apparié (`GlobalShiftMask.m`).
5. Ajuster le bolus apparié (`BolusTrack.m`). Ajuster finement les positions au besoin.

---

## Documentation des fichiers de script

### drawROI.m

**Syntaxe :**
```matlab
maskObj = drawROI(img, vnum, stype)
```

| Paramètre | Description |
|-----------|-------------|
| `img` | Matrice de l'image MIP |
| `vnum` | Nombre de ROI (généralement 50 à 70) |
| `stype` | `0` = zoom unique, dessiner tout ; `1` = zoom réappliqué avant chaque ROI |

**Enregistrement :**
```matlab
save('MAX_4755_bolus3_maskObj.mat', 'maskObj')
```

**Taille des ROI :** Segmentez la totalité de la lumière visible du vaisseau en vous arrêtant à 1 ou 2 pixels du bord apparent. Les ROI plus larges offrent un meilleur rapport signal sur bruit (RSB).

---

### ApplyRegistrationToMask.m

Applique la transformation affine complète (translation + rotation) à un fichier `maskObj` existant.

**Syntaxe :**
```matlab
ApplyRegistrationToMask
```

1. Sélectionner le `maskObj`, 2. Sélectionner le fichier `.mat` de décalage, 3. Vérifier sur le TIFF recalé, 4. Enregistrer.

Voir `README_ApplyRegistrationToMask_FR.md` pour plus de détails.

---

### GlobalShiftMask.m

Décalage XY uniforme en pixels pour toutes les ROI. Utilisé pour transférer des ROI entre paires de bolus dans le même espace de coordonnées recalé.

**Syntaxe :**
```matlab
GlobalShiftMask
```

| Bouton | Fonction |
|--------|----------|
| Load Data | Charger le TIFF du bolus, afficher la MIP |
| Import ROIs | Superposer le `maskObj` sur la MIP |
| Pop-out View | Saisir le décalage XY, vérifier visuellement |
| Save Mask | Enregistrer le `maskObj` décalé |

---

### BolusTrack_InteractiveEdit.m

Interface graphique complète de suivi de bolus. Renommez le fichier en `BolusTrack.m` pour un usage quotidien.

**Syntaxe :**
```matlab
BolusTrack
```

**Boutons du panneau gauche :**

| Bouton | Fonction |
|--------|----------|
| Load Data | Charger le TIFF recadré du bolus |
| Load Metadata | Extraire automatiquement la fréquence d'acquisition depuis le fichier `.txt` |
| Frame Rate | Saisie manuelle de la fréquence d'acquisition |
| Import ROIs | Charger le `maskObj` |
| Pop-out View | Éditeur interactif de ROI en taille réelle |
| Save ROIs | Enregistrer les positions ajustées sous un nouveau `maskObj` avant l'ajustement |
| Show ROIs tc | Extraire les tracés bruts, afficher le premier tracé |
| Denoise (SD) | Définir le seuil des valeurs aberrantes (par défaut 2.0) |
| Apply Denoise | Appliquer le débruitage au tracé actuel |
| Toggle Raw | Basculer entre l'affichage brut et débruité |
| Resume Session | Restaurer la progression d'une session interrompue via `autosave_progress.mat` |

**Charger les métadonnées :** Utilisez le fichier métadonnées `.txt` ORIGINAL non tronqué de Fluoview. La fréquence d'acquisition ne change pas avec le recadrage temporel de l'image.

**Enregistrer les ROI :** Enregistre les positions actuelles des ROI (après ajustements) sous un nouveau fichier `maskObj.mat`. Utilisez cette option APRÈS avoir finalisé la position des ROI et AVANT de cliquer sur Show ROIs tc. Cela vous évite de perdre votre travail d'ajustement géométrique en cas d'interruption du processus d'ajustement.

**Sauvegarde automatique :** La progression est automatiquement sauvegardée toutes les 5 courbes ajustées dans le fichier `autosave_progress.mat` du répertoire de travail. Après un export réussi, ce fichier est supprimé.

**Restaurer une session après une interruption :**

1. Relancer `BolusTrack`.
2. Charger les données (TIFF du bolus).
3. Charger les métadonnées (ou ajuster la fréquence manuellement).
4. Importer les ROI.
5. Cliquer sur `Show ROIs tc` pour extraire les signaux.
6. Cliquer sur `Resume Session` et sélectionner `autosave_progress.mat` : les ajustements terminés sont rechargés et l'affichage bascule sur le premier tracé non ajusté.

*Note : le script `gammaFun.m` doit être présent dans le chemin de recherche MATLAB. Utilisez `which gammaFun` pour valider.*

**Débruitage — Approche recommandée :**

Chaque nouveau tracé s'affiche par défaut en mode BRUT. Tentez toujours d'ajuster le tracé brut en premier lieu. Le solveur Gamma utilise une pondération de Cauchy qui diminue naturellement l'impact des points aberrants. N'appliquez le débruitage que si le signal brut empêche de définir convenablement les estimations initiales ou si le solveur diverge.

1. Inspecter le signal brut. Si le début, le pic et le retour à la base sont visibles, ajustez directement.
2. Si le signal est trop bruité, cliquez sur `Apply Denoise` au seuil par défaut de 2.0 SD.
3. Si le seuil de 2.0 SD is insuffisant, diminuez-le progressivement (jusqu'à 1.5). Évitez de descendre sous 1.5 pour ne pas écrêter la rampe de montée du bolus.
4. Utilisez `Toggle Raw` pour comparer les signaux.
5. La version affichée au moment où vous lancez l'ajustement correspond à celle qui sera ajustée et enregistrée.

Le logiciel indique si le tracé a été ajusté sur le signal brut ou débruité (ainsi que le seuil de SD utilisé) dans le champ `fitOut(n).fittedOn`.

---

## Classification des types de vaisseaux

| Code | Type | Critères géométriques |
|------|------|----------|
| A | Artériole | Pénétrante ; < 5 ramifications ; ramification profonde (> 400 um) ; ramifications à angle droit |
| V | Veinule | Pénétrante ; > 5 ramifications ; ramification superficielle ; angles obtus ; plus large en surface |
| C | Capillaire | Vaisseau de surface non pénétrant |
| U | Inconnu | Pénétrant, type indéterminé |

---

## Dépannage

**Les ROI ne correspondent pas au bolus apparié :** Utilisez `GlobalShiftMask` pour corriger globalement, puis l'éditeur pour les corrections locales par vaisseau.

**Les anciennes ROI ne correspondent pas aux images recalées :** Utilisez `ApplyRegistrationToMask` pour appliquer la transformation affine complète.

**Échec de convergence de la courbe Gamma :** Ajustez les paramètres initiaux. Avancez légèrement le temps au pic estimé ou reculez le début de l'ajustement.

**Erreur sur la fonction "gammaFun" :** Le script `gammaFun.m` n'est pas présent dans les chemins de recherche. Ajoutez le dossier avec `addpath('/chemin/vers/dossier')`.le temps de pic estimé ou reculez le début de l'ajustement.

**Erreur sur la fonction "gammaFun" :** Le script `gammaFun.m` n'est pas présent dans les chemins de recherche. Ajoutez le dossier avec `addpath('/chemin/vers/dossier')`.
