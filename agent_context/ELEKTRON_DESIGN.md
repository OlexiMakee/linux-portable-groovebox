# Elektron Workflow & Design Principles for XU10 Groovebox

## 1. Core Philosophy
Elektron machines (Digitakt, Digitone, Syntakt, Octatrack, Analog Rytm/Four) are celebrated for their rapid, muscle-memory-driven workflow, high information density, and deep sequencing capabilities ("parameter locks" and "trig conditions").

The key design pillars:
1. **Screen + Encoders (or D-Pad/Sticks):**
   - The screen shows 6 to 8 parameters at a time organized into logical pages.
   - UI elements always display parameter name and current value simultaneously.
   - Instant visual feedback on every encoder/stick movement.

2. **Parameter Pages (The Page Grid):**
   - Sound design is strictly partitioned into standardized functional pages:
     - **SRC (Source / Engine):** Basic oscillator shapes, pitch, detune, FM index/ratio, sample select.
     - **FLTR (Filter):** Cutoff frequency, resonance, filter type (LPF/HPF/BPF), envelope depth, keytracking.
     - **AMP (Amplifier & Envelope):** Attack, Decay, Sustain, Release, Volume, Pan, Overdrive/Drive.
     - **LFO / MOD (Modulation):** Speed, Depth, Waveform, Start Phase, Destination, Fade in/out.
     - **FX (Sends & Master):** Delay Send, Reverb Send, Chorus Send, Master Filter/Distortion.

3. **Trig Conditions & Probabilities:**
   - Instead of static 16-step loops, steps can evolve algorithmically over bars:
     - **Probability (`%`):** Evaluated each time the playhead hits the step (e.g. 75% chance to trigger).
     - **Iteration Conditions (`A:B`):** Step fires only on iteration A of every B loops (e.g. `1:2` fires only on 1st loop of 2; `2:2` fires on 2nd; `1:4`, `2:4`, `3:4`, `4:4` for fills and evolving variations).
     - **Fill & !Fill (`FILL`, `!FIL`):** Fires only when temporary "Fill" mode is held, allowing live breakdown/fill performance.
     - **Neighbor / Previous (`PRE`, `!PRE`):** Dependent on whether the previous step on the same track actually fired (creates conditional chains).

4. **Parameter Locks (P-Locks):**
   - Holding down a step button (or editing while a step is selected in Grid Record mode) allows altering any synthesis parameter specifically for that step.
   - When the step finishes, parameters smoothly or instantly return to the track's base sound.

5. **Live Performance & Navigation Ergonomics:**
   - Dedicated, predictable button functions per page.
   - No deep multi-level menus during live jamming.
   - Quick mute, quick jump between tracks, instant pattern reload/save.
