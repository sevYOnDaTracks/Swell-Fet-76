# Swell FET 76 0.1.0

Limiteur FET, façade noire en aluminium brossé. Commandes : Input, Output, Attack (1–7), Release (1–7), Ratio 4 / 8 / 12 / 20 / All (Maj + clic), Meter GR / +8 / +4 / Off, Mix, Power, suréchantillonnage Off / 2x / 4x.

Fabricant : Swell-audio. JUCE 9.0.5. Formats : VST3 et Standalone (Windows), plus AU sur Mac.

![Swell FET 76](docs/fet76.png)

## Moteur

Seuil fixe (Input pousse dans la compression), attaque 20–800 µs, relâchement 50 ms–1,1 s en partie dépendant du programme, genou doux par ratio, mode « All » à courbe dépassante (la sortie baisse quand l'entrée monte), distorsion paire du FET proportionnelle à la réduction de gain, étage de sortie classe A asymétrique.

Commun : suréchantillonnage demi-bande IIR polyphase (latence entière compensée, changement sans allocation),
mix parallèle aligné sur la latence, bypass progressif par l'interrupteur Power, détection stéréo couplée, lissage des paramètres.

## Presets

Presets d'usine classés Vocals / Drums / Bass / Instruments / Bus, aussi visibles comme « programmes » dans l'hôte.
Afficheur rétroéclairé sur la façade : ‹ › précédent / suivant, clic sur le nom = menu complet.
Presets perso (`.swellpreset`) dans `Documents/Swell Audio/Swell FET 76 Presets`. Un preset ne change que le son
(Power, suréchantillonnage et sélecteur du vu-mètre restent en place).

## Licence Swell

Module commun `Source/Licence` (copie identique à celle des autres plugins Swell). Slug boutique : **`swell-fet76`**
(`Source/LicenceConfig.h`). Sans licence : silence et écran d'activation par-dessus la façade.

## Interface « matériel réel » sans images

Rien n'est importé (ni PNG, ni SVG, ni filmstrip) : tout est calculé par le code.

- **Textures procédurales** : aluminium brossé (stries floutées de façon anisotrope + reflet en bande), peinture grise marbrée,
  grain de papier et de plastique. Calculées une fois au démarrage.
- **Lumière fixe** comme sur une photo : les boutons tournent mais leurs reflets restent en place ; les cannelures
  s'éclairent selon leur orientation face à la lampe.
- **Ombres douces** (portées et de contact), verre du vu-mètre avec reflets, carter du galvanomètre, vis, rondelles,
  rails de rack visibles dans les fentes, inverseurs chromés, voyant à cabochon taillé avec halo.
- **Aiguille VU physique** : galvanomètre masse-ressort amorti (≈ 300 ms, ≈ 1,5 % de dépassement), butées avec rebond,
  animée à la fréquence de l'écran (VBlank). L'ombre de l'aiguille se déplace sur le cadran.
- **Performance** : tout ce qui est fixe est rendu une fois en cache à la résolution physique de l'écran (Retina compris) ;
  seules les pièces mobiles sont redessinées. Interface redimensionnable de 50 % à 200 %, toujours nette, taille gardée dans la session.

## Compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Pour compiler hors ligne avec une copie locale de JUCE : `-DFETCHCONTENT_SOURCE_DIR_JUCE=deps/juce`.

Outils : `TestFET76` (stabilité, compression, transparence à Mix 0 et en bypass, 3 facteurs de suréchantillonnage,
réglages extrêmes, état, silence sans licence, chargement des presets d'usine, presets perso) et `SnapshotFET76 <sortie.png> [échelle] [locked]` (capture de l'interface, `locked` = écran d'activation).

Licence JUCE : JUCE est sous AGPLv3 ou licence commerciale ; vendre ce plugin en code fermé demande une licence JUCE commerciale.
Nom et façade propres à Swell : aucune marque, aucun logo ni aucune sérigraphie d'un fabricant existant.
