# The Flaky Network — plain-language briefing

## What's actually being built

Think of a phone call. One side talks, audio gets chopped into small
20-millisecond chunks, and those chunks need to arrive at the other side
often enough, and in the right order, that the voice sounds smooth.

You are writing **both ends** of that call — a `sender` and a `receiver` —
in C++. In between them sits a piece of network you don't control and
can't change (the "relay"), which is deliberately built to be hostile:
it drops some packets, delays others by random amounts, sometimes
reorders them, and sometimes delivers the same one twice. That's what
"flaky" means here — it's simulating the real, messy internet.

Your job: make the audio still arrive on time and correct as often as
possible, while adding as little extra delay as you can get away with.

## The pieces, in order

```
[harness source]  --frame i, every 20ms-->  [YOUR SENDER]
                                                  |
                                          (your own protocol)
                                                  v
                                            [hostile relay]
                                     (drops / delays / reorders / dupes)
                                                  |
                                                  v
[harness receiver] <--frame i, must be on time--  [YOUR RECEIVER]
```

- The **harness source** hands your sender one 160-byte frame every
  20ms, right on schedule. You can't ask for it early.
- Between your sender and your receiver, **you design the protocol** —
  what bytes go over the wire, whether you add redundancy, whether you
  ask for retransmits, anything. This is the actual assignment.
- The **harness player**, on the far end, checks each frame against a
  strict deadline. If it's not there, correct, on time — it's a miss.

## Why this is hard: the network fights you in four ways

1. **Drops** — some frames just vanish. Permanently, unless you do
   something about it.
2. **Random delay (jitter)** — frames don't arrive at a steady pace even
   if they were sent at one. Frame 5 might take 12ms, frame 6 might take
   38ms, even though both left on schedule.
3. **Reordering** — because of that random delay, frame 6 can physically
   arrive before frame 5.
4. **Duplication** — occasionally the same frame arrives twice.

## How you're graded (in order of priority)

1. **Deadline-miss rate ≤ 1%.** Almost every frame has to show up on
   time and correct. This is the hard gate — fail this and nothing else
   matters.
2. **Bandwidth overhead ≤ 2.0×.** You're allowed to send at most double
   the raw data (across both directions, counting even the bytes the
   network ends up dropping). This budget exists so you have *some* room
   to fight loss — but it's not unlimited.
3. **Playout delay — lower is better.** This is the deliberate buffering
   delay you choose to add before playing a frame. Among solutions that
   pass gates 1 and 2, whoever adds the least delay wins. This is the
   real tradeoff: more delay makes it easier to hide jitter and loss;
   less delay scores better but leaves you less room to recover.

## What's broken in the naive starting code (and why)

We ran the given baseline (which just forwards every packet as-is,
immediately, no buffering) at a 40ms delay budget on the "mild" test
network. Result:

- Only **2.3%** of packets were actually dropped by the network.
- But **23.7%** of frames missed their deadline — ten times worse.

The gap between those two numbers is the real lesson: **most of the
failure isn't lost packets, it's the baseline having nowhere to put a
packet that arrives a little late or a little out of order.** It just
forwards whatever shows up, the instant it shows up, with zero
buffering. A frame that arrives 2ms after its personal deadline is
scored exactly the same as a frame that never arrived at all — both are
"misses" — and jitter alone is enough to cause that constantly.

So there are two separate problems to solve, not one:

1. **Reordering / jitter problem** — give the receiver a small waiting
   room (a "jitter buffer") so it can hold early-arriving frames just
   long enough to play them back in the right order, on a steady clock,
   instead of forwarding immediately and chaotically.
2. **Loss problem** — some frames really do get dropped by the network.
   Waiting longer doesn't bring back a packet that was never delivered.
   You need to either send extra redundant information ahead of time
   (so the receiver can reconstruct a lost frame without needing it
   resent), or explicitly ask the sender to resend it — each approach
   has a different cost in time and bandwidth.

## The core tension you're actually being tested on

Every real-time system on the internet (video calls, live audio,
multiplayer games) lives on this same tradeoff:

> **The more delay you're willing to add, the easier it is to hide
> network chaos. The less delay you add, the better you score — but the
> less room you have to recover from problems.**

Fixing "misses" is not optional — you must get under 1% to be graded at
all. Once you're under 1%, the actual competition is: how little delay
can you get away with while staying under 1% misses and 2× bandwidth?

## What we still need to decide together

- How to recover from real packet loss within the bandwidth budget
  (send extra copies ahead of time vs. ask for a resend vs. both).
- How big the receiver's waiting room (jitter buffer) needs to be,
  based on the actual delay spikes measured on each test network.
- The exact byte layout of the protocol between your sender and
  receiver (entirely your choice — the harness doesn't care).
- How the two C++ programs should be structured internally (threads,
  locking, non-blocking reads) so the receive loop never stalls at the
  wrong moment and drops the steady 20ms clock.

We haven't picked the loss-recovery strategy yet — that's the next
decision.
