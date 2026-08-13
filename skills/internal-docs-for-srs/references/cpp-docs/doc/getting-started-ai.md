---
title: AI Agent
sidebar_label: AI Agent
hide_title: false
hide_table_of_contents: false
---

# AI Agent

SRS provides several ways to use AI: you can chat with the SRS Robot in Telegram or Discord for quick answers, or run AI locally with Claude Code, Codex, OpenClaw, or Kiro using the pre-configured SRS knowledge base.

## Claude Code

You can use Claude Code locally with the SRS codebase. SRS ships with a pre-configured `.claude` directory so Claude Code works out of the box with full context of the project.

Clone the SRS code, related projects, and start Claude Code:

```bash
git clone https://github.com/ossrs/oryx.git
git clone https://github.com/ossrs/dev-docker.git
git clone https://github.com/ossrs/srs.git
cd srs
claude
```

Claude Code will automatically load the configuration from `srs/.claude`, giving it deep knowledge of the SRS codebase so you can ask questions, debug issues, write code, and more.

## Codex

You can also use Codex locally with the SRS codebase. SRS ships with a pre-configured `.codex` directory so Codex works out of the box.

Clone the SRS code, related projects, and start Codex:

```bash
git clone https://github.com/ossrs/oryx.git
git clone https://github.com/ossrs/dev-docker.git
git clone https://github.com/ossrs/srs.git
cd srs
codex
```

Codex will automatically load the configuration from `srs/.codex`.

## Kiro

You can also use Kiro locally with the SRS codebase. SRS ships with a pre-configured `.kiro` directory so Kiro works out of the box.

Clone the SRS code, related projects, and start Kiro:

```bash
git clone https://github.com/ossrs/oryx.git
git clone https://github.com/ossrs/dev-docker.git
git clone https://github.com/ossrs/srs.git
cd srs
kiro-cli
```

Kiro will automatically load the configuration from `srs/.kiro`.

## OpenClaw

You can create an OpenClaw agent with the SRS knowledge base for local use. First 
clone the SRS code, and related projects:

```bash
git clone https://github.com/ossrs/oryx.git
git clone https://github.com/ossrs/dev-docker.git
git clone https://github.com/ossrs/srs.git
```

There are two ways to point the agent at the SRS knowledge base:

- **Set the workspace directly** — when creating the agent, set the workspace path to `srs/.openclaw`.
- **Soft link** — create the agent with its default workspace, then remove the default workspace directory and replace it with a soft link to `srs/.openclaw`:

```bash
ln -sf ~/git/srs/.openclaw ~/.openclaw/workspace
```

Once the agent is running, list the available skills and trigger them to load the knowledge base. The skills tell the AI which files to load and how to work more effectively with the SRS codebase.

## SRS Robot

SRS provides an AI-powered support robot built on OpenClaw, available in both the Telegram group and Discord channel. The robot has a deep knowledge base covering SRS code, documentation, and common usage scenarios, and is powered by the latest AI model.

You can ask the SRS Robot anything about SRS — how to use it, how to match your use case, how to configure it, how to debug issues, or any other questions. The robot will give you accurate, up-to-date answers based on the latest SRS knowledge base.

Join the SRS Telegram group: [https://t.me/+RiynvKOxpQ42MGJl](https://t.me/+RiynvKOxpQ42MGJl)

Join the SRS Discord channel: [https://discord.gg/yZ4BnPmHAd](https://discord.gg/yZ4BnPmHAd)

Once you're in the group, @ the SRS Robot and ask your question directly.

## Skills

After starting Claude Code, Codex, OpenClaw, or Kiro with the SRS codebase, you should use skills to get the best results. Skills load the right knowledge base for your task and guide the AI through the correct workflow.

The `srs-support` skill is provided for answering questions about the SRS project. It automatically loads the relevant knowledge base based on your question, so the AI can give you accurate and context-aware answers.

More skills will be added over time. To see what skills are currently available, simply ask the AI:

```
What skills can I use for SRS?
```

## Knowledge Base

Code and documentation alone are not enough for AI to truly understand and maintain a project. There is background knowledge, design thinking, accumulated experience, use cases, community communication, and debugging workflows that live only in people's heads — not in any file. The SRS knowledge base is an effort to make all of that explicit.

The knowledge base is the AI agent memory — files that encode the background, experience, and context behind SRS. It is built by having AI read the code and documents, then talking with AI to surface the implicit knowledge and write it down. Over time, the knowledge base will cover everything: not just what the code does, but why it was designed that way, how to think about problems, and how to operate and maintain the project.

The knowledge base and the code together are the single source of truth for SRS. The knowledge base captures what the code cannot — background, design decisions, use cases, debugging experience, and community knowledge. In the future, traditional documentation will be generated from the knowledge base and maintained entirely by AI.

On top of the knowledge base, there are skills. Skills are workflows that tell AI how to handle specific tasks, such as:

- **Support** — answering user questions and matching use cases
- **Issue triage and fix** — understanding, reproducing, and resolving issues
- **Feature development** — designing and implementing new features
- **Maintenance** — reviewing pull requests, managing releases, keeping the project healthy
- **Debugging** — diagnosing and tracing problems in the codebase

Each skill loads the relevant parts of the knowledge base and guides the AI through the right workflow for that task. This is what makes AI effective at maintaining SRS — not just raw intelligence, but structured knowledge and workflows built up over time.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/getting-started-ai)
