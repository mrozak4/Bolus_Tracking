# Recalage géométrique des masques (ApplyRegistrationToMask) — Guide d'utilisation

**[English](README_ApplyRegistrationToMask.md) | [Français (Québec)](README_ApplyRegistrationToMask_FR.md)**

---

## Aperçu

`matlab/src/ApplyRegistrationToMask.m` est une interface graphique MATLAB permettant d'appliquer les transformations géométriques de recalage de Visual Studio aux fichiers existants de ROI `maskObj`. 

Lorsque des ROI sont tracées sur une image de bolus non recalée puis que ce bolus est recalé sur la pile volumétrique XYZ, les ROI ne correspondent plus à l'image recalée. Cet outil applique aux sommets des polygones de ROI la même transformation affine que celle appliquée à l'image, afin que les ROI suivent fidèlement les vaisseaux.

## Pourquoi ne pas simplement utiliser GlobalShiftMask ?

`GlobalShiftMask` applique un décalage XY uniforme en pixels. Cependant, le recalage de Visual Studio peut inclure une composante de rotation en plus de la translation. 

Par exemple, un bolus de votre jeu de données présente un angle de rotation de 0,44 degré. Aux limites extérieures de l'image, cela produit un écart d'environ 2 pixels en plus de la translation. Pour des vaisseaux fins de 3 à 5 pixels de large, négliger la rotation entraînerait un désalignement important des ROI en périphérie de l'image. `ApplyRegistrationToMask` gère la transformation affine complète.

## Format de la transformation

Les fichiers de recalage issus de Visual Studio (par exemple, `bolus1_shift.mat`) contiennent :

- `AffineTransform_float_2_2` : un vecteur 6x1 `[a00, a01, a10, a11, tx, ty]` représentant la matrice 2x2 de rotation/échelle et le vecteur de translation 2D.
- `fixed` : un vecteur 2x1 spécifiant le point central (pivot) de la transformation.

La transformation appliquée à chaque sommet de ROI est :

```
sortie = A * (entree - centre) + centre + translation
```

où `A` est la matrice 2x2 et `translation` correspond à `[tx, ty]`.

## Utilisation

1. Saisissez `ApplyRegistrationToMask` dans la fenêtre de commande de MATLAB.
2. Sélectionnez le fichier `maskObj.mat` contenant les ROI à transformer.
3. Sélectionnez le fichier de décalage de Visual Studio correspondant (ex : `bolus1_shift.mat`) — il doit s'agir de la transformation correspondant au bolus sur lequel les ROI ont été dessinées initialement.
4. L'outil affiche les valeurs de la transformation détectée (translation, angle de rotation, pivot).
5. Vous pouvez facultativement charger l'image TIFF ou la MIP recalée pour vérifier visuellement le positionnement.
6. Cliquez sur **Save Transformed Mask** pour enregistrer.

## Sorties

Un fichier `.mat` contenant une structure `maskObj` avec des champs `.Position` mis à jour. Le nom de fichier par défaut est `nomdorigine_registered_shiftnomdefichier.mat`. Ce fichier est entièrement compatible avec les outils `BolusTrack` et `GlobalShiftMask`.

## Intégration dans le flux de travail

Cet outil intervient lors de la correction géométrique des masques de données existants (Flux B) entre l'alignement des images et le traitement cinétique :

```
Recaler les TIFF de bolus sur la pile XYZ (Visual Studio)
                           |
matlab/src/ApplyRegistrationToMask.m — transformer l'ancien masque pour correspondre à l'image
                           |
(Optionnel) matlab/src/GlobalShiftMask.m — appliquer une correction XY fine
                           |
matlab/src/BolusTrack_InteractiveEdit.m — importer les masques, modéliser, exporter
```

## Remarques importantes

- Appliquez toujours le fichier de recalage correspondant au bolus d'origine sur lequel les ROI ont été dessinées (pas le bolus apparié). L'objectif est de déplacer les ROI vers l'espace recalé propre à cette image.
- Pour le bolus apparié (où aucun tracé n'a été fait initialement), appliquez d'abord ce recalage affine, puis utilisez `GlobalShiftMask` pour superposer le décalage XY requis pour correspondre à la géométrie de l'autre bolus.
- Si l'angle de rotation est négligeable (< 0,01 degré), l'outil le signale. Dans ce cas particulier, l'usage de `GlobalShiftMask` seul aurait été suffisant.
- L'outil prend en charge tous les formats de `maskObj` existants.
