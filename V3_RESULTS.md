# V3 Benchmark Results

v3 = block parity (K=2) + **receiver forwards on arrival (no playout
clock)**. Overhead is fixed by our send pattern at **1.54x** (369000B up /
240000B raw over 30s; 0B feedback) on every run, independent of profile or
delay. All runs `--profile profiles/B.json --seed 1` unless noted. Profile
B is the binding (worse) practice profile, so the sweep focuses there.

## The fix: same-seed repeatability

Before (v2-style playout clock, held frames to ~deadline) the identical
`--seed 1` B run gave different miss rates — the OS scheduling jitter of
`sleep_until`, not the network:

```
B @ 120ms, playout clock:  1.07% (INVALID),  1.00%,  1.00%   <- straddling the cap
```

After (forward on arrival) the same run is stable and well under the cap:

```
B @ 120ms, forward-on-arrival:  0.60%,  0.60%   (no run-to-run variance)
```

## Profile B sweep (v3, forward-on-arrival)

| delay_ms | duration | deadline misses | miss %  | overhead | result |
|---------:|---------:|----------------:|--------:|---------:|--------|
| 120      | 20s      | 6 / 6           | 0.60%   | 1.54x    | VALID  |
| 110      | 30s      | 12              | 0.80%   | 1.54x    | VALID  |
| 110      | 20s      | 6 / 6           | 0.60%   | 1.54x    | VALID  |
| 100      | 30s      | 14              | 0.93%   | 1.54x    | VALID  |
| 100      | 20s      | 9               | 0.90%   | 1.54x    | VALID  |

(Two entries in a "misses" cell are repeat runs of the identical seed.)

- **Floor on B is ~100ms** (0.93%, thin), comfortably valid by 110–120ms.
- Overhead is a non-issue: 1.54x on every run, ~0.46x of headroom under
  the 2.0x cap.

## Comparison to v2

| version | mechanism | B lowest valid delay | overhead | same-seed stability |
|---------|-----------|---------------------:|---------:|---------------------|
| v2      | 1-frame XOR + playout clock | 120ms (0.87%) | 1.9963x | straddled the cap (0.87–1.40%) |
| v3      | K=2 block parity + forward-on-arrival | ~100–110ms | 1.54x | stable (0.60–0.93%) |

v3 wins on all three axes that matter: it validates at **lower delay** (the
score), at **lower overhead** (the tiebreaker, and with real safety
margin), and — most importantly — it is **repeatable**, so a graded run
won't lose to an unlucky scheduling slice.

## Locked submission value

**`delay_ms = 120`** for grading: 0.60% on B with ample headroom, trivially
valid on the milder A, and margin to spare against an unseen profile
harsher than B. 110ms is a lower-delay alternative (0.80% on B) if the
better score is judged worth trading some of that unseen-profile margin.

## Reproduce

```
cd systems_handout && make
python3 run.py --profile profiles/B.json --seed 1 --delay_ms 120 --duration 30
python3 run.py --profile profiles/A.json --seed 1 --delay_ms 120 --duration 30
```
