# Smells

Mistakes an agent has made in this project, one line each, each confirmed by the user's own read, with the commit or review that showed it. The auditor reads this in phase 2, after its blind read, and reports each match with the file and line.

Add a line only when the user's read catches something the audit missed, with the commit. Do not add lines from guesswork.

## State and lifetime

- **Transient state.** The same thing is added and removed in one function, around an event that reads it, so the event sees a state no client ever sees. `452987524` (R48): an RTSP client added to the stats for its `on_play` at DESCRIBE and removed right after, though it stays connected.

## Tests

- **Test asserts the implementation.** The expected values describe what the diff does, not what the protocol says, such as a state right after an event that the protocol says does not change it. `452987524`'s `RtspOnPlayCountsPlayer`: the hook sees 2 clients, then the stats hold 1.
- **One moment, not a lifetime.** A single number at a single instant, where the expectation is the state after each step: after the start, after a refusal, after the play, after the close. `452987524`'s `RtspOnPlayClients` case checked only the hook payload's number, so the add-then-remove passed it (R48).

## Message and comments

- **"Everything else unchanged."** Claimed for a bug whose honest fix should move something visible: an API listing, another protocol's hook, a default. `452987524`: "every other hook and client are unchanged", while the client's presence in the API should have changed.
- **Comment that justifies.** A comment that gives a reason for the shape of the code instead of saying what it does; the reason usually covers a gap. `452987524`: "It leaves the stats after the hook, for only PLAY makes the player, which adds it again."
- **Decision presented as given.** A design choice settled in passing, such as when a client exists or which layer owns a check, with no option named. `452987524`: "for PLAY adds the player as before", so a client is not listed until PLAY.

## Code shape

- **Guards that do not match the surrounding code.** A re-lookup, a null check or a two-line helper where the nearby code uses the object directly; it hides a lifetime question instead of answering it. `7b744bcde` (R37): `expire_rtc_session(username)` found a session again that the next lines use directly.
- **Per-packet work on the hot path.** A scan, a lookup or an allocation per RTP or RTCP packet where the code around it caches. The WebRTC RTX review (2026-09-21): an RTX check that scanned the video tracks on every packet, audio included, before `find_track`.
- **Iterator element not named.** `it->second->` or `(*it)->` where the surrounding code names the element first. R43 (`SrsRtcPlayStream::on_timer`).
