#!/usr/bin/env python3
"""
Generates 0001.mp3 (fuse -> ring ignition -> build-up) and 0002.mp3 (KABOOM + ghost wisp)
for the DFPlayer Mini. Timings mirror the constants in halloween-bang.ino - if you change
them in the sketch, change them here and re-run:   python3 sounds.py
Needs: numpy, scipy, ffmpeg.      Copy results to the ROOT of the SD card (FAT32).
"""
import subprocess, numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 44100
rng = np.random.default_rng(31)          # change the seed for a different crackle pattern

# ---- mirror of the sketch constants ------------------------------------
FUSE_LEN, FUSE_TAIL = 120, 14
FUSE_BURN_S   = 6.0
FAKEOUT       = True
FAKEOUT_AT    = 0.55
FAKEOUT_S     = 1.0
RING_IGNITE_S = 1.8
BUILDUP_S     = 2.0
KABOOM_TOTAL  = 3.8
SLIDE_WHISTLE = True                     # cartoon "wheeeee-pop" as the vampire vanishes

stall_t = FAKEOUT_AT ** 0.8 * FUSE_BURN_S
cross_t = (FUSE_LEN / (FUSE_LEN + FUSE_TAIL)) ** 0.8 * FUSE_BURN_S + (FAKEOUT_S if FAKEOUT else 0)
build_t = cross_t + RING_IGNITE_S           # build-up starts
end1    = build_t + BUILDUP_S               # sound 1 length (matches the sketch's wait)

# ---- helpers -----------------------------------------------------------
def tt(sec): return np.arange(int(sec * SR)) / SR
def noise(n): return rng.standard_normal(n)
def band(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype='band', fs=SR, output='sos'), x)
def hp(x, f): return sosfilt(butter(2, f, btype='high', fs=SR, output='sos'), x)
def lp(x, f): return sosfilt(butter(2, f, btype='low', fs=SR, output='sos'), x)
def smooth(x, hz=30): return lp(x, hz)

def sweep_noise(t, f0, f1, width=0.5):
    """noise whose band centre sweeps f0 -> f1 (log) over the time axis (5 crossfaded bands)"""
    n = len(t); x = noise(n); out = np.zeros(n)
    centres = np.geomspace(f0, f1, 6)
    pos = np.linspace(0, 1, 6)
    for c, p in zip(centres, pos):
        env = np.exp(-((np.linspace(0, 1, n) - p) ** 2) / (2 * (width / 4) ** 2))
        out += band(x, c * 0.6, min(c * 1.6, SR / 2 - 100)) * env
    return out

def crackle(n, rate, amp=1.0, tone=(2500, 9000), length=0.006):
    """random fire/spark pops. rate: array of pops/second per sample."""
    out = np.zeros(n)
    hits = np.where(rng.random(n) < rate / SR)[0]
    for h in hits:
        L = int(length * SR * (0.4 + rng.random() * 1.6))
        if h + L >= n: continue
        env = np.exp(-np.arange(L) / (L / 4.0))
        b = noise(L) * env
        if rng.random() < 0.5: b = hp(b, tone[0])
        out[h:h + L] += b * amp * (0.2 + rng.random() * 0.8) * (amp_at(rate, h))
    return out
def amp_at(rate, h): return 1.0

def tone_fm(f, amp, harmonics=((1, 1.0),)):
    ph = 2 * np.pi * np.cumsum(f) / SR
    return sum(a * np.sin(h * ph) for h, a in harmonics) * amp

def reverb(x, sec=1.4, wet=0.3):
    n = int(sec * SR)
    ir = noise(n) * np.exp(-np.arange(n) / (n / 5.0))
    ir = lp(ir, 3500); ir[0] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    y = fftconvolve(x, ir)[:len(x)]
    return x * (1 - wet) + y * wet * 1.3

def finish(x, peak=0.89, fade=0.03):
    x = np.tanh(x * 1.1)
    x = x / np.max(np.abs(x)) * peak
    f = int(fade * SR); x[-f:] *= np.linspace(1, 0, f)
    return x

def export(x, name):
    pcm = (x * 32767).astype('<i2').tobytes()
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 's16le', '-ar', str(SR), '-ac', '1',
                    '-i', '-', '-codec:a', 'libmp3lame', '-b:a', '192k', name], input=pcm, check=True)

# =========================================================================
#  SOUND 1 : sizzling fuse -> fake-out -> flare -> ring whoosh -> riser
# =========================================================================
def sound1():
    t = tt(end1); n = len(t)
    te = np.where(t < stall_t, t, np.where(t < stall_t + FAKEOUT_S, stall_t, t - FAKEOUT_S)) if FAKEOUT else t
    prog = np.clip(te / FUSE_BURN_S, 0, 1)
    fuse_on = (t < cross_t + 0.25).astype(float)
    fuse_on = smooth(fuse_on, 8)
    # fake-out dip
    frac = np.clip((t - stall_t) / FAKEOUT_S, 0, 1)
    dip = np.where((t >= stall_t) & (t < stall_t + FAKEOUT_S), np.clip(1 - frac * 1.2, 0, 1) ** 2, 1.0) if FAKEOUT else np.ones(n)
    dip = smooth(dip, 25)

    # --- sizzle: hissing noise gets louder/brighter as the spark speeds up
    hiss = hp(noise(n), 2500) * (0.10 + 0.40 * prog ** 1.5)
    hiss += band(noise(n), 700, 2200) * (0.05 + 0.15 * prog)
    flutter = 1 + 0.35 * smooth(noise(n), 12) * 6
    s1 = hiss * flutter * dip * fuse_on

    # --- crackling sparks: more and more often
    rate = (8 + 70 * prog) * dip * fuse_on
    s1 += crackle(n, rate, amp=0.9) * 1.0

    # --- sputter pops while the spark is dying
    if FAKEOUT:
        for k in (0.45, 0.62, 0.78, 0.88):
            i = int((stall_t + k * FAKEOUT_S) * SR); L = int(0.07 * SR)
            env = np.exp(-np.arange(L) / (L / 5.0)) * (1 - np.arange(L) / L)
            s1[i:i + L] += band(noise(L), 500, 3500) * env * 0.5
        # --- re-ignite flare
        i = int((stall_t + FAKEOUT_S) * SR); L = int(0.55 * SR)
        tf = np.arange(L) / SR
        fl = sweep_noise(tf, 400, 7000, 0.7) * np.minimum(tf * 120, 1) * np.exp(-tf / 0.18)
        s1[i:i + L] += fl * 0.9
        s1[i:i + int(0.03 * SR)] += hp(noise(int(0.03 * SR)), 1500) * 0.8

    # --- ring ignition: low fire roar swelling + swept whoosh
    ign = np.clip((t - cross_t) / RING_IGNITE_S, 0, 1)
    build = np.clip((t - build_t) / BUILDUP_S, 0, 1)
    ring_on = smooth((t >= cross_t - 0.02).astype(float), 40)
    roar_amp = ring_on * (0.15 + 0.55 * ign + 0.55 * build)
    roar = lp(noise(n), 900) * (1 + 0.5 * smooth(noise(n), 18) * 5) * roar_amp
    roar += band(noise(n), 1500, 5000) * ring_on * (0.05 + 0.2 * ign + 0.25 * build)
    s1 += roar * 1.6
    i = int((cross_t - 0.05) * SR); L = int((RING_IGNITE_S + 0.3) * SR)
    tw = np.arange(L) / SR
    w = sweep_noise(tw, 150, 5500, 0.9) * np.sin(np.pi * np.clip(tw / (RING_IGNITE_S + 0.3), 0, 1)) ** 1.5
    s1[i:i + L] += w * 1.6
    s1 += crackle(n, ring_on * (50 + 90 * build), amp=1.1, length=0.009)

    # --- build-up: rising riser + accelerating dynamite ticks + low drone
    k = np.clip((t - build_t) / (BUILDUP_S - 0.25), 0, 1)       # riser peaks 0.25s before the end
    on = ((t >= build_t) & (t < end1 - 0.25)).astype(float)
    on = smooth(on, 60)
    f = 90 * (900 / 90) ** (k ** 1.4)                                    # 90 Hz -> 900 Hz
    trem_hz = 4 + 20 * k ** 1.6
    trem = 0.55 + 0.45 * np.sin(2 * np.pi * np.cumsum(trem_hz) / SR)
    riser = tone_fm(f, 1.0, ((1, 1.0), (2, 0.5), (3, 0.33), (4, 0.25))) * (0.12 + 0.35 * k ** 1.5) * trem * on
    drone = np.sin(2 * np.pi * 55 * t) * 0.25 * k * on + np.sin(2 * np.pi * 82.4 * t) * 0.12 * k * on
    s1 += lp(riser, 4000) + drone
    # ticks (like a cartoon time bomb) - interval shrinks 0.42s -> 0.06s
    tk = build_t; dt = 0.42
    while tk < end1 - 0.28:
        i = int(tk * SR); L = int(0.03 * SR)
        e = np.exp(-np.arange(L) / (L / 6.0))
        s1[i:i + L] += np.sin(2 * np.pi * 1700 * np.arange(L) / SR) * e * 0.55
        tk += dt; dt = max(0.06, dt * 0.88)

    # --- breath-hold: everything cuts 0.25s before the end (KABOOM follows)
    cut = np.ones(n); j = int((end1 - 0.25) * SR)
    cut[j:] = 0; cut[j - int(0.015 * SR):j] = np.linspace(1, 0, int(0.015 * SR))
    s1 *= cut
    return finish(reverb(s1, 0.5, 0.12))

# =========================================================================
#  SOUND 2 : KABOOM -> debris -> (slide-whistle vanish) -> ghost wisp
# =========================================================================
def sound2():
    L2 = KABOOM_TOTAL + 0.2
    t = tt(L2); n = len(t)
    # sub boom (pitch drops fast)
    fb = 26 + 90 * np.exp(-t / 0.10)
    boom = tone_fm(fb, 1.0) * np.exp(-t / 0.85) * np.minimum(t * 2000, 1) * 1.6
    boom += tone_fm(fb * 2.01, 1.0) * np.exp(-t / 0.35) * 0.5
    # blast noise, cutoff falling from bright to dull (via 6 crossfaded low bands)
    x = noise(n)
    blast = np.zeros(n)
    for i, c in enumerate([9000, 4000, 1800, 800, 350, 160]):
        env = np.exp(-((t - i * 0.22) ** 2) / (2 * 0.2 ** 2))
        blast += lp(x, c) * env * (1 + 0.3 * i)
    blast *= np.exp(-t / 0.8) * np.minimum(t * 3000, 1) * 1.1
    crack = hp(noise(n), 1200) * np.exp(-t / 0.035) * 1.6                 # initial crack
    # debris rain
    rate = 120 * np.exp(-np.clip(t - 0.1, 0, None) / 0.9) * (t > 0.1)
    debris = crackle(n, rate, amp=1.3, tone=(900, 6000), length=0.012)
    # rolling rumble tail
    rumble = lp(noise(n), 160) * np.exp(-t / 1.4) * (1 + 0.5 * smooth(noise(n), 6) * 8) * 0.9
    boomy = reverb(boom + blast + crack + rumble, 1.8, 0.35) + debris * 0.6

    # ghost wisp (theremin-ish, gliding down, vibrato, + shimmering bells) from 0.6s
    tw = np.clip(t - 0.6, 0, None)
    wfade = np.clip(tw / 0.6, 0, 1) * np.clip((KABOOM_TOTAL - t) / 1.2, 0, 1) * (t > 0.6)
    f1 = (520 - 220 * np.clip(tw / 3.0, 0, 1)) * (1 + 0.012 * np.sin(2 * np.pi * 5.2 * tw))
    wisp = tone_fm(f1, 1.0, ((1, 1.0), (2, 0.18))) + tone_fm(f1 * 1.498, 0.6) + tone_fm(f1 * 0.5, 0.5)
    wisp *= wfade * 0.16
    wisp += band(noise(n), 1500, 3500) * wfade * 0.03 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.9 * tw))
    for bt in (0.75, 1.05, 1.45, 1.8, 2.3, 2.7, 3.1):
        i = int(bt * SR); Lb = int(1.0 * SR)
        if i + Lb > n: Lb = n - i
        tb = np.arange(Lb) / SR; fq = rng.choice([1319, 1568, 1760, 2093, 2637])
        wisp[i:i + Lb] += (np.sin(2 * np.pi * fq * tb) + 0.4 * np.sin(2 * np.pi * fq * 2.76 * tb)) \
                          * np.exp(-tb / 0.35) * 0.05
    wisp = reverb(wisp, 1.6, 0.45)

    out = boomy + wisp

    # cartoon vanish: slide-whistle falling, ends in a "pop"
    if SLIDE_WHISTLE:
        a, b = 0.35, 1.55
        i = int(a * SR); Lw = int((b - a) * SR); tw2 = np.arange(Lw) / SR; k = tw2 / (b - a)
        fw = 2200 * (230 / 2200) ** k * (1 + 0.015 * np.sin(2 * np.pi * 7 * tw2))
        env = np.minimum(tw2 * 25, 1) * np.minimum((b - a - tw2) * 40, 1)
        wh = tone_fm(fw, 1.0, ((1, 1.0), (2, 0.12))) * env * 0.22
        out[i:i + Lw] += wh
        j = int(b * SR); Lp = int(0.12 * SR); tp = np.arange(Lp) / SR
        pop = np.sin(2 * np.pi * (300 + 900 * np.exp(-tp / 0.015)) * tp) * np.exp(-tp / 0.03)
        out[j:j + Lp] += pop * 0.5 + hp(noise(Lp), 1500) * np.exp(-tp / 0.01) * 0.25

    return finish(out)

if __name__ == '__main__':
    s1, s2 = sound1(), sound2()
    export(s1, '0001.mp3'); export(s2, '0002.mp3')
    print('0001.mp3  %.2fs  (sketch: fuse crosses at %.2fs, ring ignites to %.2fs, build-up ends %.2fs)'
          % (len(s1) / SR, cross_t, build_t, end1))
    print('0002.mp3  %.2fs  (sketch KABOOM animation: %.1fs)' % (len(s2) / SR, KABOOM_TOTAL))
