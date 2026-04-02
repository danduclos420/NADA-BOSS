# NADA BOSS — Vocal Suite v1.0
## Elite Vocal Channel Strip avec IA intégrée

---

## STRUCTURE DU PROJET

```
NADA-BOSS/
├── CMakeLists.txt
└── Source/
    ├── ai/
    │   └── AIEngine.h          ← Analyseur spectral + AI Mixer
    ├── dsp/
    │   ├── PluginProcessor.h   ← Tous les DSP + header processor
    │   └── PluginProcessor.cpp ← Logique audio corrigée
    └── ui/
        ├── PluginEditor.h      ← UI header
        └── PluginEditor.cpp    ← UI implementation
```

---

## CHAÎNE DE TRAITEMENT (ordre corrigé)

```
Input
  ↓  [1] Capture AI (buffer ring pour analyse)
  ↓  [2] Pitch Shifter (phase vocoder)
  ↓  [3] EQ 6 bandes (Pro-Q 3 style)
  ↓  [4] Pultec EQ (low shelf + high shelf)
  ↓  [5] SSL Channel EQ (coloration)
  ↓  [6] De-Esser 902 (avant compression!)
  ↓  [7] FET 1176 (comp rapide, transitoires)
  ↓  [8] LA-2A Opto (comp lent, glue)
  ↓  [9] HG-2 Saturateur (chaleur harmonique)
  ↓  [10] R-VOX (comp vocal final)
  ↓  [11] Stereo Width (M/S)
  ↓  [12] Reverb BUS PARALLÈLE (dry préservé)
  ↓  [13] Delay BUS PARALLÈLE (dry préservé)
  ↓  [14] Limiter (toujours en dernier)
Output
```

---

## COMPILATION — WINDOWS

### Prérequis
- Visual Studio 2022 (avec C++ Desktop Development)
- CMake 3.22+
- JUCE installé (ex: C:\JUCE)

### Étapes

```bash
# 1. Clone le projet
git clone https://github.com/danduclos420/NADA-BOSS.git
cd NADA-BOSS

# 2. Ouvre CMakeLists.txt et change le chemin JUCE
# set(JUCE_PATH "C:/JUCE")   ← Ton chemin JUCE

# 3. Génère le projet Visual Studio
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# 4. Compile
cmake --build . --config Release

# 5. Le .vst3 est automatiquement copié dans:
# C:\Program Files\Common Files\VST3\NADA_BOSS.vst3
```

---

## TESTER DANS REAPER

1. Ouvrir Reaper
2. Options → Preferences → Plug-ins → VST
3. Cliquer "Re-scan" ou "Add" → pointer vers `C:\Program Files\Common Files\VST3`
4. Créer une piste audio
5. Enregistrer ta voix (ou importer un fichier)
6. Cliquer sur le bouton FX de la piste
7. Chercher "NADA BOSS" dans la liste
8. Double-cliquer pour insérer

### Test de base
1. Joue ta piste vocale
2. Vérifier que le son passe (pas de bruit)
3. Bouger le knob "THRESH" (FET 1176) — tu dois entendre la compression
4. Cliquer "NADA ANALYZE" — l'IA analyse et ajuste automatiquement

---

## BUGS CORRIGÉS vs VERSION ORIGINALE

| Bug | Original | Corrigé |
|-----|----------|---------|
| Ordre DSP | Reverb avant compresseur | Reverb en parallèle à la fin |
| Delay | Feedback immédiat (même buffer) | Bus parallèle propre |
| updateDSPChain | Appelé 44100x/sec | Seulement si paramètre change |
| ONNX dans audio thread | Crash / bruit | Tourne sur timer thread |
| Smoothing | Aucun → clicks | SmoothedValue sur tous les params |
| De-esser ordre | Après compression | Avant compression |

---

## ONNX (OPTIONNEL — pour plus tard)

Pour activer le vrai modèle ML:
1. Télécharger ONNX Runtime: https://github.com/microsoft/onnxruntime/releases
2. Extraire dans C:\onnxruntime\
3. Dans CMakeLists.txt, changer: `option(USE_ONNX "Enable ONNX Runtime for AI" ON)`
4. Recompiler

---

## PROCHAINES ÉTAPES

- [ ] Entraîner un modèle ML sur tes propres sessions
- [ ] Ajouter le plugin INSTRU (analyse spectrale pour sidechain IA)
- [ ] Communication inter-plugins via SharedMemory
- [ ] Design UI custom (quand tu seras prêt avec Figma)
- [ ] Sync BPM pour le delay
- [ ] Autotune automatique (détection pitch YIN)
