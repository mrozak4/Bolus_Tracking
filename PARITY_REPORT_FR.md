# Rapport de parité et de performance des implémentations Python et C++

**[English](PARITY_REPORT.md) | [Français (Québec)](PARITY_REPORT_FR.md)**

---

Ce rapport compare les implémentations Python et C++ de la chaîne de traitement de suivi de bolus, en analysant la performance, la parité numérique et les compromis de conception.

## 1. Analyse comparative des performances

L'analyse comparative suivante a été exécutée à l'aide du jeu de données **Sample Subject 2259**, composé de **6 fichiers TIFF** (chacun comprenant 300 trames d'images $512 \times 512$ et 70 ROI distinctes cartographiées).

| Phase / Métrique | Chaîne de traitement Python | Chaîne de traitement C++ | Accélération / Différence |
| :--- | :--- | :--- | :--- |
| **Configuration Docker / Compilation de l'image** (Unique) | ~4,3 secondes | ~3,8 secondes (Mise en cache)<br>~45,0 secondes (Compilation complète) | Configuration initiale uniquement |
| **Calcul numérique** (Ajustement numérique) | ~63,71 secondes | ~5,57 secondes | **~11,4x plus rapide** |
| **Temps d'exécution total** (Lancement Docker + Entrées/Sorties) | ~63,85 secondes | ~33,32 secondes | **~1,9x plus rapide** |

### Détail de l'analyse :
* **Configuration Docker (Compilation unique)** : L'étape initiale de construction du conteneur Docker. Pour le C++, cela inclut la configuration de CMake et la compilation du code (`make`). Pour Python, cela installe les dépendances requises (`matplotlib`).
* **Temps d'exécution (Lancement du conteneur)** : Le temps système associé au lancement de Docker, au montage des dossiers locaux pour le partage des données et à l'exécution de l'analyse. La parallélisation en C++ accélère grandement la charge de calcul, bien que le montage de disque ajoute un délai fixe.

### Pourquoi le C++ est plus rapide :
1. **Exécution en parallèle** : Le C++ implémente l'exécution multithread via `std::async` pour exploiter tous les cœurs de processeur disponibles lors de l'ajustement simultané des ROI. Python est limité par le verrou global de l'interpréteur (GIL).
2. **Exécution compilée** : Les calculs principaux (interpolation spline, opérations matricielles via Eigen et optimisation des paramètres) sont compilés directement en code machine natif.
3. **Absence de surcharge d'interpréteur** : Élimine le délai de démarrage et d'exécution associé à la machine virtuelle de l'interpréteur Python.

---

## 2. Analyse de parité numérique

Les deux chaînes de traitement affichent un accord numérique exact (dans les limites de la précision en virgule flottante) tant pour la première passe automatisée que pour la passe de secours basée sur la loi de population a priori :

- **Alignement algorithmique exact** :
  - **Correction de la copie de foncteur (C++)** : Dans la passe robuste de Cauchy en C++, le solveur de Levenberg-Marquardt d'Eigen et son foncteur sont maintenant correctement réinstanciés, garantissant la propagation correcte des poids robustes.
  - **Correction du bogue de passe unique (Python)** : La chaîne de traitement Python a été corrigée pour s'assurer que les contraintes personnalisées ne forcent pas inconditionnellement `single_pass = True`. La seconde passe de secours est désormais correctement déclenchée pour les ROI bruitées, ramenant les sorties Python et C++ à une parité parfaite.
- **Accord des paramètres** : Toutes les métriques calculées (amplitude, temps au pic, largeur à mi-hauteur, ligne de base, RSB, RCB, AUC, OnT, OnTSc et bornes d'intervalles de confiance du temps de transit) sont pleinement alignées.
- **Parité du débruitage et des splines** : Le débruitage (filtre gaussien 1D) et l'interpolation par spline cubique sont mathématiquement identiques et fournissent les mêmes données d'entrée aux deux moteurs de calcul.

---

## 3. Recommandation

### Nous recommandons l'**implémentation C++** pour la production, l'analyse par lots et les révisions (GUI)
Pour la recherche quotidienne, les études de cohortes et le triage de révision manuelle, l'**écosystème C++** est vivement recommandé :
* **Efficacité temporelle** : La modélisation parallèle réduit les temps d'analyse des cohortes de plusieurs heures à quelques secondes.
* **Interface graphique intégrée** : L'application moderne Dear ImGui (`bolus_tracking_gui`) permet aux opérateurs de réviser, filtrer (PASS/WARN/FAIL), réajuster manuellement (marqueurs de début, pic et fin de bolus) et rogner/zoomer les fenêtres d'ajustement au sein d'une même plateforme.
* **Exécutable unique / Grande portabilité** : Peut être compilé et exécuté sur Windows, Linux et macOS sans se soucier des conflits de versions de packages Python.

### Nous recommandons l'**implémentation Python** pour le prototypage et l'extension des scripts
* **Prototypage rapide** : Si vous expérimentez de nouvelles techniques de traitement de signal, des fonctions de perte personnalisées (par exemple, perte de Huber vs Cauchy) ou de nouveaux types d'affichage, l'environnement Python est rapide et ne nécessite pas de compilation.
* **Personnalisation Matplotlib** : Utile si vous devez construire des scripts hautement personnalisés pour générer des figures spécifiques ou pour intégrer vos résultats à d'autres pipelines de science des données.
