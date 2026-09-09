# racecut

Finds the parts of a long endurance-race video where racing is actually happening, and cuts them
into a highlight reel.

The problem it solves is that a stint is mostly empty track. Running YOLO over seven hours of 1080p
to find the good bits costs more than the footage is worth, so `racecut` does two passes:

| pass | what it touches | cost |
|---|---|---|
| analyse | a cropped, downscaled, low-frame-rate proxy decoded on the fly | ~17x realtime |
| cut | the untouched HD original, only the chosen ranges | seconds per cut |

Nothing is written to disk between the passes except a small JSON of detections, so a 3-hour source
is analysed once and can then be recut at any length or ratio instantly.

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

```sh
.venv/bin/python racecut.py sunday.mp4 25 600 -o highlights.mp4
```

Three positional arguments, in order:

1. the source video
2. the minimum length of any single cut, in seconds
3. the approximate total length of the output, in seconds

So the line above builds a roughly 10-minute reel in which no cut is shorter than 25 seconds.

### What counts as interesting

Two independent measures, mixed by `--battle-ratio`:

- **traffic** is weighted car-seconds packed into a window, which reads as the average number of
  cars on screen. Cars closer to the camera count for more.
- **battle** is the longest single car held continuously on screen across a window, bonused by how
  long that fight runs in total. A car you follow through six corners scores far above six
  different cars glimpsed once each.

`--battle-ratio 1.0` spends the whole output on battles, `0.0` on traffic, and the default `0.5`
splits it evenly. If one side cannot spend its share, for instance a clean session with no sustained
fights, the remainder rolls over to the other so the reel still reaches the requested length.

Two further measures scale both of the above. `--closeness-weight` (default 0.3) raises windows
where the racing was close, and `--speed-weight` (default 0.3) raises windows taken at speed, so a
fight held at 110mph outranks the same fight held at 50mph. It multiplies each window's score by
`(1 - w) + w * speed`, where speed is normalised across the session. `0` ignores speed entirely,
`1` makes the score scale fully with it, and the default is `0.3`. See [Reading the
speedometer](#reading-the-speedometer) for where the number comes from and when to turn it off.

### Closeness

`--closeness-weight` uses the widest car box in each frame: a car filling a tenth of the frame is
right in front of you, a 30px speck is not. It multiplies each window's score by
`(1 - w) + w * closeness`, the same shape as the speed weighting.

It is measured **per frame**, which is the point. A 60-second battle that starts 100m back and ends
bumper-to-bumper has one mean bbox width across its whole length, so weighting by that would rank
the dull opening exactly like the close finish. Scoring each window on the closeness during that
window lets the selector cut the part where the cars are actually together.

`--closeness-ref` (default 0.10) sets what counts as maximally close, as a fraction of frame width.
Anything wider saturates. Raise it if your camera is wide-angle and cars look small even when they
are alongside; lower it if nothing ever reaches the top of the range.

Normalisation is against that absolute reference rather than against percentiles of the session.
That is deliberate: a stint with no close racing in it should score low throughout, rather than
having its mildest moments stretched to fill the range the way a percentile would.

This is a different thing from the per-track size weight inside the traffic sum, which decides how
much each of several simultaneous cars contributes to one frame. That one weighs cars against each
other; this one weighs windows against each other.

### Lap pace

`--delta-weight` (default 0.2) prefers windows where the delta timer is low, i.e. where you were
lapping close to or under your session best.

The timer is read by OCR, so this works in real seconds. Badness rises linearly from 0 and
saturates at `--delta-ref` (default 5.0s), which stops a minute lost in the pits from swamping
everything else. Values over a minute render as `+M:SS.hh` and are handled.

#### How the OCR works

There is no OCR engine involved. The field shows fourteen glyphs in a fixed font at a fixed place,
so it is read with 20x16 bitmap templates matched by normalised correlation, built into the file at
784 characters and costing under 2ms a frame.

The templates were derived from the footage itself: every glyph was extracted and clustered, and
the cluster means were labelled.

Resolution turned out to be the whole problem. The delta text is 28px tall in the source but only
11px once the frame is scaled to 768 wide for detection, and at that size the glyphs touch each
other and cannot be told apart. Reading it off the scaled frame managed 24%. So the analysis pass
now asks ffmpeg to stack a native-resolution crop of the HUD underneath the scaled frame and reads
the timer from that. It is still one decode, one pipe and about 19% more bytes, and the read rate
went to 93% over the full 3h19m.

A read is accepted only when the decoded text matches the field's format and every glyph clears a
correlation floor. Measured on 44 frames at native resolution: 42 read correctly, 0 read wrongly,
2 not read.

Zero wrong is the property being bought. An earlier version recovered one more frame but also
produced a confidently wrong 60.13 where the truth was 107.13, at a correlation of 0.10 against
0.6 to 0.95 for genuine reads. A wrong delta drags the scoring around; an unread frame just scores
neutrally. The floor sits where wrong answers stop appearing.

Unread frames hold the previous value for `--delta-hold` seconds (default 3), since the delta moves
slowly and a dropped frame is usually a rendering blip. Beyond that they score neutrally. The read
disables itself if too few frames decode, so a different camera or a moved field degrades to
ignoring pace rather than inventing it.

One property worth knowing: early in a session almost every lap sets a new session best, so the
delta is negative throughout and every window scores a perfect pace. `--delta-weight` therefore
carries a mild bias toward the start of a stint.

The default is low on purpose, because this measure pulls against the other two. Time lost fighting
a car shows up as a worse delta, so the best racing often has the worst pace, and a high
`--delta-weight` will drift the reel toward clean-air laps with nobody around. Its real value is at
low settings, where it demotes the obviously off-pace footage without overriding the racing.

## Yellow flags

The lap counter across the top of the frame is not drawn during a full course yellow, so its absence
marks the caution laps, and those windows are rejected outright. There is no racing under yellow and
a reel that cuts to a caution lap looks like a mistake.

The same test drops the opening laps, where the counter is absent because the Catalyst has not
acquired yet. That is intended, though note it is a change: those frames used to be kept and scored
neutrally, so a cut you saw near the start of an earlier reel may not survive.

Detection is a band of near-white pixels in `--lap-roi`. The two states are far apart, which is what
makes it work: the counter fills about 0.13 of the band, while an empty banner reads about 0.007,
all of it antialiasing along the banner's own edge rather than text. `--lap-min-frac` (default 0.04)
sits between them. Setting it too near zero makes an empty banner look like a counter, which on this
footage wrongly passed the first 44 seconds of the session.

The upper bound matters as much as the lower one: if the banner is gone entirely the region shows
sky, which is bright, so a naive "is it bright" test would call that text. The flag is smoothed over `--yellow-smooth` seconds (default 6)
before `--yellow-frac` (default 0.4) is applied, so one misread frame cannot punch a hole in a good
stretch. `--no-yellow-filter` keeps everything.

### Padding

`--pad-start` and `--pad-end` (both default 1.0s) keep extra footage either side of the segment that
actually scored, which is what stops a cut opening mid-corner. They are independent, so a long
lead-in with a hard exit is `--pad-start 5 --pad-end 0`. `--pad` sets both at once.

Padding is charged against the target length, so adding it shortens the scored cores rather than
overrunning the reel.

### Bridging short jumps

Two cuts chosen a few seconds apart get spliced together with the dead time between them removed,
which reads as a hiccup: same corner, same cars, but the picture skips. `--bridge-gap` (default
3.0s) says that when the jump left between two finished cuts is that short or shorter, keep rolling
through the quiet bit instead of cutting it out.

It acts on the final padded ranges, because that gap is what actually ends up on screen. That also
makes it the thing which stops overlapping pads from putting the same second of footage into the
reel twice, which is possible whenever `--min-gap` is smaller than `--pad-start` plus `--pad-end`.
`--bridge-gap 0` disables it.

Note that `--min-gap` and `--bridge-gap` do different jobs. `--min-gap` applies during selection and
keeps two picks from landing on top of each other. `--bridge-gap` applies afterwards and decides
whether a surviving gap is worth cutting at all. Bridging adds the gap to the output but removes two
interior pads, so at the default settings it costs roughly nothing in total length.

```sh
# a reel that is mostly wheel-to-wheel fights
.venv/bin/python racecut.py sunday.mp4 30 900 --battle-ratio 0.8
```

### Seeing the decisions before encoding

```sh
.venv/bin/python racecut.py sunday.mp4 25 600 --analyse-only --dump-scores scores.csv
```

`--analyse-only` prints the chosen ranges and writes no video. `--dump-scores` writes the per-window
traffic and battle scores so you can plot them against the source timeline.

## Tuning for a different camera

The defaults are set for a forward-facing dash cam with a Garmin Catalyst overlay: a banner across
the top, and the car's own hood plus gauges across the bottom. Three settings carry that assumption.

| flag | default | why |
|---|---|---|
| `--roi-top` | 0.10 | crops off the lap/segment banner |
| `--roi-bottom` | 0.72 | crops off the hood and the speedometer |
| `--speed-roi` | 0,0.699,0.172,0.977 | where the speedometer is, if `--speed-weight` is on |
| `--lap-roi` | 0.28,0.010,0.42,0.075 | where the lap counter is, for the yellow-flag filter |
| `--delta-roi` | 0.677,0.912,0.844,0.986 | where the delta timer is, if `--delta-weight` is on |
| `--max-car-width` | 0.35 | the ego car's own hood otherwise detects as a car in nearly every frame |

Cropping is not only about false positives. It removes about a third of every frame before
inference, which is a direct speedup.

`--min-car-width` (default 0.026, so 50px on a 1920-wide source) and `--min-track-sec` (default 1.5)
together discard cars that are not part of the race. Paddock cars parked alongside the track are the
case that matters: they detect readily, but they are small and they sweep out of frame in under a
second, so both filters reject them. On the test footage the paddock straight scores 0.00 while
genuine traffic 40 seconds later scores 0.85.

If your camera has no overlay and no visible bodywork, set `--roi-top 0` and `--roi-bottom 1.0`.

## The pit lane

Those two filters are tuned for passing parked cars at racing speed. In the pit lane you crawl past
the same cars slowly, so they stay in frame long enough to beat `--min-track-sec` and look close
enough to beat `--min-car-width`. A pit stop therefore scores as the heaviest traffic of the session,
which is how the first build of this tool put a 75-second pit stop in the reel.

The signal that separates them is people. On the racing surface there are essentially none; in the
pits there are always some. YOLO is already running, so class 0 rides along in the same forward pass
at no cost. Measured on this footage:

| window | frames containing a person |
|---|---|
| pit stop at 0:48:33 | 29% |
| pit entry at 0:47:50 | 19% |
| four racing windows | 0% to 1% |

Acting on that raw flag directly would be wrong in both directions, so it goes through three steps.
The per-frame flag is smoothed over `--pit-smooth` seconds (default 20) into a density, so one stray
detection of a corner worker cannot veto 25 seconds of good racing. Regions whose density clears
`--pit-person-frac` (default 0.08) are marked as pits. Those regions are then widened by
`--pit-dilate` seconds either side (default 45), because the slow lap in and out of the pits is not
racing either and has no people visible for most of it.

On the pit-stop range this rejects 111 of 116 candidate windows and selects nothing. On a racing
range it rejects none and the selection is unchanged. `--no-pit-filter` turns it off.

Optical flow was tried first, on the theory that pit lane means low speed. It does not work: median
flow magnitude was 1.82 in the pits against 1.69 at racing speed, because in the pit lane you are
close to walls and trailers, so nearby objects sweep past quickly even at 15mph. Flow magnitude
measures depth as much as speed.

## Reading the speedometer

`--speed-weight` needs a speed, and since flow does not give one, it is read off the Catalyst
speedometer directly. The gauge draws a red arc that grows with speed, so counting red pixels in a
fixed region is enough. That count turns out to be very nearly linear in mph:

| gauge reads | red pixels | px per mph |
|---|---|---|
| 6 | 358 | 59.7 |
| 12 | 685 | 57.1 |
| 53 | 3034 | 57.2 |
| 64 | 3668 | 57.3 |
| 77 | 4433 | 57.6 |
| 89 | 5073 | 57.0 |
| 126 | 7215 | 57.3 |

Those counts were measured on full-resolution frames. The analysis pass reads the gauge on the
downscaled frame, so its counts are about 0.15x these, which does not matter: the count is
normalised against its own 5th and 95th percentiles rather than converted to mph, because only the
ordering matters.

Sweeping the weight over the 3h19m source, against a whole-session mean of 72.8mph, the effect is
monotonic:

| `--speed-weight` | cuts | mean speed of selected footage |
|---|---|---|
| 0.0 | 8 | 74.7 mph |
| 0.3 (default) | 9 | 76.6 mph |
| 0.5 | 9 | 80.9 mph |
| 1.0 | 11 | 83.3 mph |

Cut count rises with the weight because high-speed stretches are shorter than the fights they are
taken from, so the reel fills with more, shorter cuts.

The gauge sits below the detection ROI, so the analysis pass now decodes the whole frame and slices
both regions out of it. That is one decode, not two. It is not bit-identical to letting ffmpeg crop
the ROI first, because the area scaler averages a slightly different row phase when reducing 1080
rows rather than 670, which moves a handful of marginal detections. Neither version is more correct.

Two things this has to get right. A zero count means the HUD is not drawn at all, which is not the
same as being stopped: the Catalyst takes a lap or two to acquire, and on this source the first
two minutes read `- -` while the car is racing at full speed. Those frames are treated as unknown
and score neutrally rather than being penalised as parked. And if the region holds no usable signal,
because your camera is not a Catalyst or the gauge is elsewhere, weighting disables itself with a
warning rather than scoring noise.

`--speed-roi x0,y0,x1,y1` moves the region (fractions of the frame, default `0,0.699,0.172,0.977`).
`--speed-weight 0` turns the whole thing off.

## Performance

Measured on a 16-core CPU with no usable GPU, against a 3h19m 1080p29.97 source:

| stage | rate | 3h19m source |
|---|---|---|
| decode alone | 63x realtime | ~3 min |
| analyse, `yolo11n` at 768 | 17x realtime | ~12 min |
| delta OCR, inside the analyse pass | <2ms/frame | ~1.4 min of the above |

Measured end to end on the 3h19m source: 10.5 min to analyse at 19x realtime, 93% of frames with a
delta read, 95% with a readable gauge, then 9 cuts encoded to a 598s reel.
| cut and concat | - | under a minute |

Inference dominates, so the knobs that matter are `--sample-fps` (default 4) and `--imgsz`
(default 768). Both trade accuracy for speed roughly linearly.

Model choice was measured rather than assumed. Against `yolo11s` at 1280 as a reference, on frames
from this footage:

| model | ms/frame | cars found | missed | extra |
|---|---|---|---|---|
| yolo11s @1280 | 98.5 | 29 | reference | reference |
| yolo11s @768 | 35.9 | 29 | 4 | 4 |
| yolo11n @768 | 15.7 | 24 | 7 | 2 |
| yolo11n @640 | 9.9 | 16 | 14 | 1 |

`yolo11n` at 768 is the default. Per-frame recall of roughly 76% sounds lossy, but a 25-second
window at 4fps is 100 frames, and the scores are aggregates over those windows, so the misses
average out and the ranking of windows is what survives. Pass `--model yolo11s.pt` if you would
rather spend the time.

ONNX Runtime was tried and rejected: at batch 1 it ran at 100ms/frame against batched torch at
15.7ms, because torch keeps all 16 cores busy across a batch.

## Caching

Detections are cached in `.racecut-cache/`, keyed on the source file and every flag that affects
detection. Changing `--battle-ratio`, the target length, or the minimum cut length reuses the cache
and returns immediately. Changing `--sample-fps`, `--imgsz`, `--conf` or the ROI re-runs the
analysis. `--no-cache` disables it.

## Limitations

- Cuts are re-encoded rather than stream-copied, because copying can only cut on keyframes and would
  drift the boundaries by up to a couple of seconds. Quality is set by `--crf` (default 18).
- The scoring has no idea who is ahead. A car you are being lapped by scores the same as one you are
  fighting for position.
- The pit filter keys on people, so it will also reject a grid walk, a pit-lane start, and any
  genuine racing that happens to run past a crowded corner station.
- `--max-cut` (default 3x the minimum) bounds how far a single cut grows, but two cuts that end up
  adjacent are merged or bridged and the result may exceed it. That is deliberate: it means one
  continuous interesting stretch stays one continuous shot.
