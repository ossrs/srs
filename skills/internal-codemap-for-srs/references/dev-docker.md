# SRS Docker Image Code Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` routes a task to the SRS Docker image toolchain.

## Repository Boundary

- Authoritative repository: `https://github.com/ossrs/dev-docker`
- Configured local checkout: `~/git/dev-docker`
- Check this path directly before using the route. If it does not exist, stop and ask the user to clone `https://github.com/ossrs/dev-docker` into `~/git/dev-docker`; do not clone it automatically or search for another checkout.
- Keep the SRS repository as the current working directory. Inspect the Docker repository with `git -C ~/git/dev-docker ...`.
- Do not assume the checked-out branch owns the requested image. Check status and branches first, then read the target branch with `git show origin/<branch>:<path>` when switching is unnecessary.
- The repository uses long-lived branches as independent image definitions. A file named `Dockerfile` has different responsibilities on different branches.

Before editing, verify both repositories are clean and record their branches and commits:

```bash
git status --short --branch
git rev-parse HEAD
git -C ~/git/dev-docker status --short --branch
git -C ~/git/dev-docker rev-parse HEAD
```

## Primary Ubuntu 20 Toolchain

The `ubuntu20` branch owns the primary multi-architecture SRS development image, published as `ossrs/srs:ubuntu20`. Its root files are split into dependency layers so the workflow can rebuild only affected images.

- `Dockerfile.base` — Builds foundational static dependencies, including OpenSSL, NASM, FDK-AAC, LAME, and x264, on Ubuntu Focal.
- `Dockerfile.base2` — Adds CMake.
- `Dockerfile.base3` — Adds SRT, libxml2, x265, and text-rendering dependencies used by FFmpeg.
- `Dockerfile.base50` — Builds stock FFmpeg 5.0.2 and saves it as `ffmpeg5` and `ffprobe5`.
- `Dockerfile.base51` — Builds FFmpeg 5.0.2 with the legacy `ffmpeg_rtmp_h265-5.0` patch and saves `ffmpeg5-hevc-over-rtmp` and its ffprobe companion.
- `Dockerfile.base52` — Builds stock FFmpeg 7.1.1 and saves it as `ffmpeg7` and `ffprobe7`.
- `Dockerfile.base999999` — Aggregates the FFmpeg variants and common dependencies. It currently selects `ffmpeg5-hevc-over-rtmp` as `/usr/local/bin/ffmpeg` and `ffprobe`.
- `Dockerfile` — Produces the final `ossrs/srs:ubuntu20` developer image from `ubuntu20-base999999`, adding build/debug tools, Go, and GoogleTest.
- `.github/workflows/release.yml` — Detects which layered Dockerfiles changed, builds `base`, `base2`, `base3`, `base50`, `base51`, `base52`, `base999999`, then publishes the final multi-architecture `ubuntu20` image.
- Versioned archives such as `ffmpeg-*.tar.bz2`, `srt-*.tar.gz`, `openssl-*.tar.bz2`, and `x265-*.tar.bz2` — Vendored build inputs referenced directly by the Dockerfiles. A version bump normally changes both its Dockerfile and archive.

### Tool Packaging Into SRS

The final SRS runtime image is built in the SRS repository's root `Dockerfile`:

1. It uses `ossrs/srs:ubuntu20` as the build image.
2. It builds and installs SRS.
3. It copies `/usr/local/bin/ffmpeg` from the build stage to `/usr/local/srs/objs/ffmpeg/bin/ffmpeg` in the runtime image.

Therefore, adding or changing an alternate binary such as `ffmpeg7` in `dev-docker` does not change the SRS-packaged FFmpeg unless the default symlink or the SRS copy source is also changed.

## Cache and Cross-Build Branches

Cache branches start from a corresponding development image, build selected SRS branches to pre-populate dependency objects, and publish a faster CI build image.

- `ubuntu20-cache` — Primary current cache image. It builds the SRS `6.0release`, `7.0release`, and `develop` branches and publishes `ossrs/srs:ubuntu20-cache` for armv7, arm64, and amd64.
- `ubuntu20-cache-cross-arm` — Pre-builds the Ubuntu 20 ARM cross-compilation dependencies.
- `ubuntu20-cache-cross-aarch64` — Pre-builds the Ubuntu 20 AArch64 cross-compilation dependencies.
- `ubuntu18-cache` and `ubuntu16-cache` — Compatibility caches based on their corresponding Ubuntu images.
- `ubuntu16-cache-cross-arm` and `ubuntu16-cache-cross-aarch64` — Older Ubuntu 16 cross-compilation caches.
- `dev-cache` and `dev-gcc7-cache` — CentOS 7 dependency caches based on `dev` and `dev-gcc7`.
- `cygwin64-cache` — Prepares the Cygwin64 SRS cache through an Ubuntu build environment.

Each cache branch has its own root `Dockerfile` and `.github/workflows/release.yml`. Check which SRS release branches it builds before refreshing a cache. Several cache Dockerfiles explicitly restore `ffmpeg5-hevc-over-rtmp` as the default even when their parent image contains another FFmpeg version.

## Base and Compatibility Branches

- `ubuntu18` — Ubuntu Bionic development image with FFmpeg 4.2.1, FFmpeg 5.0.2, and the patched FFmpeg 5 HEVC variant.
- `ubuntu16` — Ubuntu Xenial equivalent of the compatibility development image.
- `dev` — CentOS 7 development image.
- `dev-gcc7` — CentOS 7 development image using the GCC 7 software collection.
- `dev6` and `dev6-cache` — CentOS 6 definitions whose workflows intentionally target non-existent `*-not-maintained` branch names; treat them as disabled historical definitions.
- `dev8` — CentOS 8 definition disabled in the same way.
- `aarch64` — Older dedicated ARM64 image, predating the multi-platform Buildx definitions.

Do not update compatibility or disabled branches automatically when changing `ubuntu20`. Add them only when the maintenance scope explicitly requires those published images.

## Specialized Branches

- `encoder` — Builds the standalone SRS FFmpeg encoder image and uses the patched FFmpeg 5 variant by default.
- `tools` — Builds a small Ubuntu image that exports FFmpeg and FFprobe command-line tools from the Ubuntu 20 toolchain.
- `node-av` — Builds a Node.js image containing the FFmpeg and FFprobe variants for media applications.
- `docs-cache` — Builds the Node-based SRS documentation dependency cache; it does not own the media-server FFmpeg package.
- `copy-image` — Workflow-only branch for copying existing container images between registries or tags.
- `internal/files` and `nv-ffmpeg` — Historical/internal CentOS-based dependency work; neither is the primary Ubuntu 20 release path.
- `srt` — Documentation-only historical branch.
- `v2`, `v3`, and `v4` — Historical release documentation branches without current Docker build definitions.

## Change Routing

Choose the smallest branch and file set:

- FFmpeg or another dependency version in the current development image: `ubuntu20`, its owning layered Dockerfile, vendored archive, aggregation file when selection changes, and release workflow only when the layer graph changes.
- Default FFmpeg selection: `ubuntu20:Dockerfile.base999999`, then the SRS root `Dockerfile` to confirm which binary is copied.
- Faster native CI builds: the matching `*-cache` branch's `Dockerfile` and workflow.
- Cross-compilation cache: the exact `*-cache-cross-*` branch only.
- Standalone encoder or tools image: `encoder`, `tools`, or `node-av`, not the primary `ubuntu20` branch unless its parent toolchain must also change.
- Registry-copy behavior: `copy-image` workflow only.

When a dependency update affects both the base image and cache images, update and publish in dependency order:

1. Build and publish the owning `ubuntu20` dependency layer.
2. Build and publish `ubuntu20-base999999` and `ubuntu20`.
3. Refresh only the required cache or specialized branches that consume `ubuntu20`.
4. Build the SRS release image and verify the binary copied to `objs/ffmpeg/bin/ffmpeg`.

## Verification

Static verification:

```bash
git -C ~/git/dev-docker diff --check
git -C ~/git/dev-docker diff --stat
git -C ~/git/dev-docker grep -n 'ffmpeg-' origin/ubuntu20 -- 'Dockerfile*'
git -C ~/git/dev-docker show origin/ubuntu20:Dockerfile.base999999
```

Runtime verification requires Docker or Buildx. Use the smallest changed layer first, then the final consumer image. At minimum verify:

```bash
docker run --rm ossrs/srs:ubuntu20 ffmpeg -version
docker run --rm ossrs/srs:ubuntu20 ffprobe -version
docker run --rm <srs-release-image> ./objs/ffmpeg/bin/ffmpeg -version
```

For codec or protocol dependency updates, run the issue-specific media reproduction against the final SRS release image rather than treating a successful Docker build or `ffmpeg -version` as sufficient.
