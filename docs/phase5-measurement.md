# Phase 5: real-world measurement

Goal from the roadmap: repeatable, measurable feedback reduction across several conditions,
without audio-thread instability.

How it works: you record the **dry mic signal**, the exact input the plug-in receives, while
the plug-in runs live in the mic-to-speaker loop. The suppressor is deterministic, so
`scripts\measure.ps1` replays each recording through the same engine and reproduces what the
plug-in did, block by block. With the plug-in on, the feedback in your recording is what was
left after its notches acted in the room, so the drop after each detection is a real
measurement of the suppression. You only label when you heard feedback; the script measures the rest.

## 1. Setup

- [ ] **Build and install** the plug-in:
  ```powershell
  .\scripts\build.ps1 -Install
  ```
  This installs to `%LOCALAPPDATA%\Programs\Common\VST3`. If your host doesn't scan that
  folder, add it to the host's VST3 paths or copy the `.vst3` folder to
  `C:\Program Files\Common Files\VST3` (needs admin).
- [ ] **Host:** use a DAW that records a track's input before its effects (REAPER does by default:
  record mode "Record: input"). The mic track holds the plug-in and is armed with monitoring
  on, so the plug-in's output goes to the speaker.
- [ ] **Signal chain:** mic → interface → track (mono) → **Feedback From Whence You Came as
  the first effect**, with nothing before it → speaker. No input gain or EQ inside the DAW
  before the plug-in: the replay must see what the plug-in saw.
- [ ] **Loop gain control:** turn the gain up and down with something **after** the plug-in
  whose level you can read in dB: the track fader or a gain plug-in after it. Interface
  preamp gain also works for the takes, but it can't be read off reliably for the stable-gain
  test.
- [ ] **Sample rate:** 48 kHz (repeat a few takes at 44.1 kHz if you can). Record as WAV.
- [ ] **Safety:** start every take with the gain low. A brickwall limiter on the master bus
  (ceiling around -6 dBFS) protects ears and tweeters; it only acts on loud howls. Keep a hand
  on the fader.
- [ ] **Latency:** check that the host reports 0 samples of latency (plug-in delay
  compensation) for the plug-in.

Don't switch bypass in the middle of a take: the replay can't know. Record bypassed and
enabled takes as separate files.

## 2. Takes to record

Each take is 30–90 s. Note each one in `measurements\sessions.csv` as you go (see section 3).

| | Take | Plug-in | Kind | Repeat at |
| --- | --- | --- | --- | --- |
| [ ] | **A. Baseline:** raise the gain until feedback starts, let it ring about 2 s, pull back. Three times. | bypassed | feedback | |
| [ ] | **B. Same with the plug-in on:** raise the gain past where A fed back. | on | feedback | buffers 64, 256, 1024 |
| [ ] | **C. Mic pointed at the speaker:** sing or talk at a normal gain, point the mic at the speaker for about 3 s, then away. Three times. | on | mixed | buffers 64, 256 |
| [ ] | **D. Your common case:** sing while feedback grows into it. | on | mixed | buffers 64, 256, 1024 |
| [ ] | **E. Singing without feedback:** gain low enough that nothing rings. Held notes (straight and with vibrato), low notes (100–250 Hz), high belts, crescendos, fast passages, whistling. About 3 minutes in total. | on | vocal | |
| [ ] | **F. Singing over a backing track** playing through the speakers at karaoke volume, with no feedback. | on | vocal | |
| [ ] | **G. Positions:** repeat B at 1 m and 2 m in front of the speaker, beside it, and behind it. | on | feedback | |
| [ ] | **H. Another room**, if you have one: repeat B and D. | on | as above | |

## 3. Label each take

Open the WAV in an editor (REAPER, Audacity) and add a row to `measurements\sessions.csv`:

```
file,plugin,kind,feedback,buffer,position,room,notes
takes/b-64-1.wav,on,feedback,3.2-5.0;11.8-12.6,64,1 m on axis,living room,
takes/e-held.wav,on,vocal,,64,normal,living room,held notes + vibrato
```

- `file`: path relative to the `measurements` folder.
- `plugin`: `on`, `bypassed`, or `none` (recorded without the plug-in in the loop).
- `kind`:
  - `vocal`: no feedback in the take; every detection counts as false.
  - `feedback` or `mixed`: list the feedback windows in `feedback`; detections outside them count as false.
- `feedback`: windows in seconds, `start-end`, separated by `;`. Each window runs from when the
  ring first becomes audible to when it's gone.
- `buffer`: the host buffer size in samples. The replay uses the same block size, so it matches
  the live processing exactly when the host delivers fixed-size blocks.

The manifest already holds your three earlier recordings (`plugin` = `none`) as a reference.

## 4. Maximum stable gain

For each position, fill in a row of `measurements\stable-gain.csv`:

1. [ ] Plug-in bypassed. Raise the post-plug-in gain in 1 dB steps, waiting about 5 s at each.
   Note the highest setting that doesn't ring or howl → `bypassed_gain_db`.
2. [ ] Plug-in on. Same again, giving it about 10 s at each step to settle → `enabled_gain_db`.

```
condition,bypassed_gain_db,enabled_gain_db,buffer,notes
1 m on axis,-12,-4,64,
```

## 5. CPU and latency

- [ ] Note the host's CPU or performance meter for the plug-in at buffer 64 (in `notes`, or
  just tell me). The report adds an offline benchmark from this machine.
- [ ] **Null test** (proves zero latency and transparency): put a take with no feedback
  (take E) on two tracks, add the plug-in to one, flip the polarity of the other, and render
  the sum. It should be silent (-inf) as long as the analyser shows no detections for that
  take.

## 6. Run the report

```powershell
.\scripts\measure.ps1
```

This writes:
- `build\measurements\report.md`: per-take table, overall figures grouped by plug-in state and by
  buffer size, stable gain, and CPU.
- `build\measurements\episodes.csv`: every detection, classified as true or false.
- For each take: `<take>.log` (the analyser's event log) and `<take>.processed.wav`.

## 7. Targets

| Measure | Target |
| --- | --- |
| Missed feedback windows | 0 |
| Labelled onset to detection (plug-in on) | median ≤ 250 ms |
| Level at detection | median ≤ -30 dBFS |
| Detection to 10 dB drop (plug-in on) | median ≤ 200 ms |
| False detections on vocal takes | ≤ 1 per 5 minutes; the emergency gain never engages |
| Maximum stable gain improvement | median ≥ 3 dB (simulations give 7–9 dB) |
| CPU | < 1% of a core at buffer 64 |
| Latency | 0 samples reported; null test silent |
| Buffer sizes | medians within 20% of each other |

Send me the report and the takes behind any false or missed detection. Those are what the
next round of tuning needs.
