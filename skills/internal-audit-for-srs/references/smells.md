# Smells

Mistakes an agent has made in this project, one line each, each confirmed by the user's own read, with an example. The auditor reads this in phase 2, after its blind read, and reports each match with the file and line.

Add a line only when the user's read catches something the audit missed, with an example and no commit or review ID. Do not add lines from guesswork.

## State and lifetime

- **Transient state.** The same thing is added and removed in one function, around an event that reads it, so the event sees a state no client ever sees. Example: an RTSP client added to the stats for its `on_play` at DESCRIBE and removed right after, though it stays connected.

## Tests

- **Test asserts the implementation.** The expected values describe what the diff does, not what the protocol says, such as a state right after an event that the protocol says does not change it. Example: the RTSP test `RtspOnPlayCountsPlayer`: the hook sees 2 clients, then the stats hold 1.
- **One moment, not a lifetime.** A single number at a single instant, where the expectation is the state after each step: after the start, after a refusal, after the play, after the close. Example: the `RtspOnPlayClients` case checked only the hook payload's number, so the add-then-remove passed it.

## Message and comments

- **"Everything else unchanged."** Claimed for a bug whose honest fix should move something visible: an API listing, another protocol's hook, a default. Example: "every other hook and client are unchanged", while the RTSP client's presence in the API should have changed.
- **Comment that justifies.** A comment that defends the shape of the code; the reason usually covers a gap. Example: "It leaves the stats after the hook, for only PLAY makes the player, which adds it again."
- **Decision presented as given.** A design choice settled in passing, such as when a client exists or which layer owns a check, with no option named. Example: "for PLAY adds the player as before", so a client is not listed until PLAY.

## Code shape

- **Guards that do not match the surrounding code.** A re-lookup, a null check or a two-line helper where the nearby code uses the object directly; it hides a lifetime question instead of answering it. Example: `expire_rtc_session(username)` found a session again that the next lines use directly.
- **Per-packet work on the hot path.** A scan, a lookup or an allocation per RTP or RTCP packet where the code around it caches. Example: an RTX check that scanned the video tracks on every packet, audio included, before `find_track`.
- **Iterator element not named.** `it->second->` or `(*it)->` where the surrounding code names the element first. Example: `SrsRtcPlayStream::on_timer`.
