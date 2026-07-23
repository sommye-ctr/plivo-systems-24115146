# RUNLOG

Per-experiment log of the real-time UDP sender/receiver. Chronological,
newest section last. Each experiment: profile, `delay_ms`, deadline-miss %,
bandwidth overhead, and what changed and why. Gates: **miss ≤ 1.00%** and
**overhead ≤ 2.00x**; among valid runs, **lower `delay_ms` wins**, overhead
breaks ties. All runs use `--seed 1`, 30s (1500 frames) unless noted.

Source lives in `systems_handout/` (`make` → `./sender`, `./receiver`).
Reproduce any row: `cd systems_handout && python3 run.py --profile
profiles/<P>.json --delay_ms <D>`.

Profiles: **A** = mild (loss 0.02, delay 10–40ms, dup 0.005); **B** =
moderate (loss 0.05, delay 20–80ms, dup 0.01). B is the binding profile.

---

## 0. Baseline — naive forward-as-is (given `sender.c`/`receiver.c`)

No jitter buffer, no redundancy. Establishes the problem.

| profile | delay_ms | miss % | overhead | result |
|---------|---------:|-------:|---------:|--------|
| A | 40  | 24.00% | 1.02x | INVALID |
| A | 50  | 2.47%  | 1.02x | INVALID |
| A | 70  | 2.33%  | 1.02x | INVALID |
| A | 100 | 2.27%  | 1.02x | INVALID |
| B | 40  | 78.53% | 1.02x | INVALID |
| B | 100 | 5.47%  | 1.02x | INVALID |
| B | 150 | 5.40%  | 1.02x | INVALID |

**What / why:** Buffering absorbs jitter (A flattens by ~70ms, B by
~100–150ms) but the miss rate floors at the raw network loss rate (2.3% on
A, 5.4% on B) — both above the 1% cap. **Conclusion: buffering alone can
never pass; we must recover lost frames.** Decision: proactive redundancy
(FEC), not retransmission — a NACK round trip across the hostile relay
would cost 20–160ms of the very delay we are scored on.

## 1. v1 — piggyback frame N−3 onto packet N (interleaved duplicate)

Each packet carries an earlier frame's full payload as its recovery copy.

| profile | delay_ms | miss % | overhead | result |
|---------|---------:|-------:|---------:|--------|
| A | 40 | 34.87% | 1.78x | INVALID |
| A | 50 | 8.07%  | 1.78x | INVALID |
| A | 60 | 6.33%  | 1.78x | INVALID |
| B | 60 | 55.53% | 1.78x | INVALID |
| B | 80 | 24.93% | 1.78x | INVALID |
| B | 100| 11.00% | 1.78x | INVALID |

**What / why it failed:** the N−3 recovery copy arrives 60ms + transit
after the original, so recovered frames routinely miss their deadline;
25% of frames also had no redundancy (`seq % 4` gating). Lesson: recovery
latency must be ~one frame, not several. Move to XOR parity of *adjacent*
frames.

## 2. v2 — 1-frame XOR parity + receiver playout clock

Packet N carries `Payload(N) ⊕ Payload(N−1)` on 34/35 packets; receiver
runs a jitter buffer + `sleep_until` playout loop (10ms lead). Overhead is
fixed by our send pattern at 1.9963x (≈2.00x).

| profile | delay_ms | miss % | overhead | result |
|---------|---------:|-------:|---------:|--------|
| A | 60  | 4.47% | 2.00x | INVALID |
| A | 65  | 1.20% | 2.00x | INVALID |
| A | 70  | 0.93% | 2.00x | **VALID** |
| B | 110 | 1.33% | 2.00x | INVALID |
| B | 120 | 0.87% | 2.00x | **VALID** |

**What / why:** first valid design — adjacent XOR gives ~20ms recovery
latency, so recovered frames make their deadline. Two weaknesses: overhead
sits ~0.4% under the cap (no margin for an unseen profile), and repeat runs
straddled the cap on B (see v3).

## 3. v3 (pre-fix) — block parity, choosing K, still with playout clock

Group K data frames into a block, send one XOR parity packet per block.
Overhead ≈ `(K+1)/K × 164/160`. Sweep to pick K (playout clock still in
place).

| K | profile | delay_ms | miss % | overhead | result |
|---|---------|---------:|-------:|---------:|--------|
| 4 | A | 120 | 0.27% | 1.28x | VALID |
| 4 | B | 120 | 3.60% | 1.28x | INVALID |
| 2 | A | 120 | 0.27% | 1.54x | VALID |
| 2 | B | 120 | 0.87–1.00% | 1.54x | straddled cap |

Repeat-run instability on B at K=2 (identical `--seed 1`, so identical loss
pattern):

| profile | delay_ms | miss % across repeats | result |
|---------|---------:|-----------------------|--------|
| B | 120 | 1.07% / 1.00% / 1.00% | flaky (INVALID once) |
| B | 140 | 1.40% / 0.93% / 0.93% | flaky (INVALID once) |

**What / why:** K=4 recovers too little for B's 5% loss (a wider block
gets a second loss too often). K=2 is the right recovery strength and cuts
overhead to a comfortable 1.54x. But the same-seed variance proved misses
were coming from the receiver's own `sleep_until` wake jitter, not the
network — the playout clock was manufacturing misses.

## 4. v3 (FINAL, LOCKED) — block parity K=2 + forward-on-arrival

Removed the receiver playout clock. The harness player scores a frame on
whether a correct-seq packet arrives before its deadline (dedups by seq,
no penalty for early arrival), so the receiver forwards every data frame
and every parity reconstruction the instant it is ready. Overhead 1.54x.

| profile | delay_ms | duration | miss % | overhead | result |
|---------|---------:|---------:|-------:|---------:|--------|
| B | 120 | 20s | 0.60% / 0.60% | 1.54x | **VALID** (stable) |
| B | 110 | 30s | 0.80% | 1.54x | **VALID** |
| B | 110 | 20s | 0.60% / 0.60% | 1.54x | **VALID** (stable) |
| B | 100 | 30s | 0.93% | 1.54x | **VALID** (thin) |
| B | 100 | 20s | 0.90% | 1.54x | **VALID** |

**What / why:** dropping the playout clock removed the self-inflicted
scheduling jitter — the B straddle is gone and repeats are identical. K=2
block parity now validates on B at *lower* delay than v2 (~100–110ms vs
120ms) and at *lower* overhead (1.54x vs 2.00x), with real headroom on both
gates.

**Locked submission: `delay_ms = 120`** — 0.60% on B with wide margin,
trivially valid on the milder A, and margin against an unseen profile
harsher than B. (110ms is a lower-delay alternative at 0.80% on B if a
better score is worth spending some of that margin.)
