# NOTES

**Design.** Sender pairs every 2 frames into a block and sends one XOR-parity packet per block; the receiver rebuilds a lost frame from the parity plus its partner. Recovery is proactive FEC, not retransmission — a resend across the hostile relay would cost 20–160ms of the delay we're scored on. The receiver has no playout clock: it forwards each frame (and reconstruction) the instant it's ready, since the harness scores on arrival-before-deadline and dedups by seq. That one choice removed the `sleep_until` scheduling jitter that had earlier versions straddling the 1% cap, and keeps overhead at a fixed 1.54× (well under the 2× cap).

**Grade at `delay_ms = 120`.** Profile B (the harder profile) measures 0.60% misses there with margin, and A is trivially valid; the ~100ms floor is B's 80ms jitter tail plus the 20ms parity lag, so 120ms is deliberate safety headroom for unseen profiles.

**What breaks it.** Two consecutive losses hitting both frames of one block are unrecoverable (single parity covers one loss per block), and any profile with a jitter tail past ~100ms would need a larger `delay_ms`.
