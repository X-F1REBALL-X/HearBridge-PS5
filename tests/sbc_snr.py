#!/usr/bin/env python3
"""SNR of decoded SBC (s16le stereo) vs reference PCM, best integer delay."""
import sys, numpy as np
ref = np.fromfile(sys.argv[1], '<i2').astype(float).reshape(-1, 2)
dec = np.fromfile(sys.argv[2], '<i2').astype(float).reshape(-1, 2)
best = None
for d in range(0, 400):
    n = min(len(ref), len(dec) - d) - 2000
    r = ref[1000:n]; x = dec[1000 + d:n + d]
    err = np.sum((r - x) ** 2)
    if best is None or err < best[1]: best = (d, err, r, x)
d, err, r, x = best
for ch in range(2):
    snr = 10 * np.log10(np.sum(r[:, ch] ** 2) / np.sum((r[:, ch] - x[:, ch]) ** 2))
    gain = np.dot(r[:, ch], x[:, ch]) / np.dot(r[:, ch], r[:, ch])
    print(f"ch{ch}: delay={d} SNR={snr:.2f} dB gain={gain:.4f}")
