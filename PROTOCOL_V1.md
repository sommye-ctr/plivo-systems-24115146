# Protocol v1 — locked spec

## Wire format (sender → receiver, over the relay)

```
Header (5 bytes):
  seq      u32 big-endian   — this frame's sequence number
  flags    u8               — bit0: has_redundant_payload

Body:
  primary_payload    160 bytes            — this frame's data
  redundant_payload  160 bytes (only if flags bit0 set) — copy of frame (seq-3)'s payload
```

- Packet size: 165 bytes (no redundancy) or 325 bytes (with redundancy).
- `redund_seq = seq - 3` is implicit, never transmitted.
- Redundancy is attached on 3 of every 4 frames; skipped when `seq % 4 == 0`.
- Overhead: `(0.25×165 + 0.75×325) / 160 ≈ 1.78x` — under the 2.0x cap with ~0.22x headroom.
- Interleave distance of 3 means a burst of up to 3 consecutive losses still
  leaves a surviving copy of every affected frame (each arrives on a
  different, later packet).

## Threading model

### Sender — single-threaded

No independent clock to service and no concurrent responsibility in v1
(feedback path unused), so one thread is sufficient:

```
loop:
  recvfrom(47010, blocking)          — frame i from harness source
  ring[i % 8] = payload              — small lookback buffer for redundancy
  flags = (i % 4 == 0) ? 0 : REDUNDANT_BIT
  if REDUNDANT_BIT: attach ring[(i-3) % 8]
  sendto(47001, header + primary [+ redundant])
```

### Receiver — two threads, one mutex

Ingesting arrivals and emitting on a fixed clock are independent jobs
and must not block each other.

**Shared state** (behind one `std::mutex`):
```
struct Slot { uint32_t seq; bool present; uint8_t payload[160]; };
Slot ring[CAPACITY];        // CAPACITY = ceil(DELAY_MS/20) + 3 + 5, sized at startup
uint32_t next_to_play;      // playout cursor; drops stale/duplicate writes below it
```

**Thread 1 — network-in** (blocking `recvfrom` on 47002 only, never
sleeps/waits on anything else):
- Parse header. If `seq >= next_to_play` and slot empty/stale, store primary.
- If redundancy flag set and `seq-3 >= next_to_play` and that slot is
  still empty, store the redundant copy there too.

**Thread 2 — playout clock** (never touches the socket):
- `sleep_until(T0 + DELAY_MS/1000 + i*0.020)` — absolute timestamps,
  not relative sleeps, to avoid drift over the run.
- Lock, check `ring[i % CAPACITY]`, copy out payload if present and
  `seq == i`, advance `next_to_play = i+1`, unlock.
- If present: `sendto(47020, header(i) + payload)`. Else: silent miss.

### Why this shape
- No lock-free structures — traffic is ~50 pkts/sec, an uncontended
  mutex costs nothing measurable.
- No third thread yet — v1 carries no feedback traffic; a future NACK
  backstop would slot in as its own thread without touching these two.
- `next_to_play` is a cheap gate so the network thread never wastes work
  on frames that are already played or unreachable in time.

## Open items (not yet decided)
- Real interleave distance / skip-rate tuning once we have working code
  and real measured miss rates (currently reasoned from the naive
  baseline sweep, not from our actual protocol running).
- Whether a lightweight NACK backstop is worth adding later, only if
  delay budget allows a round trip without risking the 1% cap.
