# V3 Protocol Changes

Status: **LOCKED — final protocol version.** v3 replaces v2's per-packet
1-frame-lag XOR with block parity (K=2), cutting bandwidth overhead from
~2.0x to 1.54x, and removes the receiver's playout clock so frames are
forwarded the instant they are ready. That second change eliminated the
run-to-run instability on profile B that was straddling the 1% cap, so a
submission `delay_ms` can now be locked in. See `V3_RESULTS.md`.

## Why v2 needed another pass

v2 (`Payload(N) ⊕ Payload(N-1)` attached to 34/35 packets) was VALID on
both profiles but paid for it at **1.9963x overhead** — essentially every
packet carried a full duplicate-sized parity payload, leaving only ~0.4%
headroom under the 2.0x cap (see `V2_CHANGES.md`). The ask was to cut
overhead meaningfully and lock a single `delay_ms` for grading.

## v3 design: block parity

Instead of piggybacking parity onto (almost) every data packet, group `K`
consecutive data frames into a block and send **one separate parity
packet per block** (`XOR` of all `K` frames), rather than one parity per
frame:

- **Wire format**: 4B header (bit31 = `is_parity`, bits0–30 = `seq` for
  data packets or `block_id` for parity packets) + 160B payload. Every
  packet — data or parity — is a flat 164B; there's no more variable
  164B/324B split from v2.
- **Sender** (`sender.cpp`): keeps a running XOR accumulator for the
  current block; resets it every `K`th frame, sends the data packet
  immediately as before, and emits one extra parity packet right after
  the last frame in each block (`seq % K == K-1`).
- **Receiver** (`receiver.cpp`): **no playout clock — forwards on
  arrival.** The harness player (`endpoints.py`) judges frame `i` solely
  on whether a correct-seq packet reaches it before `t0 + delay_ms +
  i*20ms`, dedups by seq itself, and never penalises an early arrival. So
  the receiver forwards every data frame — and every parity
  reconstruction — to the player the instant it is ready, in a single
  recv loop, and lets the player enforce the deadline. Block recovery is
  unchanged in spirit: a `BlockState` per block-id (got[K] bitmap, partial
  XOR, parity payload/flag) sized as its own ring (`block_capacity`); on
  every arrival it updates the block and calls `try_reconstruct` (parity
  in + exactly `K-1` data frames in ⇒ XOR the missing one back out and
  forward it via the same `forward()` path as a normal arrival). A
  retirement horizon `floor_seq = max_seq_seen - window` drops frames and
  blocks whose deadline has certainly passed, so a late/duplicate packet
  can't clobber a slot reused by a newer block sharing the same
  `block_id % block_capacity` index. Best-effort seq dedup avoids
  double-forwarding and wasted reconstruction (the player also dedups).
- **Why the playout clock was dropped (the v3 stability fix).** v2 kept a
  ring-buffer + `sleep_until` playout loop (10ms lead) that held each
  frame back to ~its deadline before emitting. Because the player only
  cares about *arrival before deadline*, that buffering bought nothing and
  the `sleep_until` wake was itself subject to OS scheduling jitter — the
  exact "same-seed, different miss rate" variance seen below. Forwarding
  on arrival removes that self-inflicted jitter entirely.

Overhead now scales as roughly `(K+1)/K × (164/160)` instead of ~2x,
since redundancy is amortized over `K` frames instead of sent on
(almost) every one.

## Choosing K (block-size sweep)

This sweep picked the block size. It was taken under the old playout-clock
receiver, so the K=2 B figure shows the pre-fix straddle; the stable
post-fix numbers are in the stability section below and in `V3_RESULTS.md`.

| K | overhead (measured) | Profile A @120ms | Profile B @120ms |
|---:|---:|---|---|
| 4 | 1.28x | 0.27% miss — VALID | 3.60% miss — **INVALID** |
| 2 | 1.54x | 0.27% miss — VALID | 0.87–1.00% miss — straddled the cap (pre-fix) |

K=4 recovers too little for B's 5% loss rate (a block spans 80ms, wide
enough that a second loss in the same block — not rare at 5% — defeats
the single-parity recovery), so **K=2 is the locked choice**. K=2 looked
borderline on B here only because of the playout-clock jitter, which the
forward-on-arrival change (below) removed.

### Stability problem found on profile B — RESOLVED

Repeating the **identical** `--profile profiles/B.json --seed 1` run
(same seed ⇒ identical drop/dup pattern from the relay) at K=2 originally
produced different miss rates across runs:

- 120ms: 0.87%, then 1.00% (exactly at the cap) on a repeat.
- 140ms: 1.40% (**INVALID**), then 0.93%, then 0.93% across three repeats.

Since the seed is fixed, the *loss pattern* is identical every run, so the
variance came from OS/process scheduling jitter — specifically the
receiver's own `sleep_until` playout wake, not the network model.

**Root cause and fix.** The playout clock was the source: it deliberately
held each frame until ~its deadline before emitting, so any late wake
turned an otherwise-in-time frame into a miss. But the player scores on
arrival-before-deadline, so holding frames served no purpose. Dropping the
playout clock and forwarding on arrival removed the jitter. Re-measuring
the same `--seed 1` B run at K=2 (see `V3_RESULTS.md`):

- 120ms: 0.60%, 0.60% (identical across repeats — no variance).
- 110ms: 0.80% at 30s; 0.60%, 0.60% at 20s.
- 100ms: 0.93% at 30s — valid but the thin edge of the safe range.

The straddle is gone: runs are now repeatable and sit well under the cap.

## Locked decisions

- **K = 2, block parity.** Overhead 1.54x — comfortable headroom under the
  2.0x cap (vs v2's tight 1.9963x), which is insurance against an unseen
  grading profile harsher than B. K=4 (1.28x) recovers too little for B's
  5% loss; K=3 was not needed once the jitter fix reclaimed the margin
  that was making K=2 look borderline.
- **Receiver forwards on arrival — no playout clock.** This is the change
  that made K=2 comfortably valid on B; see above.
- **Submission `delay_ms`.** Profile B is the binding (worse) practice
  profile; its floor under v3 is ~100ms (0.93%, thin) with comfortable
  validity by 110–120ms (0.80–0.60%). Because grading profiles are unseen
  and may be harsher than B, the submitted value should carry margin above
  B's thin floor — **recommend 120ms** (0.60% on B, ample headroom, still
  trivially valid on the milder A). 110ms is a defensible lower-delay
  alternative if a better score is worth trading some unseen-profile
  margin.

## Residual risk (documented, accepted)

- **Bursts.** Adjacent K=2 block parity recovers only one loss per block,
  so a burst of 2 consecutive losses landing on both data frames of a
  block (frames `2m` and `2m+1`) is unrecoverable. A/B are iid and don't
  exercise this; the relay's Gilbert-Elliott `burst_loss` could. Adding
  burst tolerance means interleaving the block members (pairing frames a
  stride apart), which costs ~stride×20ms of extra delay — not taken,
  since it trades the primary score (delay) for a hypothetical unseen
  hazard.
