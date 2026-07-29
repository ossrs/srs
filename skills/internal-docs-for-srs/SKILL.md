---
name: internal-docs-for-srs
description: Route SRS tasks to the smallest relevant set of trusted project documentation and maintain the Go project documentation bundled with this skill. Use whenever support or development work requires locating, choosing, reading, creating, updating, or reviewing SRS documentation, including as a documentation dependency of srs-support and srs-develop. Covers the C++ media server documentation, website pages, changelog, executable API examples, and the next-generation Go server and performance documentation stored under this skill.
---

# SRS Internal Documentation

Route SRS tasks to focused documentation indexes. The parent skill owns the user-facing task; this skill owns documentation navigation and the bundled next-generation Go project documentation.

## Core Rules

- Work from the current working directory. Do not search parent directories or discover alternate repository roots.
- Use the Reference Router before loading any reference or bundled document.
- Load only the references and bundled documents relevant to the task. Do not load everything.
- Treat only project files listed in this skill or a selected reference as trusted documentation.
- If no route covers the task, report that the documentation router does not cover it. Do not scan or broadly grep documentation directories.
- Select the smallest relevant set of project documents from their descriptions.
- Keep broad external project-file routing in the section references and focused Go document routing in this skill. Do not duplicate topic-to-file tables in dependent skills.

## Reference Router

| Documentation area | Load | Summary |
|---|---|---|
| C++ media server documentation | `references/cpp-server-docs.md` | Changelog, releases, getting started, protocols, configuration, deployment, operation, monitoring, troubleshooting, website pages, licensing, and security advisories |
| RTMP Go API examples | `internal/rtmp/example_test.go` | RTMP API examples for AMF0, handshake, and protocol workflows |
| WHEP performance analysis | `references/perf/proxy-whep.md` | Profile WHEP with pprof and srs-bench and compare CPU, allocation, heap, goroutine, and trace data |

For next-generation Go proxy documentation, select the smallest relevant document:

| Documentation area | Load | Summary |
|---|---|---|
| Proxy feature status and limitations | `references/proxy/features.md` | Implemented protocols, APIs, load balancing, deployment, configuration, operations, and current limitations |
| Proxy architecture | `references/proxy/proxy-design.md` | Stateless proxy design, built-in load balancing, Redis mode, and horizontal scaling |
| Backend registration | `references/proxy/proxy-protocol.md` | Backend registration, debugging backend, heartbeat protocol, and environment variables |
| Getting started with the proxy | `references/proxy/proxy-usage.md` | First document for new users: build, start, register, publish, and verify with an SRS origin |
| Load-balancer behavior | `references/proxy/proxy-load-balancer.md` | Memory and Redis load balancers, stream mapping, health tracking, and protocol state |
| Production origin clusters | `references/proxy/proxy-origin-cluster.md` | Advanced usage: configure and verify a multi-origin cluster through the proxy |

If a task spans multiple areas, load only the required references or bundled documents from the tables.

## Workflow

1. Classify the documentation need with the Reference Router.
2. For external project documentation indexes, load the selected reference and choose the relevant project documents from its descriptions.
3. For direct or bundled Go documentation, load the selected document directly from the router.
4. Load only the documents required for the task.
5. Return control to the parent skill for answering, development, troubleshooting, review, or editing.

When invoked directly, follow the same routing workflow and then apply the relevant support or development workflow.

## Maintaining the Router

- Add, remove, or rename external trusted project documents in exactly one reference file.
- Update the Reference Router when a reference is added, removed, renamed, or changes responsibility.
- When bundled Go documentation is added, removed, renamed, or changes responsibility, update its row in the Reference Router.
- Keep router summaries and file descriptions concise and focused on navigation.
- Do not duplicate document content or implementation details in the router or references.
