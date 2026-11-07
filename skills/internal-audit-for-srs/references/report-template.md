# Audit Report Template

One report per auditor, in Markdown, at the path the parent names; the three auditors of a change each write one, each from the approach it chose. Sections 1 to 4 are written in phase 1, before the auditor has seen the commit message or the tests; sections 5 to 8 in phase 2; sections 9 to 11 in phase 3, after each suspect was attacked with a test. Keep the order and the headings, so the user can see what was written blind and what was proven.

Write plain sentences. Name files and lines as `path:line`. Quote the code or the doc that an expectation comes from; do not paraphrase a claim as a fact.

## 1. Change

- The base commit, the code files changed with lines added and removed, and the areas touched (the protocol, the feature, the layer: connection, source, stats, hooks, config, tests).

## 2. Expectations

Written before the message and the tests. For each lifecycle the change touches, the state that should hold after each step, with its source:

- The protocol (RFC or SRS's documented behavior), `trunk/conf/full.conf`, the docs, or the sibling code that does the same thing for another protocol (an RTMP, HTTP-FLV, HLS, SRT, WebRTC, RTSP or GB28181 player or publisher).
- Each expectation as one line: *after `<step>`, `<state>` because `<source>`.*
- Include the API (`/api/v1/clients`, `/api/v1/streams`), the hooks (`on_connect`, `on_play`, `on_stop`, ...), the stats and the stream's lifetime, whenever the change can move them.

## 3. Blind read

What the code does, in plain words, step by step, without guessing why. If the diff adds and removes the same thing, say so. If it changes a value a client sees, name the client. If it touches one of several copies of the same logic, name the others.

For C++ code, then one line per changed function or class against `srs-develop` `references/cpp-memory-safety.md`: who owns each pointer and frees it, what can yield and what is used after it, and which input reads are bounded.

## 4. Suspects

Where the P0 or P1 bug most likely is, from sections 2 and 3, P0 first. Start with one line naming the approach this auditor chose. One line each: the place (`path:line`), the scenario that would break it, and the state that would be wrong. Cover the state after each step, the refusal and error paths, the sibling protocols, and the close.

## 5. Code against message

Now with the message:

- Claims the code does not meet.
- Things the code does that the message does not say.
- A comment that gives a reason: does the code need that reason, or does the reason cover a gap?

## 6. Test gaps

The suspects of section 4 against the tests present:

- Suspects no test reaches.
- Tests whose expected values describe what the diff does rather than what section 2 says. Quote the assertion.
- Tests that assert one moment where section 2 expects a lifetime.

## 7. Decisions the change makes without saying so

Design choices the change settles and presents as given, such as when a client exists, which layer owns a check, or which protocols are covered. These are the user's to make; list them with the option the change took and the other option.

## 8. Smells

The lines of `references/smells.md` that match, each with the file and line where it matches; each match is a suspect to prove, not a bug. None is a valid answer, and it says nothing about whether the change has a bug.

Close the phase with the suspects updated by what the claims and the gaps showed, ranked again.

## 9. Proven bug

The one bug a test proved: the first P0, or a P1 when no P0 suspect could be proven. With its severity and:

- **Summary** — one line.
- **Scenario** — the steps, and the state that is wrong after them.
- **Evidence** — `path:line`, and the expectation of section 2 it breaks.
- **Proof** — the test's name and file in the proof worktree, its code, the command that ran it, and its failing output at the audited commit, quoted.
- **What the tests did not check** — the gap that let it pass.

Or **No bug proven**, with the number of suspects attacked; section 10 lists them.

## 10. Not proven

Every suspect of section 4 and phase 2 that was attacked and is not in section 9, then the ones left untried after the bug was proven, one line each: the scenario, the test tried and its result (passed, or could not build or run, and why), and the other paths tried to the same code. A suspect with no test tried is listed with the reason, such as "a P0 was proven first".

## 11. Not checked

What this audit cannot see: timing and load, other platforms, behavior that only a run shows, and anything the auditor could not reach from the diff and the base tree.
