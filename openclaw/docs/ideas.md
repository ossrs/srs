# Ideas

Collection of ideas for SRS — captured as they come, refined over time.

**Format:** Each idea is a top-level section (`##`) with the date. Multiple paragraphs OK, but one section per idea. No subsections.

---

## 1M Token Context Window = Full SRS in One Shot (2025-02-12)

Claude Opus 4.6 supports 1M tokens. SRS is ~150K lines of code + docs ≈ 300K tokens — only 30% of the context window. This means the entire codebase and all documentation can be loaded into a single model context at once.

**Why this matters:**
- No need for RAG, context engines, or search tools to narrow focus — the AI already has everything
- The model can find its own context across the full codebase rather than being told what to look at
- An AI with full codebase + full docs is potentially more capable than any single human maintainer

**The gap:** There's still knowledge in William's brain that isn't in the code or docs — background reasoning, design decisions, "why not" choices, protocol nuances.

**The play:** Build out the knowledge base to capture that brain knowledge. And here's the beautiful part — because the AI can already load all the code and docs, it can *help* extract that knowledge from William by cross-referencing against the codebase. The AI becomes a tool for building its own training data.

**Next step:** Load the full SRS codebase and docs into context, then use that to help William build the knowledge base for the project. See whether the AI can effectively assist in extracting and structuring knowledge when it has full code + doc context.

## Skills as Knowledge Base Loaders, Not Replacements (2026-02-14)

Published the first SRS skill to ClawHub: https://clawhub.ai/winlinvip/srs-support

**What the skill does:** It's a standard workflow that activates when you ask about SRS. The skill checks the knowledge base, checks the codebase, checks the local repository, and loads all relevant documents into the AI's context. This turns a generic AI into one that knows every detail of SRS — specific, up-to-date, not generic or misleading.

**Key insight: Skills don't replace the knowledge base — skills depend on the knowledge base.** The skill is the mechanism that loads the knowledge base (and potentially special knowledge bases) into context. If the workflow changes or new workflows are introduced (e.g., a skill for code review, or bug fixes), new skills can be created. But the knowledge base itself is the core asset.

**What makes the knowledge base valuable:**
- It's up-to-date (not stale training data)
- It includes the codebase AND documentation
- Most importantly: it captures background, decisions, and design thinking — the stuff in William's mind that's NOT in the code
- The knowledge base will be updated by William as he teaches the AI, not by the skill itself (unless workflow changes require it)

**Multiple knowledge bases for different audiences:**
- Users need to know how to use SRS — configuration, deployment, scaling, debugging
- Developers need to know how to modify SRS — architecture, internals, design rationale
- Maybe at least two knowledge bases, maybe more (deep analysis, domain-specific)
- With large enough context windows (1M+ tokens), the AI might load all knowledge bases at once — but the audiences are still different, so the skill can help focus the response

**Skills can also be smart about context:**
- Check the environment (local repo state, config, etc.)
- Ask clarifying questions to make the query more specific
- Help the user formulate better questions before loading context

**Future direction:** More specialized skills (code review, bug fix, deployment) following the same pattern — skill as workflow, knowledge base as the underlying asset. The skill + knowledge base combination is powerful for the community: anyone can use it to get expert-level help with SRS.
