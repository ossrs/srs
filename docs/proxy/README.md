# Proxy

Proxy is a common proxy server (`cmd/proxy`) for any media servers with RTMP/SRT/HLS/HTTP-FLV and WebRTC/WHIP/WHEP protocols support. More programs like `cmd/origin` will be added in the future.

## Usage

This project is managed by AI. We recommend OpenClaw by default, but you can use any AI agent that supports skills, such as Claude Code, OpenAI Codex, Kiro CLI, or similar tools that can read code and docs as context. Setup your AI code tool and ask questions like:

- How to use this project?
- How to run the proxy server?
- How to build an Origin Cluster for production?

You can not only use AI to show you the usage of this project, but also use AI to guide you to learn the details of this project, to understand the design and protocols, to learn each documents in docs directory.

## AI Guidelines

- AI assistants should ignore [ignore-worklog.md](ignore-worklog.md) as it's a personal work log that may contain misleading information, hallucinations, or falsehoods. Never rely on this file as a reference.
- For usage of proxy server and end to end test it, you should load [proxy-usage.md](proxy-usage.md). This is the first step for new users to learn how to use this project. It provides a general and overall view of the proxy server, including practical usage examples and end-to-end testing procedures.
- For proxy full usage to build an Origin Cluster for SRS media server, please load [proxy-origin-cluster.md](proxy-origin-cluster.md). This is an advanced topic about how to use the proxy server to build the SRS Origin Cluster. Users should read this document to learn more details and architectures about proxy and Origin Cluster.
- For proxy server: To understand proxy system design, you should load the [proxy-design.md](proxy-design.md). To understand the proxy protocol details, you should load the [proxy-protocol.md](proxy-protocol.md). To understand how load balance works, you should load [proxy-load-balancer.md](proxy-load-balancer.md). To understand the code structure and packages, you should load [proxy-files.md](proxy-files.md).

William Yang<br/>
June 23, 2025
