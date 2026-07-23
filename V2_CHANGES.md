# V2 Protocol Changes

Status: **Implemented and verified**. v2 replaces v1's delayed duplicate mechanism with a 1-frame short-window FEC scheme, achieving valid scores on both practice profiles (deadline miss rate $\le 1.0\%$, bandwidth overhead $\le 2.0\times$).

## Why v1 failed (from V1_RESULTS.md)

- **Redundancy was too slow.** piggybacking frame `N-3` onto packet `N` meant recovery copies were delayed by 60ms + transit time, causing 6%–56% deadline misses.
- **Coverage gaps.** 25% of frames had zero redundancy due to `seq % 4 != 0` gating.
- **High overhead.** 1.78x baseline left little margin before the 2.0x cap.

## Implemented v2 Design

1. **Short-Window 1-Frame FEC**:
   - Each packet $N \ge 1$ carries a 160-byte parity payload: $P_N = \text{Payload}(N) \oplus \text{Payload}(N-1)$.
   - If packet $N-1$ is dropped by the relay, receiving packet $N$ (only 20ms later) allows instant reconstruction of $\text{Payload}(N-1) = \text{Primary}(N) \oplus P_N$.
2. **Coverage & Overhead Tuning**:
   - Parity is attached to 34 out of every 35 packets (`seq % 35 != 0` for $N \ge 1$).
   - This provides ~97% frame loss recovery coverage while keeping bandwidth overhead at $1.996\times \le 2.00\times$.
3. **Wire Format**:
   - 4-byte big-endian header (`net_seq`).
     - Bit 31 (`0x80000000`): `has_fec` flag.
     - Bits 0–30: Sequence number (`seq`).
   - 160-byte Primary payload.
   - 160-byte FEC Parity payload (present only when `has_fec` is set).
   - Packet length: 164 bytes (no FEC) or 324 bytes (with FEC).
4. **Playout Scheduling**:
   - Maintains ring buffer + `sleep_until` deadline loop.
   - Target playout time incorporates a 10ms lead before deadline ($T_0 + \text{DELAY\_MS}/1000 - 0.010$) to absorb OS thread sleep scheduling jitter.

## Benchmark Results Summary

- **Profile A (`loss=0.02, delay 10–40ms`)**:
  - Lowest Valid Playout Delay: **70 ms** (Deadline misses: 14 / 0.93%, Overhead: 2.00x)
  - 65ms is INVALID (1.20% misses) — 70ms is the measured floor on A.
- **Profile B (`loss=0.05, delay 20–80ms`)**:
  - Lowest Valid Playout Delay: **120 ms** (Deadline misses: 13 / 0.87%, Overhead: 2.00x)
  - 110ms is INVALID (1.33% misses) — 120ms is the measured floor on B.

Overhead is identical (479120B up / 240000B raw = **1.9963x**, displayed
rounded as "2.00x") on every run regardless of profile or delay — it's
fixed by our own send pattern (34/35 packets carry a parity payload), not
by network conditions, so it doesn't shift with an unseen grading profile.
It leaves only **~0.4% headroom** under the 2.0x cap, though — worth
flagging as tight, not comfortable.

## Submission lock-in

Per the submission's requirement to name a single fixed `delay_ms` (see
`PROTOCOL_DESIGN.md`), and since profile B needs 120ms to stay valid while
A only needs 70ms, and grading profiles are unseen: **the single value we
submit must be safely above the worse (B-like) floor, not the better
(A-like) one.** Picking 70ms because it worked on A would fail outright on
anything as lossy/jittery as B. Current recommendation: submit **120ms**
(the measured B floor, still valid on A with more margin there), pending a
decision on whether to pad further for profiles harsher than both A and B.

## Remaining risk

- **Overhead margin is thin (1.9963x vs 2.0x cap).** Since it's
  deterministic and not network-dependent, it won't drift within a run,
  but there's no room for e.g. a slightly longer duration or a header
  miscount to push it over. Worth considering dropping parity coverage
  from 34/35 to something like 30/31 for a larger safety margin if the
  delay cost of doing so is small.
- **Only two profiles measured (A, B).** Grading profiles are unseen and
  may include burst loss (Gilbert-Elliott, supported by the relay but not
  exercised by A/B) — the 1-frame-lag FEC recovers isolated single losses
  well but has no defense against 2+ consecutive losses. Worth stress
  testing against a synthetic burst-loss profile before finalizing the
  submitted `delay_ms`.
