# Learn Code

**Prerequisite:** Use this workflow only after the Task Router in `skills/srs-develop/SKILL.md` selects **Learn Code**. Do not execute it directly.

**Scope:** Explain how existing SRS or Oryx code works without modifying either project. Cover implementation, architecture, control flow, data flow, module boundaries, and behavior grounded in current code and project documentation.

## Step 1: Route and read documentation first

1. Load `skills/internal-docs-for-srs/SKILL.md` and use its Reference Router before reading project documentation.
2. Classify the question by product first, then by server generation, service, protocol, or feature. Select and read the smallest relevant document set for design intent, architecture, and documented behavior.
3. If the documentation router has no matching route, or a fully resolved routed file is unavailable, record the documentation gap and continue to code routing. Do not broadly search documentation directories or invent intent.

## Step 2: Route to the responsible code

1. Load `skills/internal-codemap-for-srs/SKILL.md` and use its Reference Router to select the relevant SRS or Oryx map. If choosing the wrong product or server generation would materially change the answer, ask the user to clarify.
2. Use the selected map descriptions to identify the owning module and the smallest relevant file set. When the map lists a directory, list filenames only in that directory before selecting files.
3. Read or search only the selected files. Add another routed module only when evidence shows that the implementation crosses the first module boundary.
4. For Oryx, keep the current working directory unchanged and inspect the configured `~/git/oryx` checkout. If it is missing, ask the user to clone it; do not search for another checkout.

## Step 3: Trace and reconcile the implementation

1. Trace the narrowest useful path from the feature entry point, such as configuration, API, listener, protocol handler, or public interface, through its owning module and required lower-level dependencies.
2. Separate the common implementation path from protocol-specific or service-specific behavior. Identify defaults, platform-dependent behavior, fallbacks, and limitations when relevant.
3. Compare documentation with code. Investigate material conflicts instead of silently preferring either source. Clearly separate confirmed behavior, reasonable inference, and unknowns.
4. Use the testing and verification map only when the user requests runtime verification or static evidence is insufficient for an important claim. Do not change code or tests as part of Learn Code.

## Step 4: Answer the question

1. Answer the user's question directly before describing the investigation.
2. State the examined product and component and, when behavior may vary by revision, the owning repository's current branch and commit.
3. Cite the responsible files with focused line ranges and explain how their responsibilities connect. Do not return an unstructured file dump.
4. Report relevant documentation gaps, code/document conflicts, limitations, and unresolved questions.
5. Make no project changes. If the investigation reveals a likely bug or desired change, report it and let the user start a separate Fix a Bug or Develop Code task.
