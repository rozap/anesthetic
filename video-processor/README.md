# racecut

Finds the parts of a long endurance-race video where racing is actually happening, and cuts them
into a highlight reel.

A stint is mostly empty track, and running YOLO over seven hours of 1080p to find the good bits
costs more than the footage is worth. So there are two passes:

| pass | what it touches | cost |
|---|---|---|
| analyse | a cropped, downscaled, low-frame-rate proxy decoded on the fly | ~17x realtime |
| cut | the untouched HD original, only the chosen ranges | seconds per cut |

Nothing is written between the passes except a small JSON of detections, so a 3-hour source is
analysed once and can then be recut at any length or ratio instantly.

## Install

Needs `ffmpeg` and `ffprobe` on `PATH`.

```sh
python3 -m venv .venv
.venv/bin/pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
.venv/bin/pip install ultralytics numpy
```

The CPU-only torch build is deliberate. On a machine without an NVIDIA card the CUDA build is a
multi-gigabyte download that never gets used.

## Use

```
racecut.py [options] input [input ...] min_cut target_len
```

The two lengths come last because the number of inputs is variable. Everything else is a named
option and can go anywhere.

| argument | meaning |
|---|---|
| `input` | one or more source videos, in the order they should appear |
| `min_cut` | no cut in the output is shorter than this, in seconds |
| `target_len` | approximate total length of the output, in seconds |

## Examples

A 10-minute reel from one stint, with no cut shorter than 25 seconds:

```sh
.venv/bin/python racecut.py sunday.mp4 25 600 -o highlights.mp4
```

A whole weekend on one budget. The inputs are laid end to end on a single timeline and compete
against each other, so a busy Sunday takes more of the 15 minutes than a quiet Saturday, but no
single cut ever spans two files:

```sh
.venv/bin/python racecut.py saturday.mp4 sunday.mp4 30 900
```

Mostly wheel-to-wheel fights rather than general traffic, with longer cuts so a fight is not cut
off mid-corner:

```sh
.venv/bin/python racecut.py sunday.mp4 40 600 --battle-ratio 0.8 --max-cut 180
```

See what it would pick without spending 10 minutes encoding, and get the per-window scores to plot
against the source timeline:

```sh
.venv/bin/python racecut.py sunday.mp4 25 600 --analyse-only --dump-scores scores.csv
```

Both filters off, for footage that is deliberately all pit lane, formation lap or caution:

```sh
.venv/bin/python racecut.py pitlane.mp4 15 120 --no-pit-filter --no-yellow-filter
```

A camera with no HUD and no visible bodywork. Use the whole frame, and turn off the three measures
that read the Garmin Catalyst overlay:

```sh
.venv/bin/python racecut.py gopro.mp4 25 600 \
    --roi-top 0 --roi-bottom 1.0 \
    --speed-weight 0 --delta-weight 0 --no-yellow-filter
```

Fast and rough on a 4-minute range to check the ROIs are right, then the same range for real. Each
run gets its own cache entry, because `--sample-fps` and `--model` both affect detection:

```sh
.venv/bin/python racecut.py sunday.mp4 20 120 --start 3600 --end 3840 --sample-fps 2 --analyse-only
.venv/bin/python racecut.py sunday.mp4 20 120 --start 3600 --end 3840 --model yolo11s.pt
```

Recutting the same analysis at a different length or ratio is instant, since neither flag affects
detection:

```sh
.venv/bin/python racecut.py sunday.mp4 25 600 -o short.mp4
.venv/bin/python racecut.py sunday.mp4 25 1800 --battle-ratio 0.0 -o long-traffic.mp4
```

## What counts as interesting

Two independent measures, mixed by `--battle-ratio`:

- **traffic** is weighted car-seconds packed into a window, which reads as the average number of
  cars on screen. Cars closer to the camera count for more.
- **battle** is the longest single car held continuously on screen across a window, bonused by how
  long that fight runs in total. A car you follow through six corners scores far above six
  different cars glimpsed once each.

`--battle-ratio 1.0` spends the whole output on battles, `0.0` on traffic, `0.5` (default) splits
it evenly. If one side cannot spend its share, the remainder rolls over to the other, unless you
asked for exactly 0.0 or 1.0.

Three further measures scale both. Each multiplies a window's score by `(1 - w) + w * x`, so `0`
ignores the measure and `1` makes the score scale fully with it.

| flag | default | x is |
|---|---|---|
| `--closeness-weight` | 0.3 | widest car box in the frame, over `--closeness-ref` (0.10 of frame width) |
| `--speed-weight` | 0.3 | speedometer reading, normalised across the session |
| `--delta-weight` | 0.2 | how close the delta timer is to the session best, saturating at `--delta-ref` (5.0s) |

Closeness and speed are measured per frame, which is the point. A 60-second battle that starts 100m
back and ends bumper-to-bumper has one mean box width across its whole length, so weighting by that
would rank the dull opening exactly like the close finish.

Closeness normalises against an absolute reference rather than session percentiles, so a stint with
no close racing scores low throughout instead of having its mildest moments stretched to fill the
range. Raise `--closeness-ref` if your camera is wide-angle and cars look small even alongside.

`--delta-weight` is low on purpose, because it pulls against the other two: time lost fighting a car
shows up as a worse delta, so a high setting drifts the reel toward clean-air laps with nobody
around. Its value is at low settings, where it demotes obviously off-pace footage. It also carries a
mild bias toward the start of a stint, since early laps almost all set a new session best.

## Filters

### The pit lane

`--min-car-width` and `--min-track-sec` reject parked cars at racing speed, but not in the pit lane,
where you crawl past the same cars slowly enough that they look like large, long-lived tracks. A pit
stop therefore scores as the heaviest traffic of the session, which is how the first build of this
tool put a 75-second pit stop in the reel.

The signal that separates them is people. YOLO is already running, so class 0 rides along in the
same forward pass at no cost.

| window | frames containing a person |
|---|---|
| pit stop at 0:48:33 | 29% |
| pit entry at 0:47:50 | 19% |
| four racing windows | 0% to 1% |

Acting on that flag raw would be wrong in both directions, so it goes through three steps. The
per-frame flag is smoothed over `--pit-smooth` (20s) into a density, so one corner worker cannot
veto 25 seconds of racing. Regions above `--pit-person-frac` (0.08) are marked as pits. Those are
widened by `--pit-dilate` (45s) either side, because the slow lap in and out is not racing either
and has no people visible for most of it. On the pit-stop range this rejects 111 of 116 candidate
windows; on a racing range it rejects none. `--no-pit-filter` turns it off.

Optical flow was tried first, on the theory that pit lane means low speed. It does not work: median
flow magnitude was 1.82 in the pits against 1.69 at racing speed, because in the pit lane you are
close to walls and trailers, so nearby objects sweep past quickly even at 15mph.

### Yellow flags

The lap counter across the top of the frame is not drawn during a full course yellow, so its absence
marks the caution laps and those windows are rejected. The same test drops the opening laps, where
the counter is absent because the Catalyst has not acquired yet.

Detection is a band of near-white pixels in `--lap-roi`. The two states are far apart, which is what
makes it work: the counter fills about 0.13 of the band, an empty banner about 0.007, all of it
antialiasing along the banner's own edge. `--lap-min-frac` (0.04) sits between them. The upper bound
matters as much as the lower one, because a missing banner shows sky, which is bright, and a naive
"is it bright" test would call that text. The flag is smoothed over `--yellow-smooth` (6s) before
`--yellow-frac` (0.4) is applied. `--no-yellow-filter` keeps everything.

## Shaping the cuts

`--pad-start` and `--pad-end` (both 1.0s) keep extra footage either side of the segment that
actually scored, which is what stops a cut opening mid-corner. They are independent, so a long
lead-in with a hard exit is `--pad-start 5 --pad-end 0`, and `--pad` sets both. Padding is charged
against the target length, so it shortens the scored cores rather than overrunning the reel.

`--min-gap` (2.0s) and `--bridge-gap` (3.0s) do different jobs. `--min-gap` applies during selection
and keeps two picks from landing on top of each other. `--bridge-gap` applies afterwards, to the
final padded ranges, and says that a jump that short is better kept than cut: same corner, same
cars, but the picture skips. Bridging also stops overlapping pads from putting the same second of
footage in twice, which is possible whenever `--min-gap` is smaller than the two pads together.
`--bridge-gap 0` disables it.

## Tuning for a different camera

The defaults assume a forward-facing dash cam with a Garmin Catalyst overlay: a banner across the
top, the car's own hood and gauges across the bottom.

| flag | default | why |
|---|---|---|
| `--roi-top` | 0.10 | crops off the lap/segment banner |
| `--roi-bottom` | 0.72 | crops off the hood and the speedometer |
| `--speed-roi` | 0,0.699,0.172,0.977 | where the speedometer is |
| `--lap-roi` | 0.28,0.010,0.42,0.075 | where the lap counter is |
| `--delta-roi` | 0.677,0.912,0.844,0.986 | where the delta timer is |
| `--max-car-width` | 0.35 | the ego car's own hood otherwise detects as a car in nearly every frame |
| `--min-car-width` | 0.026 | 50px on a 1920-wide source; paddock cars are smaller |

Cropping is not only about false positives. It removes about a third of every frame before
inference, which is a direct speedup.

`--min-car-width` and `--min-track-sec` (1.5s) together discard cars that are not part of the race.
Paddock cars parked alongside the track detect readily, but they are small and sweep out of frame in
under a second, so both filters reject them. On the test footage the paddock straight scores 0.00
while genuine traffic 40 seconds later scores 0.85.

Each of the three HUD readers disables itself with a warning if its region holds no usable signal,
so a camera without a Catalyst degrades to ignoring those measures rather than scoring noise.

## Reading the speedometer

Optical flow does not give a speed, so it is read off the Catalyst gauge, which draws a red arc that
grows with speed. Counting red pixels in a fixed region is very nearly linear in mph:

| gauge reads | red pixels | px per mph |
|---|---|---|
| 6 | 358 | 59.7 |
| 12 | 685 | 57.1 |
| 53 | 3034 | 57.2 |
| 64 | 3668 | 57.3 |
| 77 | 4433 | 57.6 |
| 89 | 5073 | 57.0 |
| 126 | 7215 | 57.3 |

The count is normalised against its own 5th and 95th percentiles rather than converted to mph,
because only the ordering matters. A zero count means the HUD is not drawn at all, which is not the
same as being stopped: the Catalyst takes a lap or two to acquire, and on this source the first two
minutes read `- -` at full racing speed. Those frames score neutrally.

Sweeping the weight over the 3h19m source, against a whole-session mean of 72.8mph:

| `--speed-weight` | cuts | mean speed of selected footage |
|---|---|---|
| 0.0 | 8 | 74.7 mph |
| 0.3 (default) | 9 | 76.6 mph |
| 0.5 | 9 | 80.9 mph |
| 1.0 | 11 | 83.3 mph |

Cut count rises with the weight because high-speed stretches are shorter than the fights they are
taken from.

## Reading the delta timer

There is no OCR engine involved. The field shows fourteen glyphs in a fixed font at a fixed place,
so it is read with 20x16 bitmap templates matched by normalised correlation, built into the file at
784 characters and costing under 2ms a frame. The templates were derived from the footage itself:
every glyph was extracted and clustered, and the cluster means were labelled.

Resolution turned out to be the whole problem. The delta text is 28px tall in the source but only
11px once the frame is scaled to 768 wide for detection, and at that size the glyphs touch and
cannot be told apart. Reading it off the scaled frame managed 24%. So the analysis pass asks ffmpeg
to stack a native-resolution crop of the HUD underneath the scaled frame and reads the timer from
that. It is still one decode and one pipe, about 19% more bytes, and the read rate went to 93% over
the full 3h19m.

A read is accepted only when the decoded text matches the field's format and every glyph clears a
correlation floor. Measured on 44 frames at native resolution: 42 correct, 0 wrong, 2 not read. Zero
wrong is the property being bought. An earlier version recovered one more frame but also produced a
confidently wrong 60.13 where the truth was 107.13, at a correlation of 0.10 against 0.6 to 0.95 for
genuine reads. A wrong delta drags the scoring around; an unread frame just scores neutrally.

Unread frames hold the previous value for `--delta-hold` (3s), since the delta moves slowly and a
dropped frame is usually a rendering blip. Beyond that they score neutrally.

## Performance

Measured on a 16-core CPU with no usable GPU, against a 3h19m 1080p29.97 source:

| stage | rate | 3h19m source |
|---|---|---|
| decode alone | 63x realtime | ~3 min |
| analyse, `yolo11n` at 768 | 17x realtime | ~12 min |
| delta OCR, inside the analyse pass | <2ms/frame | ~1.4 min of the above |
| cut and concat | | under a minute |

End to end on that source: 10.5 min to analyse at 19x realtime, 93% of frames with a delta read, 95%
with a readable gauge, then 9 cuts encoded to a 598s reel.

Inference dominates, so the knobs that matter are `--sample-fps` (4) and `--imgsz` (768). Both trade
accuracy for speed roughly linearly. Model choice was measured against `yolo11s` at 1280 as a
reference, on frames from this footage:

| model | ms/frame | cars found | missed | extra |
|---|---|---|---|---|
| yolo11s @1280 | 98.5 | 29 | reference | reference |
| yolo11s @768 | 35.9 | 29 | 4 | 4 |
| yolo11n @768 | 15.7 | 24 | 7 | 2 |
| yolo11n @640 | 9.9 | 16 | 14 | 1 |

`yolo11n` at 768 is the default. Per-frame recall of roughly 76% sounds lossy, but a 25-second
window at 4fps is 100 frames and the scores are aggregates over those windows, so the misses average
out and the ranking of windows survives. Pass `--model yolo11s.pt` to spend the time.

ONNX Runtime was tried and rejected: at batch 1 it ran at 100ms/frame against batched torch at
15.7ms, because torch keeps all 16 cores busy across a batch.

## Caching

Detections are cached in `.racecut-cache/`, keyed on the source file and every flag that affects
detection. Changing `--battle-ratio`, the target length, or the minimum cut length reuses the cache
and returns immediately. Changing `--sample-fps`, `--imgsz`, `--conf` or a ROI re-runs the analysis.
`--no-cache` disables it.

## Limitations

- Cuts are re-encoded rather than stream-copied, because copying can only cut on keyframes and would
  drift the boundaries by up to a couple of seconds. Quality is set by `--crf` (18).
- The scoring has no idea who is ahead. A car you are being lapped by scores the same as one you are
  fighting for position.
- The pit filter keys on people, so it will also reject a grid walk, a pit-lane start, and any
  genuine racing that runs past a crowded corner station.
- `--max-cut` (3x the minimum) bounds how far a single cut grows, but two cuts that end up adjacent
  are merged or bridged and the result may exceed it. That is deliberate: one continuous interesting
  stretch stays one continuous shot.
