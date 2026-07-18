# Issue Truth Records

Record only verified maintenance status and the latest maintainer-approved Truth Record. Never copy unverified issue discussion.

## #4686 — CURRENT

- Issue: https://github.com/ossrs/srs/issues/4686
- Truth Record: https://github.com/ossrs/srs/issues/4686#issuecomment-5004294564
- Verified: 2026-07-17
- Checked: 2026-07-18; no later updates
- Branch: `develop`
- Commit: `e03c841dc442c6d5059cddc0e2f2e6cc4a89d087`
- Version: SRS `8.0.3`
- Environment: macOS 26.5.2, arm64; source and documentation review only
- Changes and tests: None

**Current state**

SRS does not support Media over QUIC. This is a valid deferred feature request, not a bug. Major new protocols will not be added to the current C++ server. The priority is the Go proxy, AI maintenance workflow, Go origin parity, then Go edge and protocols such as MoQ.

MoQ is still evolving; the verified specification was `draft-ietf-moq-transport-19`. A future implementation must define interoperability, roles, formats, codecs, authentication, and SRS integration.

**Conclusion**

No project change is required. Retain the issue and revisit it after the next-generation origin and edge foundations are ready.

**Unknowns**

Schedule, MOQT versus moq-lite, origin/edge/relay roles, media formats, codecs, and browser interoperability.
