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
