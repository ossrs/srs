---
name: srs-learn
description: For developers who want to become SRS contributors or maintainers — learn SRS or any of its modules (ST, protocols, media) in depth, understand detailed code and implementation, media architecture, and the underlying knowledge behind it all. The learning path for anyone who wants to touch, modify, or extend the codebase.
---

# SRS Learn

## Purpose
Turn SRS knowledge base docs into hands-on learning sessions:
- Start from `memory/srs-*.md`
- Let the user choose what to learn
- Teach with real source code
- Default: create a new, standalone unit test file for the learner
- If the user explicitly requests reusing/modifying a specific existing utest file, follow the user's request instead of forcing a new file
- Build and run it successfully before moving on
- Teach workflow and debugging until the topic is understood

## Setup
Before starting a learning session:
- Resolve `SRS_ROOT` dynamically:
  1. If `SRS_ROOT` env is set and contains `trunk/src`, use it.
  2. Else, if current workspace (or its git root) contains `trunk/src`, use that.
  3. Else, if `~/git/srs/trunk/src` exists, use `~/git/srs`.
  4. Else, ask the user for SRS repo root.
- Confirm knowledge base files exist in workspace: `memory/srs-*.md`.
- Identify the matching specialized skill for the topic (e.g. `st-develop` for ST/coroutines). A specialized skill is **required** — if none exists, abort (see Step 3).

## Learning Workflow
Follow this sequence every time.

1. Identify the target knowledge base.
2. Summarize concrete sections and let the user choose.
3. Find the matching specialized skill — abort if none exists.
4. Teach the section with code + new utest file (build success + run success required).
5. Explain utest workflow and debugging.
6. Confirm mastery and propose the next step.

Do not skip user choice steps.

## Step 1: Select Knowledge Base
List all matching files: `memory/srs-*.md`.

Ask the user:
- Which KB file to learn now?
- What is the goal (overview, deep internals, debugging, implementation)?
- Preferred depth (quick, normal, deep)?

If the user already specifies the KB, proceed directly.

## Step 2: Summarize Concrete Learning Sections
Read the selected KB fully.

Extract concrete, teachable sections and present them as a numbered menu. For each item, include:
- Section/topic name
- What the learner will master
- Main source files/functions to inspect
- A candidate utest demo idea

Keep the menu concise and actionable (typically 3-8 items).

Ask the user to select one item before continuing.

## Step 3: Find Specialized Skill (Required)
After the user picks a section, identify the matching specialized skill.

srs-learn **cannot** create, build, or run utests on its own. It relies entirely on the specialized skill for:
- Loading the correct source code context
- Creating utest files in the right location with the right patterns
- Building and running utests

Example: for ST/coroutine topics, use `st-develop`.

**If no matching specialized skill exists for the selected topic, abort the learning task.** Tell the user which topic/module lacks a skill and that one needs to be created before this topic can be learned through srs-learn.

## Step 4: Teach with Code + New Unit Test
Read and follow the specialized skill identified in Step 3. It owns the build/test workflow.

After completing build/run for a lesson, always run the specialized skill's required verifier (if defined) before declaring completion.

By default, create a **new, standalone utest file** so each lesson has a clean, isolated artifact to study. If the user explicitly asks to continue in a specific existing utest file, modify that file instead.

Teach in this order:

1. Explain the concept briefly from KB (what and why).
2. Walk through the concrete code path (entry → core logic → output/effect).
3. Create a new utest file that demonstrates one specific behavior from the section.
4. Build the utest — confirm zero build errors.
5. Run the utest — confirm it passes.
6. Explain why the test passes based on code logic.

Both **build success** and **run success** are required before the lesson is considered complete. If either fails, debug and fix, then retry. If blocked, explicitly report the blocker and current failure output.

Unit test guidelines:
- Keep the scope narrow (one behavior per test).
- Use clear naming tied to the concept.
- Prefer deterministic inputs and assertions.
- Reuse existing utest patterns from the repository.

## Step 5: Explain Utest Workflow
After running the test, explicitly teach the workflow:
- How test setup/fixtures map to module state
- What action triggers the behavior
- What assertions validate correctness
- How this test connects to production code flow

Then give a debugging walkthrough:
- Where to set breakpoints/logs
- Which variables/state transitions matter most
- Common failure signatures
- How to isolate regressions quickly

## Step 6: Mastery Check and Iteration
Ask short mastery-check questions:
- "What does this test prove?"
- "Which function is the true decision point?"
- "If this assertion fails, where do we debug first?"

If the user wants more practice:
- Propose an extension exercise (new edge case or variation)
- Create a new utest file for it (same rules: build success + run success), unless the user explicitly asks to continue in an existing utest file
- Discuss results

## Output Format During Sessions
Use this response structure during learning sessions:

1. Selected section and objective
2. Code map (files/functions)
3. Unit test plan
4. Utest implementation + build/run results (must include test file path and explicit pass evidence)
5. Workflow explanation
6. Debugging checklist
7. Mastery check + next step

Keep explanations technical and direct. Prioritize concrete code behavior over abstract theory.
