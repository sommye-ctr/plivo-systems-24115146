# Protocol design — decisions and supporting data

This captures the baseline measurements we gathered and the design
decisions made from them, before writing any real sender/receiver code.

## Baseline sweep — naive forward-as-is code, no jitter buffer, no redundancy

Ran the given `sender.c`/`receiver.c` (unchanged) across both practice
profiles at several `--delay_ms` values, 30s duration each (1500 frames).

### Profile A (`loss=0.02, delay 10–40ms, dup=0.005`)

| delay_ms | dropped by network | deadline misses | miss %  | overhead |
|---------:|--------------------:|-----------------:|--------:|---------:|
| 40       | 34                   | 360               | 24.00%  | 1.02x    |
| 50       | 34                   | 37                | 2.47%   | 1.02x    |
| 70       | 34                   | 35                | 2.33%   | 1.02x    |
| 100      | 34                   | 34                | 2.27%   | 1.02x    |

### Profile B (`loss=0.05, delay 20–80ms, dup=0.01`)

| delay_ms | dropped by network | deadline misses | miss %  | overhead |
|---------:|--------------------:|-----------------:|--------:|---------:|
| 40       | 81                   | 1178              | 78.53%  | 1.02x    |
| 100      | 81                   | 82                | 5.47%   | 1.02x    |
| 150      | 81                   | 81                | 5.40%   | 1.02x    |

### What this tells us

- **Jitter is fully absorbed well before the deadline misses bottom out.**
  Profile A flattens to its floor by ~70ms of buffering; profile B by
  ~100–150ms. Past that point, adding more delay buys nothing — every
  remaining miss is a genuinely dropped packet, not a late one.
- **The loss floor alone already fails the 1% gate on both profiles**
  (2.27% on A, 5.4%–5.47% on B), even with *unlimited* buffering and
  *zero* jitter-related misses. This is the key finding: **buffering
  alone cannot make either profile valid.** We must recover lost
  packets, not just reorder late ones.
- Network-measured drop counts (34 on A, 81 on B) line up almost
  exactly with the configured `loss` field in each profile (0.02 and
  0.05), confirming the loss model is simple iid Bernoulli in these two
  practice profiles — but the relay code also supports Gilbert-Elliott
  burst loss (`burst_loss` key), which A/B don't exercise. Since grading
  uses **unseen profiles**, we should not assume losses stay isolated —
  the real design needs to survive short bursts too, not just scattered
  single drops.
- Overhead is a non-issue for the naive baseline (1.02x, essentially the
  4-byte seq header over 160-byte payload) — meaning our full 2.0x
  budget is currently unused. That budget exists specifically to pay for
  loss recovery.

## Goals (priority order)

1. **Valid first**: miss rate ≤ 1% and overhead ≤ 2.0x are hard gates.
   Fail either and the run isn't scored at all.
2. **Then minimize delay_ms**: among valid runs, lowest added playout
   delay wins. Overhead only breaks ties.
3. **Generalize**: grading profiles are unseen. Tune the *mechanism*
   (how loss/jitter are handled), not magic numbers fitted to A and B.

## Problems to solve (three separate ones, not one)

1. **Reordering / jitter** — frames arrive out of a steady 20ms cadence.
   Fixed by holding frames in a receiver-side buffer and releasing them
   on a fixed schedule instead of forwarding immediately.
2. **True loss** — a frame never arrives. Buffering cannot fix this; the
   measurements above prove it directly (the loss floor is well above
   the 1% cap on both profiles). Needs either advance redundancy or a
   resend.
3. **Duplicates** — same frame delivered twice (from network dup, or
   from our own redundancy scheme). Needs dedup bookkeeping so a
   duplicate never wastes recovery budget or confuses the buffer.

Underlying constraint: **payload bytes are incompressible** (SHA256-derived,
indistinguishable from random to us), so any redundancy we send costs its
full 160 bytes — no compression trick is available.

## Design decisions

### 1. Loss recovery: proactive redundancy (FEC-style), not retransmission

A NACK-based resend requires the request to cross the hostile relay to
the sender *and* the resend to cross it back — each leg independently
pays that profile's full delay range (10–80ms) and carries its own
independent loss/dup risk. A single retransmit round trip can cost
20–160ms, which directly fights the thing we're scored on (minimizing
added delay). Since our score is "how little delay can you add," burning
delay budget on round trips is close to self-defeating.

**Decision: recover loss with redundancy sent ahead of time, no round
trip required.** This turns "did this frame survive" into "did the
frame *and* its independently-timed redundant copy both get unlucky,"
which for iid loss at 2–5% is roughly `p²` (~0.04%–0.25%) — comfortably
under the 1% cap — at zero *additional* delay cost, only bandwidth cost.

### 2. Redundancy shape: interleaved piggyback, not adjacent-pair or naive full duplicate

Two candidates considered:
- Full duplicate of every frame → close to 2.0x all by itself, no
  margin left for headers or a feedback channel.
- XOR parity over small groups (e.g. 4 data + 1 parity) → cheap (~25%
  overhead) but a second loss in the same group defeats it entirely.

**Decision: piggyback a copy of an *earlier, non-adjacent* frame's
payload onto each outgoing packet** (e.g. frame i carries frame i−2 or
i−3's payload, not i−1's), at a controlled overhead target of roughly
1.5–1.7x (leaving headroom under the 2.0x cap for headers and any
feedback traffic). The interleave (skipping rather than adjacent-copy)
is specifically to survive a **burst of 2 consecutive losses** — since
grading profiles are unseen and the relay supports burst loss even
though A/B don't use it, we should not design only for scattered iid
loss.

### 3. Jitter buffer size: measure, don't guess

The sweep above already shows the *shape* of the answer: buffer past
the point where miss rate stops dropping and you're just adding pure
delay for no benefit (profile A flattens ~70ms, profile B ~100–150ms
in the *naive, no-recovery* baseline). Once redundancy is added, delay
only needs to cover jitter, not jitter + a recovery round trip — this
is exactly what a round-trip-free FEC design buys us versus ARQ.

**Decision: start generous (150–200ms) to reach valid, then shrink
delay per-profile toward just above the observed jitter tail, one
change per run, logged in RUNLOG.md.**

### 4. Duplicate/reorder bookkeeping on the receiver

**Decision:** a bounded map/window keyed by seq (bounded to the current
buffer's time window, not unbounded) marking "already delivered to
player." Both network-level dup and our own redundant copies get
deduped through the same structure — first arrival wins.

### 5. Threading/ownership (C++)

- **Sender**: one thread doing `recvfrom` on 47010 → immediately builds
  the outgoing packet (current payload + interleaved redundant payload
  pulled from a small ring buffer of recent frames) → sends without
  batching delay, since the harness already paces us at 20ms.
- **Receiver**: one thread doing non-blocking `recvfrom` on 47002 into a
  mutex-protected buffer keyed by seq (plain `std::mutex` is fine at
  20ms cadence — lock-free would be premature here); a second thread
  acting as the playout clock, waking precisely at each frame's
  deadline and emitting whatever's available (primary or redundant
  copy) to 47020, marking it missed otherwise.
- **Rule from the handout's own hints**: the socket-reading thread must
  never block on anything but the socket itself — no sleeping, no
  waiting on the playout clock in that same thread — or ingestion
  stalls at exactly the wrong moment.

## Open items still to decide

- Exact interleave distance (i−2 vs i−3 vs adaptive) and exact
  redundancy overhead target, once we can measure real miss rates with
  a working implementation instead of reasoning from the naive baseline.
- Whether a lightweight best-effort NACK backstop is worth adding for
  rare catastrophic gaps (e.g. burst losses beyond what the interleave
  covers), *only* if the chosen delay budget leaves enough room for one
  round trip without threatening the 1% cap — not a primary mechanism.
- Exact wire header layout (seq + which prior frame's payload is
  attached + payload).
