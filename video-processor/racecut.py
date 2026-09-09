#!/usr/bin/env python3
"""
racecut - find the interesting parts of long endurance-race footage and cut a highlight reel.

Two-pass design, because running YOLO on hours of 1080p is not worth it:

  pass 1 (analyse)  ffmpeg decodes the source at a low frame rate, crops away the
                    HUD/hood, and scales down. YOLO sees only those small frames.
                    Detections are linked into tracks, and tracks are scored.
  pass 2 (cut)      the chosen time ranges are sliced out of the untouched HD
                    original and concatenated.

"Interesting" is two different things, mixed according to --battle-ratio:

  traffic  many cars on screen at once
  battle   one car held on screen for a long stretch, i.e. a fight that runs
           over several corners

Several inputs can be given at once. They are laid end to end on one virtual
timeline and scored together, so a weekend of footage competes for one budget,
but no single cut is ever allowed to span two of them.

Detections are cached per input file, so re-running with a different target
length or ratio is instant, and adding a second day only analyses the new file.


USAGE

    racecut.py [options] input [input ...] min_cut target_len

    input        one or more source videos, in the order they should appear
    min_cut      no cut in the output is shorter than this, in seconds
    target_len   approximate total length of the output, in seconds

    racecut.py sunday.mp4 25 600
    racecut.py -o weekend.mp4 --battle-ratio 0.8 saturday.mp4 sunday.mp4 10 300

    The lengths come last because the number of inputs is variable. Everything
    else is a named option and can go anywhere.


WHAT GETS PICKED

    -o, --output PATH       where to write the reel (highlights.mp4)
    --battle-ratio F        split of output time between the two measures:
                            1.0 all battle, 0.0 all traffic, 0.5 even. If one
                            side cannot spend its share the rest rolls over,
                            unless you asked for exactly 0.0 or 1.0 (0.5)
    --closeness-weight F    how much close racing raises a score, measured
                            from the widest car box in each frame. Same shape
                            as --speed-weight below: 0 ignores closeness, 1
                            scales fully with it. Scored per window, so the
                            bumper-to-bumper end of a long fight outranks its
                            distant opening (0.3)
    --closeness-ref F       box width, as a fraction of frame width, counted
                            as maximally close. Anything wider saturates. Set
                            it to roughly how wide a car looks when you are
                            right behind it (0.10)
    --delta-weight F        how much a low delta timer raises a score. The
                            timer is read by OCR, so this works in real
                            seconds (0.2)
    --delta-ref F           seconds off your session best counted as fully off
                            the pace. Anything worse scores the same, so a
                            minute lost in the pits cannot swamp the rest (5.0)
    --delta-roi x0,y0,x1,y1 fractions of the frame holding the delta timer
                            (0.677,0.912,0.844,0.986)
    --delta-hold F          seconds an unreadable delta keeps its last value
                            before it counts as unknown (3.0)
    --speed-weight F        how much ego speed raises a score. Each window is
                            multiplied by (1-F) + F*speed. 0 ignores speed
                            entirely, 1 scales fully with it (0.3)
    --speed-roi x0,y0,x1,y1 fractions of the frame holding the speedometer,
                            read as the length of its red arc. Disables itself
                            with a warning if there is no gauge there
                            (0,0.699,0.172,0.977)

DETECTION  (changing any of these re-runs the analysis pass)

    --model PATH            YOLO weights. yolo11s.pt is slower and sees more
                            distant cars (yolo11n.pt)
    --sample-fps F          frames per second actually analysed (4.0)
    --imgsz N               detection width; the ROI is scaled to this (768)
    --conf F                minimum detection confidence (0.35)
    --roi-top F             crop away everything above this fraction of frame
                            height, i.e. the HUD banner (0.10)
    --roi-bottom F          crop away everything below this fraction, i.e. the
                            hood and gauges (0.72)
    --min-car-width F       ignore boxes narrower than this fraction of frame
                            width. Rejects distant specks (0.026)
    --max-car-width F       ignore boxes wider than this. Rejects your own
                            bodywork, which detects as a car otherwise (0.35)
    --min-person-width F    ignore people narrower than this, for the pit
                            filter below (0.008)

TRACKING  (also re-runs the analysis pass)

    --min-track-sec F       discard tracks shorter than this. Scenery and
                            parked cars die young; a car you are racing does
                            not (1.5)
    --min-hits N            discard tracks seen fewer times than this (3)
    --track-max-age F       seconds a track survives with no detection before
                            it is closed (1.0)

SELECTION  (cheap: these reuse the cache and re-run in a second)

    --max-cut F             longest a single cut may grow to. Two cuts that
                            end up adjacent are merged and may exceed it, so
                            one continuous stretch stays one shot (3x min_cut)
    --stride F              granularity of the candidate window search (1.0)
    --extend-frac F         keep growing a cut while the neighbouring score is
                            at least this fraction of the seed score (0.6)
    --min-gap F             during selection, two picks must stay at least
                            this far apart; cores closer than it are merged (2.0)
    --bridge-gap F          after padding, if the jump left between two
                            finished cuts is this short or shorter, keep the
                            footage between them rather than cutting it out.
                            A two second hop over nothing reads as a glitch,
                            where simply rolling through it reads as one shot.
                            Also stops overlapping pads from putting the same
                            second in the reel twice. 0 disables (3.0)
    --pad-start F           seconds of lead-in kept before the segment that
                            actually scored (1.0)
    --pad-end F             seconds of run-out kept after it (1.0)
    --pad F                 sets both of the above at once
    --pit-person-frac F     a region counts as pits when people appear in more
                            than this fraction of its frames (0.08)
    --pit-smooth F          seconds over which person sightings are averaged
                            before that test is applied, so one stray corner
                            worker cannot veto good racing (20.0)
    --pit-dilate F          seconds either side of a pit region also rejected,
                            covering the slow lap in and out, during which no
                            people are visible (45.0)
    --no-pit-filter         keep pit and paddock footage
    --lap-roi x0,y0,x1,y1   fractions of the frame holding the lap counter,
                            whose absence marks a full course yellow
                            (0.28,0.010,0.42,0.075)
    --lap-min-frac F        fraction of the lap band that must be near-white
                            for the counter to count as drawn. An empty banner
                            reads about 0.007 from its own antialiased edge,
                            the counter about 0.13 (0.04)
    --yellow-frac F         treat footage as under yellow when the lap counter
                            is missing from more than this fraction of a
                            window's frames (0.4)
    --yellow-smooth F       seconds the lap counter is averaged over before
                            that test, so one misread frame cannot punch a
                            hole in a good stretch (6.0)
    --no-yellow-filter      keep footage with no lap counter, i.e. caution
                            laps and the opening laps before the HUD acquires

PERFORMANCE AND IO

    --threads N             torch threads for inference (all cores)
    --ffmpeg-threads N      decode threads, 0 lets ffmpeg choose (0)
    --batch N               frames per inference batch. Batching is what keeps
                            every core busy, so do not set this to 1 (16)
    --jobs N                segment encodes run in parallel (4)
    --crf N                 x264 quality for the output, lower is better (18)
    --preset NAME           x264 speed preset (veryfast)
    --start F               analyse from here. With several inputs this is a
                            position on their combined timeline (0.0)
    --end F                 analyse up to here, likewise combined (end)
    --cache-dir PATH        where detection caches live (.racecut-cache)
    --no-cache              ignore and do not write the cache
    --analyse-only          report the chosen cuts, write no video
    --dump-scores PATH      write per-window scores to this CSV

READING THE HUD

    Four of the signals above are read straight off the Catalyst overlay at
    fixed positions, so they assume this camera and this layout:

        --speed-roi   the speedometer, measured as the length of its red arc,
                      which is very close to linear in mph
        --lap-roi     the lap counter, present or absent
        --delta-roi   the delta timer, by colour and text width
        --roi-bottom  crops the hood and gauges away before detection

    The delta timer is read by OCR, using bitmap templates for the fourteen
    glyphs it can show, matched by correlation. No OCR engine is installed or
    called; the templates are built into this file and cost microseconds a
    frame. Values over a minute render as +M:SS.hh and are handled.

    A delta is only accepted when the decoded text matches the field's format
    and every glyph clears a correlation floor. Measured on 44 frames at the
    resolution the analysis pass runs at: 36 read correctly, 0 read wrongly,
    8 not read. That balance is deliberate, since a confidently wrong delta
    would drag the scoring around while an unread frame scores neutrally.

    Each of these disables itself and says so if its region holds nothing
    usable, rather than scoring noise, so a different camera degrades to the
    detection-only behaviour instead of producing nonsense.

Cuts are re-encoded rather than stream-copied, because a copy can only cut on
keyframes and would drift the boundaries by up to a couple of seconds. When
inputs disagree on size or frame rate they are normalised to the largest during
that encode, which is what lets the final join stay a stream copy.
"""

import argparse
import json
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field

import base64
import re

import cv2
import numpy as np

# COCO classes that are plausibly a race car.
VEHICLE_CLASSES = [2, 3, 5, 7]  # car, motorcycle, bus, truck
PERSON_CLASS = 0


# --------------------------------------------------------------------------
# source probing / ffmpeg plumbing
# --------------------------------------------------------------------------

def probe(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height,r_frame_rate",
         "-show_entries", "format=duration",
         "-of", "json", path],
        capture_output=True, text=True, check=True).stdout
    j = json.loads(out)
    st = j["streams"][0]
    num, den = st["r_frame_rate"].split("/")
    return {
        "width": int(st["width"]),
        "height": int(st["height"]),
        "fps": float(num) / float(den),
        "duration": float(j["format"]["duration"]),
    }


def round32(v):
    return max(32, int(round(v / 32.0)) * 32)


class FrameReader:
    """Decodes the source at sample_fps, scaled down, yielding raw BGR numpy
    frames. ffmpeg does the heavy lifting in C.

    The whole frame is decoded rather than a pre-cropped ROI, because the
    speedometer sits below the detection ROI and cropping it away in ffmpeg
    would cost a second decode of the source to get it back. The scale is
    chosen so the ROI rows land on exactly the detection height, so slicing
    them out costs nothing.

    This is not bit-identical to letting ffmpeg crop first: the area scaler
    averages a slightly different row phase when it reduces 1080 rows rather
    than the ROI's 670, which shifts the slice by under a pixel and moves a
    few marginal detections. Neither version is more correct than the other.
    """

    def __init__(self, path, start, duration, sample_fps, out_w, out_h,
                 threads=0, hud=None):
        filt = ["-vf", f"fps={sample_fps},scale={out_w}:{out_h}:flags=area"]
        self.hud_h = 0
        if hud:
            hx, hy, hw, hh = hud
            if hw > out_w:
                hh = max(2, int(round(hh * out_w / hw)) // 2 * 2)
                strip = f"crop={hw}:{hud[3]}:{hx}:{hy},scale={out_w}:{hh}"
            else:
                strip = (f"crop={hw}:{hh}:{hx}:{hy},"
                         f"pad={out_w}:{hh}:0:0:black")
            # One decode feeds both: the scaled frame the detector sees, and a
            # native resolution strip of the HUD stacked underneath it. The
            # delta timer's glyphs do not survive the downscale, and a second
            # ffmpeg would mean decoding the source twice.
            filt = ["-filter_complex",
                    f"[0:v]fps={sample_fps},split=2[a][b];"
                    f"[a]scale={out_w}:{out_h}:flags=area[m];"
                    f"[b]{strip}[h];[m][h]vstack=inputs=2[out]",
                    "-map", "[out]"]
            self.hud_h = hh
        out_h_total = out_h + self.hud_h
        cmd = ["ffmpeg", "-v", "error", "-nostdin"]
        if threads:
            cmd += ["-threads", str(threads)]
        if start > 0:
            cmd += ["-ss", f"{start:.3f}"]
        if duration is not None:
            cmd += ["-t", f"{duration:.3f}"]
        cmd += ["-i", path, "-an", "-sn"] + filt + [
            "-f", "rawvideo", "-pix_fmt", "bgr24", "-"]
        self.cmd = cmd
        self.frame_bytes = out_w * out_h_total * 3
        self.shape = (out_h_total, out_w, 3)
        self.proc = None

    def __iter__(self):
        self.proc = subprocess.Popen(
            self.cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            bufsize=self.frame_bytes * 8)
        try:
            while True:
                buf = self.proc.stdout.read(self.frame_bytes)
                if not buf or len(buf) < self.frame_bytes:
                    break
                yield np.frombuffer(buf, np.uint8).reshape(self.shape)
        finally:
            self.close()

    def close(self):
        if self.proc:
            try:
                self.proc.stdout.close()
            except Exception:
                pass
            self.proc.wait()
            self.proc = None



# --------------------------------------------------------------------------
# delta timer OCR
# --------------------------------------------------------------------------
#
# The delta field is text, and the only thing that reads it exactly is OCR.
# Rather than take a dependency on tesseract for fourteen glyphs in a fixed
# font at a fixed place, these are 20x16 bitmap templates matched by
# normalised correlation, which costs microseconds a frame.
#
# They were built from the footage itself: glyphs were clustered, the cluster
# means labelled by eye, and then rebuilt at the resolution the analysis pass
# actually decodes at, supervised by the full-resolution reads. Measured on 44
# frames at that resolution: 36 correct, 0 wrong, 8 unread. Unread beats wrong
# here, so DELTA_MIN_CONF is set where wrong answers stop appearing.

DELTA_GLYPHS = {
    '+': "AAAAAAAAAAAAAAAAAAADgAPAA8Af+D/8P/w//A/4A8ADwAPAA4AAAA==",
    '-': "AAAAAAAAAAAAAAAAAAAAAAAAAAAP8A/wD/AH4AAAAAAAAAAAAAAAAA==",
    '.': "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA8AH4AfgB+AH4A==",
    '0': "D/A//H/+//////w//D/8P/w//D/8P/w//D/8P/w//D///3/+P/4f+A==",
    '1': "APAD+A/4H/gf+B/4H/gB+AH4AfgB+AH4AfgB+AH4AfgB+AH4AfgB8A==",
    '2': "B/h//H/+f/4w/gA+AH4A/gP+H/w/+H/wf4B+AHwAf/x//n/+f/5//g==",
    '3': "B8B/+H/8f/4//gB+AH4A/g/+D/gP/A/+AH4AfgB+AH5//n/+f/x/8A==",
    '4': "AeAD8AfgB+APwA/AH4A/nD88fj5+Pv5+//////////8AfgB+AH4AfA==",
    '5': "//j/+P/8//z/+PgA/AD/wP/4//z//H/8AH4AfgB+APz//P/8//j/4A==",
    '6': "B+A//n/+//7//PwA/AD/8P/+//7//v///D/8P/w//D///v/+P/wf+A==",
    '7': "f/5//n/+f/5//gD+APwA/AH4AfgB8APwA/AH4AfgD+APwB+AH4AfAA==",
    '8': "AeA//n////////wf/B/+P//+P/5//v///D/8H/wf/B//////f/4f/A==",
    '9': "BsB//P/+//7//vw/+D/8P////////3//H/8APwA/AH5//n/+f/x/8A==",
    ':': "AAAAAAAAAAAAAAfgB+AH4AfgB+AAAAAAAAAAAAAAB8AH4AfgB+AH4A==",
}

DELTA_H, DELTA_W = 20, 16
DELTA_MIN_CONF = 0.45
DELTA_GOOD_CONF = 0.75
DELTA_UNKNOWN = -1 << 30

# Above this the band is not text but open sky where the banner should be.
LAP_MAX_FRAC = 0.6
_DELTA_KEYS = None
_DELTA_MAT = None
_DELTA_RE = re.compile(r"([+-])(?:(\d+):)?(\d{1,2})\.(\d{2})")


def _delta_bank():
    """Unpack the templates once, into a matrix ready for correlation."""
    global _DELTA_KEYS, _DELTA_MAT
    if _DELTA_MAT is None:
        keys, rows = [], []
        for ch, b64 in DELTA_GLYPHS.items():
            bits = np.unpackbits(np.frombuffer(base64.b64decode(b64), np.uint8))
            rows.append(bits[:DELTA_H * DELTA_W].astype(np.float32))
            keys.append(ch)
        m = np.stack(rows)
        _DELTA_MAT = (m - m.mean(1, keepdims=True)) / (m.std(1, keepdims=True) + 1e-6)
        _DELTA_KEYS = keys
    return _DELTA_KEYS, _DELTA_MAT


def _delta_norm(mask, top, bot, a, b):
    """One character cell, scaled by line height and centred.

    Scaling by the line rather than the glyph is what keeps a full stop a
    small mark near the baseline instead of a block filling the cell.
    """
    sub = (mask[top:bot + 1, a:b + 1].astype(np.uint8)) * 255
    sc = DELTA_H / max(1, bot - top + 1)
    nw = max(1, int(round(sub.shape[1] * sc)))
    r = cv2.resize(sub, (nw, DELTA_H), interpolation=cv2.INTER_AREA)
    c = np.zeros((DELTA_H, DELTA_W), np.uint8)
    if nw >= DELTA_W:
        o = (nw - DELTA_W) // 2
        c[:] = r[:, o:o + DELTA_W]
    else:
        o = (DELTA_W - nw) // 2
        c[:, o:o + nw] = r
    return c


def _delta_runs(sub, gap):
    cols = np.flatnonzero(sub.any(axis=0))
    if cols.size == 0:
        return []
    runs = []
    s = p = cols[0]
    for c in cols[1:]:
        if c - p <= gap:
            p = c
        else:
            runs.append((s, p))
            s = p = c
    runs.append((s, p))
    return runs


def _delta_split(sub, runs, unit, mode):
    """Cut runs that are clearly more than one character.

    mode 0 leaves them alone, 1 divides evenly, 2 cuts at the thinnest
    columns, which is what two touching glyphs actually look like.
    """
    out = []
    for a, b in runs:
        w = b - a + 1
        n = max(1, int(round(w / unit))) if (mode and w > unit * 1.45) else 1
        if n == 1:
            out.append((a, b))
            continue
        ink = sub[:, a:b + 1].sum(axis=0).astype(float)
        cuts = [a - 1]
        for k in range(1, n):
            centre = int(round(k * w / n))
            if mode == 2:
                lo = max(1, centre - int(unit * 0.3))
                hi = min(w - 1, centre + int(unit * 0.3))
                centre = lo + int(np.argmin(ink[lo:hi])) if hi > lo else centre
            cuts.append(a + centre)
        cuts.append(b)
        for k in range(len(cuts) - 1):
            out.append((cuts[k] + 1, cuts[k + 1]))
    return out


def _delta_parse(text):
    m = _DELTA_RE.fullmatch(text or "")
    if not m:
        return None
    sign, mins, sec, cs = m.groups()
    v = int(sec) + int(cs) / 100.0 + (int(mins) * 60 if mins else 0)
    return -v if sign == "-" else v


def _delta_dot_insert(text):
    """A decimal point touching its digit is the one loss worth repairing.

    Every digit is still present, only the separator is gone, so the value is
    recovered rather than guessed at.
    """
    m = re.fullmatch(r"([+-])((?:\d+:)?)(\d{3,4})", text or "")
    if not m:
        return None
    d = m.group(3)
    return f"{m.group(1)}{m.group(2)}{d[:-2]}.{d[-2:]}"


def read_delta_value(frame, rect):
    """Delta timer as hundredths of a second, or DELTA_UNKNOWN.

    Several segmentations are tried and only results matching the field's
    format are kept, ranked by their worst glyph correlation. Anything below
    DELTA_MIN_CONF is discarded: a confidently wrong delta would drag the
    scoring around, whereas an unread frame simply scores neutrally.
    """
    q = frame[rect[1]:rect[3], rect[0]:rect[2]]
    if q.size == 0:
        return DELTA_UNKNOWN
    b = q[..., 0].astype(np.int16)
    g = q[..., 1].astype(np.int16)
    r = q[..., 2].astype(np.int16)
    mask = (((r > 120) & (r - g > 50) & (r - b > 50))
            | ((g > 110) & (g - r > 50) & (g - b > 40)))
    rows = np.flatnonzero(mask.any(axis=1))
    if rows.size == 0:
        return DELTA_UNKNOWN
    top, bot = int(rows[0]), int(rows[-1])
    sub = mask[top:bot + 1]
    keys, bank = _delta_bank()
    best = None
    # gap 1 joins only touching columns; gap 2 also bridges a one pixel
    # break inside a glyph. Anything less joins nothing, since consecutive
    # columns are already one apart.
    for gap in (1, 2):
        runs = _delta_runs(sub, gap)
        if not runs:
            continue
        ws = [b2 - a2 + 1 for a2, b2 in runs if 10 <= b2 - a2 + 1 <= 30]
        unit = int(np.median(ws)) if ws else 21
        for mode in (0, 1, 2):
            chars, worst = [], 1.0
            for a2, b2 in _delta_split(sub, runs, unit, mode):
                if b2 < a2:
                    continue
                v = _delta_norm(mask, top, bot, a2, b2).astype(np.float32).ravel()
                v = (v - v.mean()) / (v.std() + 1e-6)
                sc = bank @ v / v.size
                i = int(np.argmax(sc))
                chars.append(keys[i])
                worst = min(worst, float(sc[i]))
            if worst < DELTA_MIN_CONF:
                continue
            text = "".join(chars)
            for cand in (text, _delta_dot_insert(text)):
                val = _delta_parse(cand)
                if val is not None and (best is None or worst > best[1]):
                    best = (val, worst)
            # Most frames are read by the first hypothesis. Once one is both
            # well formed and clearly matched there is nothing to gain from
            # trying the rest, and this runs on every sampled frame.
            if best is not None and best[1] >= DELTA_GOOD_CONF:
                return int(round(best[0] * 100))
    return DELTA_UNKNOWN if best is None else int(round(best[0] * 100))


# --------------------------------------------------------------------------
# tracking
# --------------------------------------------------------------------------

@dataclass
class Track:
    tid: int
    t0: float
    t1: float
    box: tuple           # last box, source pixel coords
    hits: int = 1
    misses: int = 0
    widths: list = field(default_factory=list)


def iou(a, b):
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    if inter <= 0:
        return 0.0
    ua = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter
    return inter / ua if ua > 0 else 0.0


class IouTracker:
    """Deliberately simple greedy IoU tracker.

    At a few frames per second a car being raced barely moves between samples,
    so IoU association is enough. Cars that flash past (scenery, the paddock)
    fail to associate and die young, which is exactly the behaviour wanted:
    short tracks get filtered out later.
    """

    def __init__(self, iou_thresh=0.2, max_age=1.0, center_gate=1.5):
        self.iou_thresh = iou_thresh
        self.max_age = max_age
        self.center_gate = center_gate
        self.active = []
        self.done = []
        self.next_id = 0

    def _assoc_score(self, tr, box):
        s = iou(tr.box, box)
        if s >= self.iou_thresh:
            return s
        # Fallback: boxes of similar size whose centres are close. Helps when a
        # car moves far between samples but is clearly the same object.
        (ax1, ay1, ax2, ay2), (bx1, by1, bx2, by2) = tr.box, box
        aw, ah = ax2 - ax1, ay2 - ay1
        bw, bh = bx2 - bx1, by2 - by1
        if aw <= 0 or bw <= 0:
            return 0.0
        ratio = min(aw, bw) / max(aw, bw)
        if ratio < 0.5:
            return 0.0
        acx, acy = (ax1 + ax2) / 2, (ay1 + ay2) / 2
        bcx, bcy = (bx1 + bx2) / 2, (by1 + by2) / 2
        dist = ((acx - bcx) ** 2 + (acy - bcy) ** 2) ** 0.5
        if dist > self.center_gate * max(aw, ah):
            return 0.0
        return 0.05 * ratio  # weak match, still better than nothing

    def update(self, t, boxes):
        pairs = []
        for ti, tr in enumerate(self.active):
            for di, b in enumerate(boxes):
                s = self._assoc_score(tr, b)
                if s > 0:
                    pairs.append((s, ti, di))
        pairs.sort(reverse=True)
        used_t, used_d = set(), set()
        for s, ti, di in pairs:
            if ti in used_t or di in used_d:
                continue
            used_t.add(ti)
            used_d.add(di)
            tr = self.active[ti]
            b = boxes[di]
            tr.box = b
            tr.t1 = t
            tr.hits += 1
            tr.misses = 0
            tr.widths.append(b[2] - b[0])

        for ti, tr in enumerate(self.active):
            if ti not in used_t:
                tr.misses += 1

        for di, b in enumerate(boxes):
            if di not in used_d:
                tr = Track(self.next_id, t, t, b)
                tr.widths.append(b[2] - b[0])
                self.active.append(tr)
                self.next_id += 1

        keep = []
        for tr in self.active:
            if tr.misses * self._dt(t) > self.max_age:
                self.done.append(tr)
            else:
                keep.append(tr)
        self.active = keep
        self._last_t = t

    def _dt(self, t):
        return getattr(self, "_step", 0.25)

    def finish(self):
        self.done.extend(self.active)
        self.active = []
        return self.done


# --------------------------------------------------------------------------
# pass 1: detect + track
# --------------------------------------------------------------------------

def analyse(args, path, info, start, end):
    import torch
    from ultralytics import YOLO

    torch.set_num_threads(args.threads)

    src_w, src_h = info["width"], info["height"]
    roi_y0 = int(round(args.roi_top * src_h))
    roi_y1 = int(round(args.roi_bottom * src_h))
    roi_h = (roi_y1 - roi_y0) // 2 * 2
    roi = (0, roi_y0, src_w, roi_h)

    det_w = round32(args.imgsz)
    det_h = round32(det_w * roi_h / src_w)
    sx = src_w / det_w
    sy = roi_h / det_h

    # Decode the full frame at a scale that puts the ROI rows exactly on det_h,
    # so slicing costs nothing and matches the old ffmpeg crop pixel for pixel.
    scale_y = det_h / roi_h
    full_h = int(round(src_h * scale_y))
    r0 = int(round(roi_y0 * scale_y))
    r1 = r0 + det_h
    if r1 > full_h:
        r0, r1 = full_h - det_h, full_h

    def rect(roi):
        x0, y0, x1, y1 = roi
        return (int(round(x0 * det_w)), int(round(y0 * full_h)),
                int(round(x1 * det_w)), int(round(y1 * full_h)))

    g = rect(args.speed_roi)
    lap_rect = rect(args.lap_roi)

    # The delta timer is read at native resolution from a strip ffmpeg stacks
    # under the scaled frame, so its glyphs stay the size the templates expect.
    dx0, dy0, dx1, dy1 = args.delta_roi
    hud_x = int(dx0 * src_w) // 2 * 2
    hud_y = int(dy0 * src_h) // 2 * 2
    hud_w = max(2, (int(round(dx1 * src_w)) - hud_x) // 2 * 2)
    hud_h = max(2, (int(round(dy1 * src_h)) - hud_y) // 2 * 2)
    hud_w = min(hud_w, src_w - hud_x)
    hud_h = min(hud_h, src_h - hud_y)
    hud = (hud_x, hud_y, hud_w, hud_h)

    min_w = args.min_car_width * src_w
    max_w = args.max_car_width * src_w

    duration = None if end is None else max(0.0, end - start)
    span = (end if end is not None else info["duration"]) - start

    print(f"[analyse] {os.path.basename(path)}: "
          f"roi y={roi_y0}..{roi_y0 + roi_h} of {src_h}, "
          f"detect at {det_w}x{det_h}, {args.sample_fps} fps, "
          f"~{int(span * args.sample_fps)} frames")

    model = YOLO(args.model)
    reader = FrameReader(path, start, duration, args.sample_fps,
                         det_w, full_h, args.ffmpeg_threads, hud)
    delta_rect = (0, full_h, min(hud_w, det_w), full_h + reader.hud_h)

    tracker = IouTracker(max_age=args.track_max_age)
    tracker._step = 1.0 / args.sample_fps

    min_pw = args.min_person_width * src_w
    person_frames = []
    speed_raw = []
    close_raw = []
    nolap_frames = []
    delta_cs = []

    batch, batch_i = [], []
    n = 0
    t_begin = time.time()

    def gauge_red(frame):
        """Length of the speedometer's red arc, in pixels.

        On this HUD the arc is very close to linear in mph (measured at
        57.0 to 59.7 px per mph from 6 to 126 mph), so the raw count is a
        usable speed signal without calibrating it to real units. Zero means
        the gauge is not drawn at all, which is not the same as stopped.
        """
        q = frame[g[1]:g[3], g[0]:g[2]]
        if q.size == 0:
            return 0
        b = q[..., 0].astype(np.int16)
        gr = q[..., 1].astype(np.int16)
        r = q[..., 2].astype(np.int16)
        m = (r > 140) & (gr < 95) & (b < 95) & (r - gr > 60) & (r - b > 60)
        return int(m.sum())

    def lap_visible(frame):
        """Is the lap counter drawn across the top of the frame?

        On this HUD it disappears under a full course yellow, so its absence
        marks the caution laps. It is also missing for the first lap or two
        while the Catalyst acquires, which is not racing footage either.

        Checked as a band of near-white pixels rather than by reading the
        number. The two states are far apart, which is what makes this work:
        on this footage the counter reads about 0.13 of the band while an
        empty banner reads about 0.007, all of it antialiasing along the
        banner edge rather than text.

        The upper bound matters as much as the lower one. If the banner is
        gone entirely the region shows sky, which is bright, and a plain "is
        it bright" test would call that text.
        """
        q = frame[lap_rect[1]:lap_rect[3], lap_rect[0]:lap_rect[2]]
        if q.size == 0:
            return True
        white = ((q[..., 0] > 190) & (q[..., 1] > 190) & (q[..., 2] > 190))
        frac = float(white.mean())
        return args.lap_min_frac <= frac <= LAP_MAX_FRAC

    def flush():
        nonlocal batch, batch_i
        if not batch:
            return
        # People ride along in the same forward pass, so the pit-lane signal
        # is effectively free.
        results = model.predict(batch, imgsz=(det_h, det_w), conf=args.conf,
                                classes=VEHICLE_CLASSES + [PERSON_CLASS],
                                verbose=False, device="cpu")
        for fi, r in zip(batch_i, results):
            ts = start + fi / args.sample_fps
            boxes = []
            has_person = False
            if len(r.boxes):
                xy = r.boxes.xyxy.cpu().numpy()
                cls = r.boxes.cls.cpu().numpy().astype(int)
            else:
                xy, cls = np.empty((0, 4)), np.empty((0,), int)
            for (x1, y1, x2, y2), c in zip(xy, cls):
                X1, X2 = x1 * sx, x2 * sx
                Y1, Y2 = y1 * sy + roi_y0, y2 * sy + roi_y0
                w = X2 - X1
                if c == PERSON_CLASS:
                    if w >= min_pw:
                        has_person = True
                    continue
                # Reject the ego car's own hood / HUD furniture (huge boxes)
                # and specks too small to be a car worth filming.
                if w < min_w or w > max_w:
                    continue
                boxes.append((X1, Y1, X2, Y2))
            if has_person:
                person_frames.append(fi)
            # Widest box in this frame, i.e. how close the nearest car got.
            # Recorded per frame rather than per track so a window can be
            # scored on the closeness during that window.
            close_raw.append(int(max((b[2] - b[0] for b in boxes), default=0)))
            tracker.update(ts, boxes)
        batch, batch_i = [], []

    for i, frame in enumerate(reader):
        # Read the HUD off the full frame here, where it still exists; only
        # the ROI band below is handed to the detector.
        speed_raw.append(gauge_red(frame))
        if not lap_visible(frame):
            nolap_frames.append(i)
        delta_cs.append(read_delta_value(frame, delta_rect))
        batch.append(frame[r0:r1])
        batch_i.append(i)
        if len(batch) >= args.batch:
            flush()
        n = i + 1
        if n % (args.sample_fps * 300) == 0:
            el = time.time() - t_begin
            done = n / args.sample_fps
            rate = done / el
            eta = (span - done) / rate if rate > 0 else 0
            print(f"  {done / 60:6.1f}/{span / 60:.1f} min analysed "
                  f"({rate:.0f}x realtime, eta {eta / 60:.1f} min)", flush=True)
    flush()

    tracks = tracker.finish()
    kept = [t for t in tracks
            if t.hits >= args.min_hits and (t.t1 - t.t0) >= args.min_track_sec]
    gauge_pct = 100.0 * float(np.mean(np.asarray(speed_raw) > 0)) if speed_raw else 0.0
    delta_pct = (100.0 * float(np.mean(np.asarray(delta_cs) != DELTA_UNKNOWN))
                 if delta_cs else 0.0)
    print(f"[analyse] {n} frames, {len(tracks)} raw tracks, "
          f"{len(kept)} kept (>= {args.min_track_sec}s), "
          f"people in {len(person_frames)} frames "
          f"({100.0 * len(person_frames) / max(1, n):.1f}%), "
          f"gauge readable in {gauge_pct:.0f}% of frames, "
          f"delta read in {delta_pct:.0f}%, "
          f"{(time.time() - t_begin) / 60:.1f} min")

    return {
        "meta": {
            "input": os.path.abspath(path),
            "duration": info["duration"],
            "sample_fps": args.sample_fps,
            "start": start,
            "span": span,
            "src_w": src_w,
            "n_frames": n,
        },
        "person_frames": person_frames,
        "speed_raw": speed_raw,
        "close_raw": close_raw,
        "nolap_frames": nolap_frames,
        "delta_cs": delta_cs,
        "tracks": [
            {"id": t.tid, "t0": round(t.t0, 3), "t1": round(t.t1, 3),
             "hits": t.hits, "w": round(float(np.mean(t.widths)), 1),
             "wmax": round(float(np.max(t.widths)), 1)}
            for t in kept
        ],
    }



# --------------------------------------------------------------------------
# scoring + selection
# --------------------------------------------------------------------------
#
# With several inputs the sources are scored on one virtual timeline: clip i
# occupies [offset_i, offset_i + duration_i). That lets the existing window
# search, budget and greedy selection work unchanged across a whole weekend of
# footage. The one rule is that a window may never straddle a boundary, since
# the two sides are different sessions, so candidate windows are generated per
# clip and growth is clamped to the clip that seeded it.

def size_weight(width, src_w):
    """0.5 for a distant car, 1.0 for one filling ~10% of frame width.
    Keeps distant traffic relevant while preferring close racing."""
    s = min(1.0, width / (0.10 * src_w))
    return 0.5 + 0.5 * s


def person_gate(data, starts, win, max_frac, smooth_sec, dilate_sec):
    """Reject windows that are in the pits or the paddock.

    Parked cars alongside a hot pit lane defeat the size and duration filters,
    because you crawl past them slowly enough that they look like large,
    long-lived tracks. People standing about are the giveaway: on the racing
    surface there are essentially none, and in the pits there are always some.

    Three steps, because neither extreme works on its own. The raw per-frame
    flag is smoothed into a density first, so one stray detection of a corner
    worker cannot veto good racing. Regions whose density clears max_frac are
    then dilated, because the slow lap in and out of the pits is not racing
    either, and no people are visible for most of it.

    Runs on one clip at a time, so neither the smoothing nor the dilation can
    bleed a pit stop in one session across into the start of the next.
    """
    meta = data["meta"]
    n = meta.get("n_frames", 0)
    pf = data.get("person_frames") or []
    if not n or max_frac >= 1.0:
        return np.ones(len(starts))
    fps = meta["sample_fps"]
    t0 = meta["start"]

    flag = np.zeros(n, np.float32)
    for i in pf:
        if 0 <= i < n:
            flag[i] = 1.0

    k = max(1, int(round(smooth_sec * fps)))
    dens = np.convolve(flag, np.ones(k, np.float32) / k, mode="same")
    pit = dens > max_frac

    d = int(round(dilate_sec * fps))
    if d > 0 and pit.any():
        pit = np.convolve(pit.astype(np.float32),
                          np.ones(2 * d + 1, np.float32), mode="same") > 0

    cum = np.concatenate([[0], np.cumsum(pit.astype(np.int64))])
    keep = np.ones(len(starts))
    for j, s in enumerate(starts):
        i0 = int(round((s - t0) * fps))
        i1 = int(round((s + win - t0) * fps))
        i0, i1 = max(0, min(n, i0)), max(0, min(n, i1))
        if i1 <= i0:
            continue
        if (cum[i1] - cum[i0]) / (i1 - i0) > 0.1:
            keep[j] = 0.0
    return keep


def speed_weights(data, starts, win, weight):
    """Per-window multiplier that favours segments taken at speed.

    The gauge count is very nearly linear in mph on this HUD, so it is used
    raw and normalised against its own 5th and 95th percentiles rather than
    calibrated to real units. A zero count means the HUD was not drawn (it
    takes a lap or two to appear), which is not the same as being stopped, so
    those frames are treated as unknown and score neutrally instead of being
    penalised as if the car were parked.

    Normalisation is per clip, because the count scales with the frame size
    and with whatever the camera was doing that day, so raw counts from two
    different sessions are not comparable.

    Returns (multiplier per window, mean normalised speed per window, ok).
    """
    meta = data["meta"]
    raw = np.asarray(data.get("speed_raw") or [], dtype=np.float64)
    n = meta.get("n_frames", 0)
    neutral = np.ones(len(starts))
    if weight <= 0 or n == 0 or raw.size == 0:
        return neutral, None, False

    known = raw > 0
    if known.sum() < 0.2 * raw.size:
        return neutral, None, False
    lo, hi = np.percentile(raw[known], 5), np.percentile(raw[known], 95)
    if hi - lo < 1e-6 or hi <= 0:
        return neutral, None, False

    norm = np.clip((raw - lo) / (hi - lo), 0.0, 1.0)
    norm[~known] = float(np.median(norm[known]))

    fps = meta["sample_fps"]
    t0 = meta["start"]
    cum = np.concatenate([[0.0], np.cumsum(norm)])
    mean_speed = np.zeros(len(starts))
    for j, st in enumerate(starts):
        i0 = int(round((st - t0) * fps))
        i1 = int(round((st + win - t0) * fps))
        i0, i1 = max(0, min(n, i0)), max(0, min(n, i1))
        if i1 <= i0:
            mean_speed[j] = 0.5
        else:
            mean_speed[j] = (cum[i1] - cum[i0]) / (i1 - i0)
    return (1.0 - weight) + weight * mean_speed, mean_speed, True


def closeness_weights(data, starts, win, weight, ref):
    """Per-window multiplier that favours close racing.

    Uses the widest car box seen in each frame, so a window is scored on how
    close the racing was during that window. The per-track size weight in
    size_weight() is a different thing: it decides how much each of several
    simultaneous cars contributes to one frame's traffic score. This decides
    how one window ranks against another.

    Normalised against an absolute reference rather than percentiles, because
    "a car filling a tenth of the frame is close" is a statement about the
    footage, not about the rest of the session. A stint with no close racing
    in it should score low throughout, not have its mild moments stretched to
    fill the range.

    Returns (multiplier per window, mean normalised closeness, ok).
    """
    meta = data["meta"]
    raw = np.asarray(data.get("close_raw") or [], dtype=np.float64)
    n = meta.get("n_frames", 0)
    neutral = np.ones(len(starts))
    if weight <= 0 or n == 0 or raw.size == 0:
        return neutral, None, False

    full = ref * meta["src_w"]
    if full <= 0:
        return neutral, None, False
    norm = np.clip(raw / full, 0.0, 1.0)

    fps = meta["sample_fps"]
    t0 = meta["start"]
    cum = np.concatenate([[0.0], np.cumsum(norm)])
    mean_close = np.zeros(len(starts))
    for j, st in enumerate(starts):
        i0 = int(round((st - t0) * fps))
        i1 = int(round((st + win - t0) * fps))
        i0, i1 = max(0, min(n, i0)), max(0, min(n, i1))
        if i1 > i0:
            mean_close[j] = (cum[i1] - cum[i0]) / (i1 - i0)
    return (1.0 - weight) + weight * mean_close, mean_close, True


def yellow_gate(data, starts, win, max_frac, smooth_sec):
    """Reject windows run under a full course yellow.

    The lap counter across the top of the frame is not drawn during a
    caution, so its absence marks those laps. It is also absent for the first
    lap or two while the HUD acquires, which gets dropped by the same test.
    That is intended: there is no racing under yellow, and a reel that cuts to
    a caution lap looks like a mistake.

    Smoothed before thresholding so a single frame where the banner was
    misread cannot punch a hole in a good stretch.
    """
    meta = data["meta"]
    n = meta.get("n_frames", 0)
    nolap = data.get("nolap_frames")
    if not n or nolap is None:
        return np.ones(len(starts)), 0

    flag = np.zeros(n, np.float32)
    for i in nolap:
        if 0 <= i < n:
            flag[i] = 1.0

    fps = meta["sample_fps"]
    k = max(1, int(round(smooth_sec * fps)))
    dens = np.convolve(flag, np.ones(k, np.float32) / k, mode="same")
    out = dens > max_frac

    t0 = meta["start"]
    cum = np.concatenate([[0], np.cumsum(out.astype(np.int64))])
    keep = np.ones(len(starts))
    for j, st in enumerate(starts):
        i0 = int(round((st - t0) * fps))
        i1 = int(round((st + win - t0) * fps))
        i0, i1 = max(0, min(n, i0)), max(0, min(n, i1))
        if i1 <= i0:
            continue
        if (cum[i1] - cum[i0]) / (i1 - i0) > 0.1:
            keep[j] = 0.0
    return keep, int((keep == 0).sum())


def delta_weights(data, starts, win, weight, ref, hold_sec):
    """Per-window multiplier that favours laps close to your session best.

    Deltas are read off the timer by OCR, so this works on real seconds.
    Anything at or under the session best is as good as it gets; from there
    badness rises linearly and saturates at `ref` seconds off, so the pit-lane
    and yellow-flag deltas of a minute or more cannot dominate everything
    else by sheer size.

    Frames the OCR could not read hold the last value for `hold_sec`, since
    the delta moves slowly and a dropped frame is usually a rendering blip.
    Beyond that they score neutrally rather than being guessed at.

    Returns (multiplier per window, mean normalised pace, ok).
    """
    meta = data["meta"]
    cs = np.asarray(data.get("delta_cs") or [], dtype=np.int64)
    n = meta.get("n_frames", 0)
    neutral = np.ones(len(starts))
    if weight <= 0 or n == 0 or cs.size == 0 or ref <= 0:
        return neutral, None, False

    have = cs != DELTA_UNKNOWN
    if have.sum() < 0.15 * cs.size:
        return neutral, None, False

    fps = meta["sample_fps"]
    val = np.where(have, cs, 0).astype(np.float64) / 100.0
    max_hold = max(0, int(round(hold_sec * fps)))
    last, age = None, 0
    filled = have.copy()
    for i in range(len(val)):
        if have[i]:
            last, age = val[i], 0
        elif last is not None and age < max_hold:
            val[i] = last
            age += 1
            filled[i] = True
        else:
            last = None

    badness = np.clip(val / ref, 0.0, 1.0)
    badness[val <= 0] = 0.0
    if filled.any():
        badness[~filled] = float(np.median(badness[filled]))
    else:
        badness[:] = 0.5
    good = 1.0 - badness

    t0 = meta["start"]
    cum = np.concatenate([[0.0], np.cumsum(good)])
    mean_good = np.zeros(len(starts))
    for j, st in enumerate(starts):
        i0 = int(round((st - t0) * fps))
        i1 = int(round((st + win - t0) * fps))
        i0, i1 = max(0, min(n, i0)), max(0, min(n, i1))
        mean_good[j] = (cum[i1] - cum[i0]) / (i1 - i0) if i1 > i0 else 0.5
    return (1.0 - weight) + weight * mean_good, mean_good, True


def score_windows(data, win, stride):
    """Traffic and battle scores for every candidate window in one clip.
    Times returned are local to that clip."""
    meta = data["meta"]
    src_w = meta["src_w"]
    t_start = meta["start"]
    t_end = meta["start"] + meta["span"]
    tracks = data["tracks"]

    starts = np.arange(t_start, max(t_start, t_end - win) + 1e-6, stride)
    if len(starts) == 0:
        starts = np.array([t_start])

    traffic = np.zeros(len(starts))
    battle = np.zeros(len(starts))

    t0 = np.array([t["t0"] for t in tracks]) if tracks else np.zeros(0)
    t1 = np.array([t["t1"] for t in tracks]) if tracks else np.zeros(0)
    wt = np.array([size_weight(t["w"], src_w) for t in tracks]) if tracks else np.zeros(0)
    dur = t1 - t0

    for i, s in enumerate(starts):
        e = s + win
        if len(tracks) == 0:
            continue
        ov = np.minimum(t1, e) - np.maximum(t0, s)
        ov = np.clip(ov, 0, None)
        m = ov > 0
        if not m.any():
            continue
        # traffic: how much weighted car-time is packed into this window,
        # expressed as an average number of cars on screen.
        traffic[i] = float((ov[m] * wt[m]).sum() / win)
        # battle: the single best track's coverage of the window, bonused by
        # how long that fight runs in total.
        cov = ov[m] / win
        bonus = 1.0 + 0.5 * np.minimum(dur[m] / 60.0, 1.0)
        battle[i] = float(np.max(cov * wt[m] * bonus))

    return starts, traffic, battle


def normalise(v):
    if len(v) == 0:
        return v
    hi = np.percentile(v, 97)
    if hi <= 0:
        hi = v.max() if v.max() > 0 else 1.0
    return np.clip(v / hi, 0, 1.5)


def pick_segments(starts, score, win, budget, taken, min_gap, max_cut,
                  extend_frac, stride, pad_start, pad_end, clip_of, lo_of, hi_of):
    """Greedily take the best-scoring non-overlapping windows, growing each
    one outward while the neighbouring score holds up.

    Budget is charged against the final on-screen cost of a cut, both pads
    included, so the finished reel lands near the requested length instead of
    drifting over it as cuts grow.

    A cut may only grow within the clip that seeded it, so no cut can span two
    sources.
    """
    order = np.argsort(-score)
    chosen = []
    total = 0.0
    idx_of = {round(s, 3): i for i, s in enumerate(starts)}

    def overlaps(a, b):
        for (c, d) in taken + chosen:
            if a < d + min_gap and c - min_gap < b:
                return True
        return False

    for i in order:
        remaining = budget - total
        # Stop rather than tack on a stub that would break the min-cut rule.
        if remaining < win + pad_start + pad_end:
            break
        if score[i] <= 0:
            continue
        a, b = float(starts[i]), float(starts[i]) + win
        if overlaps(a, b):
            continue
        clip, lo, hi = clip_of[i], lo_of[i], hi_of[i]
        thresh = score[i] * extend_frac
        room = min(max_cut, remaining - pad_start - pad_end)
        # grow outward in stride steps, best side first
        while (b - a) + stride <= room:
            li = idx_of.get(round(a - stride, 3))
            ri = idx_of.get(round(b - win + stride, 3))
            if li is not None and clip_of[li] != clip:
                li = None
            if ri is not None and clip_of[ri] != clip:
                ri = None
            ls = score[li] if li is not None else -1
            rs = score[ri] if ri is not None else -1
            if max(ls, rs) < thresh:
                break
            if rs >= ls:
                nb = b + stride
                if nb > hi or overlaps(a, nb):
                    break
                b = nb
            else:
                na = a - stride
                if na < lo or overlaps(na, b):
                    break
                a = na
        chosen.append((a, b))
        total += (b - a) + pad_start + pad_end
    return chosen, total


def build_windows(clips, args):
    """Lay every clip's candidate windows onto one virtual timeline.

    Returns parallel arrays: global window start, traffic score, battle score,
    the clip each window belongs to, and the clip's global bounds.
    """
    win = args.min_cut
    S, T, B, C, LO, HI = [], [], [], [], [], []

    for ci, clip in enumerate(clips):
        data = clip["data"]
        starts, traffic, battle = score_windows(data, win, args.stride)

        if not args.no_pit_filter:
            gate = person_gate(data, starts, win, args.pit_person_frac,
                               args.pit_smooth, args.pit_dilate)
            dropped = int((gate == 0).sum())
            if dropped:
                print(f"[select] {clip['name']}: pit filter rejected "
                      f"{dropped} of {len(starts)} windows")
            traffic = traffic * gate
            battle = battle * gate

        if not args.no_yellow_filter:
            ygate, ydropped = yellow_gate(data, starts, win,
                                          args.yellow_frac, args.yellow_smooth)
            if ydropped:
                print(f"[select] {clip['name']}: yellow filter rejected "
                      f"{ydropped} of {len(starts)} windows (no lap counter)")
            traffic = traffic * ygate
            battle = battle * ygate

        dmult, mean_good, dok = delta_weights(
            data, starts, win, args.delta_weight, args.delta_ref, args.delta_hold)
        if dok:
            print(f"[select] {clip['name']}: delta weighting at "
                  f"{args.delta_weight:.2f}, window pace spans "
                  f"{mean_good.min():.2f}-{mean_good.max():.2f}")
        traffic = traffic * dmult
        battle = battle * dmult

        mult, mean_speed, ok = speed_weights(data, starts, win, args.speed_weight)
        if args.speed_weight > 0 and not ok:
            print(f"[select] {clip['name']}: speed weighting disabled, no "
                  f"usable speedometer in --speed-roi")
        elif ok:
            print(f"[select] {clip['name']}: speed weighting at "
                  f"{args.speed_weight:.2f}, window speed spans "
                  f"{mean_speed.min():.2f}-{mean_speed.max():.2f}")
        traffic = traffic * mult
        battle = battle * mult

        cmult, mean_close, cok = closeness_weights(
            data, starts, win, args.closeness_weight, args.closeness_ref)
        if cok:
            print(f"[select] {clip['name']}: closeness weighting at "
                  f"{args.closeness_weight:.2f}, window closeness spans "
                  f"{mean_close.min():.2f}-{mean_close.max():.2f}")
        traffic = traffic * cmult
        battle = battle * cmult

        off = clip["offset"]
        meta = data["meta"]
        lo = off + meta["start"]
        hi = off + meta["start"] + meta["span"]
        S.append(starts + off)
        T.append(traffic)
        B.append(battle)
        C.append(np.full(len(starts), ci))
        LO.append(np.full(len(starts), lo))
        HI.append(np.full(len(starts), hi))

    cat = lambda xs: np.concatenate(xs) if xs else np.zeros(0)
    return cat(S), cat(T), cat(B), cat(C).astype(int), cat(LO), cat(HI)


def bridge_cuts(segs, gap):
    """Join cuts that would otherwise be separated by a barely visible jump.

    Two cuts chosen a few seconds apart get spliced together with the dead
    time between them removed, which reads as a hiccup: the same corner, the
    same cars, but the picture skips. Below a few seconds it looks better to
    keep rolling through the quiet bit than to cut it out.

    This runs on the final padded ranges, because that gap is what actually
    ends up on screen, and it also catches the case where padding has pushed
    two cuts into each other. Without it, overlapping pads would put the same
    second of footage into the reel twice.

    Bridged cuts may exceed --max-cut, for the same reason merged ones do: one
    continuous stretch belongs in one shot.
    """
    if not segs:
        return segs, 0
    segs = sorted(segs, key=lambda s: (s["clip"], s["start"]))
    out = [dict(segs[0])]
    bridged = 0
    for s in segs[1:]:
        prev = out[-1]
        if s["clip"] == prev["clip"] and s["start"] - prev["end"] <= gap:
            if s["end"] > prev["end"]:
                prev["end"] = s["end"]
            if prev["kind"] != s["kind"]:
                prev["kind"] = "mixed"
            bridged += 1
        else:
            out.append(dict(s))
    return out, bridged


def select(clips, args):
    win = args.min_cut
    stride = args.stride
    starts, traffic, battle, clip_of, lo_of, hi_of = build_windows(clips, args)

    # Normalised across every clip at once, so a quiet Saturday does not get
    # its handful of mild moments promoted to the same rank as a busy Sunday.
    tn = normalise(traffic)
    bn = normalise(battle)

    target = args.target_len
    b_budget = target * args.battle_ratio
    t_budget = target - b_budget

    # An explicit 0.0 or 1.0 is taken literally: no rolling the unspent share
    # over into the kind of footage the user just said they did not want.
    mixed = 0.0 < args.battle_ratio < 1.0

    common = (args.min_gap, args.max_cut, args.extend_frac, stride,
              args.pad_start, args.pad_end, clip_of, lo_of, hi_of)

    taken = []
    b_seg, b_tot = pick_segments(starts, bn, win, b_budget, taken, *common)
    taken += b_seg
    # Whatever the battle pass could not spend (a session with few sustained
    # fights) rolls over, so the reel still reaches the requested length.
    spill = (b_budget - b_tot) if mixed else 0.0
    t_seg, t_tot = pick_segments(starts, tn, win, t_budget + spill, taken, *common)
    if mixed and t_tot < t_budget:
        taken += t_seg
        extra, _ = pick_segments(starts, bn, win,
                                 (b_budget + t_budget) - (b_tot + t_tot),
                                 taken, *common)
        b_seg = b_seg + extra

    segs = [(a, b, "battle") for a, b in b_seg] + [(a, b, "traffic") for a, b in t_seg]
    segs.sort()

    def clip_at(t):
        for ci, c in enumerate(clips):
            if c["offset"] <= t < c["offset"] + c["duration"]:
                return ci
        return len(clips) - 1

    # merge segments that ended up adjacent, but never across a clip boundary
    merged = []
    for a, b, kind in segs:
        ci = clip_at(a)
        if (merged and merged[-1][3] == ci
                and a - merged[-1][1] <= args.min_gap):
            pa, pb, pk, _ = merged[-1]
            merged[-1] = (pa, max(pb, b), pk if pk == kind else "mixed", ci)
        else:
            merged.append((a, b, kind, ci))

    out = []
    for a, b, kind, ci in merged:
        clip = clips[ci]
        a = max(clip["offset"], a - args.pad_start)
        b = min(clip["offset"] + clip["duration"], b + args.pad_end)
        out.append({
            "path": clip["path"], "name": clip["name"], "clip": ci,
            "start": a - clip["offset"], "end": b - clip["offset"],
            "global_start": a, "kind": kind,
        })

    out, bridged = bridge_cuts(out, args.bridge_gap)
    if bridged:
        print(f"[select] bridged {bridged} short jump"
              f"{'' if bridged == 1 else 's'} of <= {args.bridge_gap:g}s, "
              f"keeping the footage between")
    return out, (starts, tn, bn, clip_of)


# --------------------------------------------------------------------------
# pass 2: cut the HD originals
# --------------------------------------------------------------------------

def cut(args, segs, clips):
    """Slice each chosen range out of its own source and join them.

    Segments are re-encoded anyway, so when the sources disagree on size or
    frame rate they are normalised here to whatever the largest input is.
    That is what lets the join stay a stream copy.
    """
    sizes = {(c["info"]["width"], c["info"]["height"]) for c in clips}
    rates = {round(c["info"]["fps"], 3) for c in clips}
    uniform = len(sizes) == 1 and len(rates) == 1
    tw = max(c["info"]["width"] for c in clips)
    th = max(c["info"]["height"] for c in clips)
    tr = max(c["info"]["fps"] for c in clips)
    if not uniform:
        print(f"[cut] inputs differ in size or rate, normalising to "
              f"{tw}x{th} @ {tr:.2f}fps")

    tmp = tempfile.mkdtemp(prefix="racecut_")
    try:
        jobs = []
        for i, sg in enumerate(segs):
            part = os.path.join(tmp, f"p{i:04d}.mp4")
            cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y",
                   "-ss", f"{sg['start']:.3f}", "-i", sg["path"],
                   "-t", f"{sg['end'] - sg['start']:.3f}"]
            if not uniform:
                cmd += ["-vf", f"scale={tw}:{th}:force_original_aspect_ratio="
                               f"decrease,pad={tw}:{th}:(ow-iw)/2:(oh-ih)/2,"
                               f"setsar=1",
                        "-r", f"{tr:.6f}"]
            cmd += ["-c:v", "libx264", "-crf", str(args.crf),
                    "-preset", args.preset, "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
                    "-avoid_negative_ts", "make_zero", part]
            jobs.append((cmd, part))

        print(f"[cut] encoding {len(jobs)} segments ({args.jobs} at a time)")
        parts = []
        running = []
        it = iter(jobs)
        done = 0
        while True:
            while len(running) < args.jobs:
                nxt = next(it, None)
                if nxt is None:
                    break
                cmd, part = nxt
                running.append((subprocess.Popen(cmd), part))
            if not running:
                break
            still = []
            for pr, part in running:
                if pr.poll() is None:
                    still.append((pr, part))
                else:
                    if pr.returncode != 0:
                        raise RuntimeError(f"ffmpeg failed on {part}")
                    parts.append(part)
                    done += 1
                    print(f"  {done}/{len(jobs)}", flush=True)
            running = still
            if running:
                time.sleep(0.2)

        parts.sort()
        lst = os.path.join(tmp, "list.txt")
        with open(lst, "w") as f:
            for pth in parts:
                f.write(f"file '{pth}'\n")
        subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-y",
                        "-f", "concat", "-safe", "0", "-i", lst,
                        "-c", "copy", args.output], check=True)
        print(f"[cut] wrote {args.output}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# --------------------------------------------------------------------------

def hhmmss(t):
    h = int(t // 3600)
    m = int((t % 3600) // 60)
    s = t % 60
    return f"{h:d}:{m:02d}:{s:05.2f}"


def cache_path(args, path, start, end):
    key = json.dumps({
        "f": os.path.abspath(path),
        "mt": int(os.path.getmtime(path)),
        "sz": os.path.getsize(path),
        "fps": args.sample_fps, "imgsz": args.imgsz, "model": args.model,
        "conf": args.conf, "rt": args.roi_top, "rb": args.roi_bottom,
        "mw": args.min_car_width, "xw": args.max_car_width,
        "pw": args.min_person_width, "sr": args.speed_roi, "lr": args.lap_roi,
        "dr": args.delta_roi, "lmf": args.lap_min_frac, "v": 7,
        "st": start, "en": end,
        "mts": args.min_track_sec, "mh": args.min_hits, "ma": args.track_max_age,
    }, sort_keys=True)
    h = hashlib.sha1(key.encode()).hexdigest()[:16]
    return os.path.join(args.cache_dir, f"racecut_{h}.json")


def split_positionals(values, parser):
    """`inputs... min_cut target_len`.

    Done by hand rather than with nargs so that the error for a mistyped
    number names the problem instead of complaining about argument counts.
    """
    if len(values) < 3:
        parser.error("need at least one input, then min_cut and target_len, "
                     "e.g. racecut.py saturday.mp4 sunday.mp4 25 600")
    inputs, tail = values[:-2], values[-2:]
    try:
        min_cut, target_len = float(tail[0]), float(tail[1])
    except ValueError:
        parser.error(f"min_cut and target_len must be numbers, got "
                     f"{tail[0]!r} and {tail[1]!r}. Inputs come first, "
                     f"lengths last.")
    if min_cut <= 0 or target_len <= 0:
        parser.error("min_cut and target_len must be positive")
    for f in inputs:
        if not os.path.exists(f):
            parser.error(f"no such file: {f}")
    return inputs, min_cut, target_len


def main():
    p = argparse.ArgumentParser(
        description="Find the racing in long race footage and cut a highlight reel.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        usage="%(prog)s [options] input [input ...] min_cut target_len")
    p.add_argument("args", nargs="+", metavar="input ... min_cut target_len",
                   help="one or more source videos, then the minimum length of "
                        "any single cut and the approximate total length of "
                        "the output, both in seconds")
    p.add_argument("-o", "--output", default="highlights.mp4")
    p.add_argument("--battle-ratio", type=float, default=0.5,
                   help="share of output time from battles vs traffic "
                        "(1.0 = all battle, 0.0 = all traffic)")
    p.add_argument("--closeness-weight", type=float, default=0.3,
                   help="how much close racing raises a segment's score, "
                        "measured from the widest car box in each frame "
                        "(0 = ignore closeness, 1 = score scales fully with it)")
    p.add_argument("--closeness-ref", type=float, default=0.10,
                   help="box width, as a fraction of frame width, that counts "
                        "as maximally close; anything wider saturates")
    p.add_argument("--speed-weight", type=float, default=0.3,
                   help="how much ego speed raises a segment's score "
                        "(0 = ignore speed, 1 = score scales fully with it)")
    p.add_argument("--speed-roi", default="0,0.699,0.172,0.977",
                   help="x0,y0,x1,y1 fractions of the frame holding the "
                        "speedometer, read as a red-arc length")
    p.add_argument("--delta-weight", type=float, default=0.2,
                   help="how much a low delta raises a segment's score. Note "
                        "this pulls against traffic and battle, since time "
                        "lost fighting a car shows up as a worse delta")
    p.add_argument("--delta-ref", type=float, default=5.0,
                   help="seconds off your session best that counts as fully "
                        "off the pace; worse deltas score the same as this")
    p.add_argument("--delta-roi", default="0.677,0.912,0.844,0.986",
                   help="x0,y0,x1,y1 fractions of the frame holding the "
                        "delta timer, read by OCR")
    p.add_argument("--delta-hold", type=float, default=3.0,
                   help="seconds an unreadable delta keeps the last value "
                        "before it is treated as unknown")

    g = p.add_argument_group("detection")
    g.add_argument("--model", default="yolo11n.pt")
    g.add_argument("--sample-fps", type=float, default=4.0,
                   help="frames per second analysed in the proxy pass")
    g.add_argument("--imgsz", type=int, default=768,
                   help="detection width; the ROI is scaled to this")
    g.add_argument("--conf", type=float, default=0.35)
    g.add_argument("--roi-top", type=float, default=0.10,
                   help="crop away everything above this fraction (HUD banner)")
    g.add_argument("--roi-bottom", type=float, default=0.72,
                   help="crop away everything below this fraction (hood, gauges)")
    g.add_argument("--min-car-width", type=float, default=0.026,
                   help="ignore boxes narrower than this fraction of frame width")
    g.add_argument("--max-car-width", type=float, default=0.35,
                   help="ignore boxes wider than this (own car, HUD furniture)")
    g.add_argument("--min-person-width", type=float, default=0.008,
                   help="ignore people narrower than this fraction of frame width")

    g = p.add_argument_group("tracking")
    g.add_argument("--min-track-sec", type=float, default=1.5,
                   help="discard tracks shorter than this (scenery, parked cars)")
    g.add_argument("--min-hits", type=int, default=3)
    g.add_argument("--track-max-age", type=float, default=1.0,
                   help="seconds a track survives without a detection")

    g = p.add_argument_group("selection")
    g.add_argument("--max-cut", type=float, default=None,
                   help="longest a single cut may grow to (default 3x min_cut)")
    g.add_argument("--stride", type=float, default=1.0,
                   help="granularity of candidate window search, seconds")
    g.add_argument("--extend-frac", type=float, default=0.6,
                   help="grow a cut while neighbouring score is this fraction "
                        "of the seed score")
    g.add_argument("--min-gap", type=float, default=2.0,
                   help="minimum gap between two cuts before they are merged")
    g.add_argument("--bridge-gap", type=float, default=3.0,
                   help="if the jump left between two finished cuts is this "
                        "short or shorter, keep the footage between them "
                        "instead of cutting it out (0 disables)")
    g.add_argument("--pad-start", type=float, default=1.0,
                   help="seconds of lead-in kept before each interesting "
                        "segment")
    g.add_argument("--pad-end", type=float, default=1.0,
                   help="seconds of run-out kept after each interesting "
                        "segment")
    g.add_argument("--pad", type=float, default=None,
                   help="shorthand setting both --pad-start and --pad-end")
    g.add_argument("--pit-person-frac", type=float, default=0.08,
                   help="reject a window if people appear in more than this "
                        "fraction of its frames (pit lane, paddock, grid)")
    g.add_argument("--pit-smooth", type=float, default=20.0,
                   help="seconds over which person sightings are averaged "
                        "before a region counts as pits")
    g.add_argument("--pit-dilate", type=float, default=45.0,
                   help="seconds either side of a pit region also rejected, "
                        "covering the slow lap in and out")
    g.add_argument("--no-pit-filter", action="store_true",
                   help="keep pit and paddock footage")
    g.add_argument("--lap-roi", default="0.28,0.010,0.42,0.075",
                   help="x0,y0,x1,y1 fractions of the frame holding the lap "
                        "counter, whose absence marks a full course yellow")
    g.add_argument("--lap-min-frac", type=float, default=0.04,
                   help="fraction of the lap band that must be near-white for "
                        "the counter to count as drawn; an empty banner still "
                        "reads a little above zero from its own edge")
    g.add_argument("--yellow-frac", type=float, default=0.4,
                   help="treat footage as under yellow when the lap counter "
                        "is missing from more than this fraction of frames")
    g.add_argument("--yellow-smooth", type=float, default=6.0,
                   help="seconds over which the lap counter is averaged "
                        "before that test is applied")
    g.add_argument("--no-yellow-filter", action="store_true",
                   help="keep footage with no lap counter, i.e. yellow-flag "
                        "laps and the opening laps before the HUD acquires")

    g = p.add_argument_group("performance / io")
    g.add_argument("--threads", type=int, default=os.cpu_count())
    g.add_argument("--ffmpeg-threads", type=int, default=0)
    g.add_argument("--batch", type=int, default=16)
    g.add_argument("--jobs", type=int, default=4, help="parallel encodes")
    g.add_argument("--crf", type=int, default=18)
    g.add_argument("--preset", default="veryfast")
    g.add_argument("--start", type=float, default=0.0,
                   help="analyse from here, as a position on the combined "
                        "timeline of all inputs")
    g.add_argument("--end", type=float, default=None,
                   help="analyse up to here, on the combined timeline")
    g.add_argument("--cache-dir", default=".racecut-cache")
    g.add_argument("--no-cache", action="store_true")
    g.add_argument("--analyse-only", action="store_true",
                   help="run detection and report segments, write no video")
    g.add_argument("--dump-scores", default=None,
                   help="write per-window scores to this CSV")

    args = p.parse_args()
    args.inputs, args.min_cut, args.target_len = split_positionals(args.args, p)
    if args.max_cut is None:
        args.max_cut = 3 * args.min_cut
    if args.pad is not None:
        args.pad_start = args.pad_end = args.pad
    if args.pad_start < 0 or args.pad_end < 0:
        p.error("padding must not be negative")
    if args.bridge_gap < 0:
        p.error("--bridge-gap must not be negative")
    if not 0.0 <= args.battle_ratio <= 1.0:
        p.error("--battle-ratio must be between 0 and 1")
    if not 0.0 <= args.speed_weight <= 1.0:
        p.error("--speed-weight must be between 0 and 1")
    if not 0.0 <= args.closeness_weight <= 1.0:
        p.error("--closeness-weight must be between 0 and 1")
    if not 0.0 < args.closeness_ref <= 1.0:
        p.error("--closeness-ref must be above 0 and at most 1")
    if not 0.0 <= args.delta_weight <= 1.0:
        p.error("--delta-weight must be between 0 and 1")
    if args.delta_ref <= 0:
        p.error("--delta-ref must be above 0")
    if args.delta_hold < 0:
        p.error("--delta-hold must not be negative")
    if not 0.0 < args.yellow_frac <= 1.0:
        p.error("--yellow-frac must be above 0 and at most 1")

    def parse_roi(name):
        raw = getattr(args, name)
        try:
            roi = tuple(float(v) for v in raw.split(","))
            if len(roi) != 4:
                raise ValueError
            x0, y0, x1, y1 = roi
            if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1):
                raise ValueError
        except ValueError:
            p.error(f"--{name.replace('_', '-')} must be x0,y0,x1,y1 "
                    f"fractions with x0<x1 and y0<y1, got {raw!r}")
        setattr(args, name, roi)

    for _roi in ("speed_roi", "lap_roi", "delta_roi"):
        parse_roi(_roi)

    # Lay the inputs end to end on one timeline, in the order given.
    clips = []
    offset = 0.0
    for path in args.inputs:
        info = probe(path)
        clips.append({
            "path": path, "name": os.path.basename(path), "info": info,
            "offset": offset, "duration": info["duration"],
        })
        offset += info["duration"]
    total = offset

    for c in clips:
        i = c["info"]
        print(f"[src] {c['name']}: {i['width']}x{i['height']} "
              f"{i['fps']:.2f}fps {hhmmss(i['duration'])}")
    if len(clips) > 1:
        print(f"[src] {len(clips)} inputs, {hhmmss(total)} combined")

    if args.end is not None and args.end <= args.start:
        p.error("--end must be after --start")

    os.makedirs(args.cache_dir, exist_ok=True)
    analysed = 0
    for c in clips:
        # Clip the requested global range down to this clip's own span.
        lo = max(0.0, args.start - c["offset"])
        hi = c["duration"] if args.end is None else min(
            c["duration"], args.end - c["offset"])
        if hi <= lo:
            c["data"] = None
            continue
        end = None if hi >= c["duration"] else hi

        cp = cache_path(args, c["path"], lo, end)
        if not args.no_cache and os.path.exists(cp):
            print(f"[cache] {c['name']}: reusing {cp}")
            c["data"] = json.load(open(cp))
        else:
            c["data"] = analyse(args, c["path"], c["info"], lo, end)
            if not args.no_cache:
                json.dump(c["data"], open(cp, "w"))
                print(f"[cache] {c['name']}: wrote {cp}")
        analysed += 1

    clips = [c for c in clips if c.get("data")]
    if not clips:
        print("nothing to analyse; check --start and --end against the "
              "combined length of the inputs")
        return 1

    segs, (starts, tn, bn, clip_of) = select(clips, args)

    if args.dump_scores:
        with open(args.dump_scores, "w") as f:
            f.write("clip,global_start,traffic,battle\n")
            for s, a, b, ci in zip(starts, tn, bn, clip_of):
                f.write(f"{clips[ci]['name']},{s:.2f},{a:.4f},{b:.4f}\n")
        print(f"[scores] wrote {args.dump_scores}")

    used = sum(s["end"] - s["start"] for s in segs)
    print(f"\n[select] {len(segs)} cuts, {used / 60:.2f} min total "
          f"(target {args.target_len / 60:.2f} min)")
    multi = len(clips) > 1
    for s in segs:
        label = f"{s['name']}  " if multi else ""
        print(f"  {label}{hhmmss(s['start'])} -> {hhmmss(s['end'])}  "
              f"{s['end'] - s['start']:6.1f}s  {s['kind']}")
    if multi:
        for ci, c in enumerate(clips):
            picked = [s for s in segs if s["clip"] == ci]
            d = sum(s["end"] - s["start"] for s in picked)
            print(f"  {c['name']}: {len(picked)} cuts, {d / 60:.2f} min")

    if not segs:
        print("nothing selected. If this footage is mostly pit lane or "
              "paddock, try --no-pit-filter; otherwise try lowering --conf "
              "or --min-track-sec")
        return 1
    if args.analyse_only:
        return 0

    cut(args, segs, clips)
    return 0


if __name__ == "__main__":
    sys.exit(main())
