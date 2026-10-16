# Audit Report Template

One report per audited change, in Markdown, at the path the parent names. Sections 1 to 4 are written in phase 1, before the auditor has seen the commit message or the tests; sections 5 to 10 in phase 2. Keep the order and the headings, so the user can see what was written blind.

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

## 4. Tests I would write

The checks this change needs to be trusted, from section 2: the state after each step, the refusal paths, the sibling protocols, the close. One line each.

## 5. Code against message

Now with the message:

- Claims the code does not meet.
- Things the code does that the message does not say.
- A comment that gives a reason: does the code need that reason, or does the reason cover a gap?

## 6. Test gaps

Section 4 against the tests present:

- Expected checks with no test.
- Tests whose expected values describe what the diff does rather than what section 2 says. Quote the assertion.
- Tests that assert one moment where section 2 expects a lifetime.

## 7. Decisions the change makes without saying so

Design choices the change settles and presents as given, such as when a client exists, which layer owns a check, or which protocols are covered. These are the user's to make; list them with the option the change took and the other option.

## 8. Smells

The lines of `references/smells.md` that match, each with the file and line where it matches. None is a valid answer.

## 9. Findings

Ranked, most severe first. Each one:

- **Summary** — one line.
- **Scenario** — the steps, and the state that is wrong after them. Concrete enough to turn into a test.
- **Evidence** — `path:line`, and the expectation of section 2 it breaks.
- **What the tests did not check** — the gap that let it pass.

Or **Nothing found**, followed by what was tried: the lifecycles walked, the siblings compared, the refusal paths followed.

## 10. Not checked

What this audit cannot see: timing and load, other platforms, behavior that only a run shows, and anything the auditor could not reach from the diff and the base tree.
