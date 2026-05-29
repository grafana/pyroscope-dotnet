---
description: Prepare and publish a new pyroscope-dotnet release
allowed-tools: Bash(git *), Bash(gh *), Read, Edit
---

# Release

Releases are automated with [release-please](https://github.com/googleapis/release-please).
Every push to `main` opens or updates a release PR; merging it triggers the full
release pipeline in a single workflow.

## Steps

### 1. Check the release-please PR

Every push to `main` triggers `release-please.yml`, which opens or updates a PR
with the `autorelease: pending` label that bumps the version.

```bash
gh pr list --repo grafana/pyroscope-dotnet --label "autorelease: pending"
```

To force a specific version bump on the next release PR, add a `Release-As: X.Y.Z`
commit to `main` before the next release-please run, or edit the release PR
before merging.

### 2. Review and merge the release PR

Review the version bumps, then merge the PR. The next
push to `main` runs `release-please.yml`, which:

1. Creates a **draft** GitHub release at `pyroscope-X.Y.Z`
2. Builds profiler `.so` tarballs and uploads them to the release
3. Builds and uploads the managed helper (`.dll`, `.nupkg`)
4. Publishes the NuGet package to nuget.org
5. Publishes the GitHub release and tag once all steps succeed

Wait for the workflow to complete:

```bash
gh run list --repo grafana/pyroscope-dotnet --workflow release-please.yml
gh run watch <run-id> --repo grafana/pyroscope-dotnet --exit-status
```

### 3. Verify the published release

```bash
gh release view pyroscope-X.Y.Z --repo grafana/pyroscope-dotnet
```

Release artifacts attached to the GitHub release:

- `pyroscope.X.Y.Z-{glibc,musl}-{x86_64,aarch64}.tar.gz` — native profiler libraries
- `Pyroscope.dll` and `Pyroscope*.nupkg` — managed helper

## Components

Release-please manages three independently versioned components (`separate-pull-requests: true`):

| Component | Tag format | Package path |
|---|---|---|
| Profiler + managed helper | `pyroscope-X.Y.Z` | `.` (excludes tracing packages) |
| OpenTracing helper | `opentracing-X.Y.Z` | `Pyroscope/Pyroscope.OpenTracing` |
| OpenTelemetry helper | `opentelemetry-X.Y.Z` | `Pyroscope/Pyroscope.OpenTelemetry` |

Each component gets its own release PR when commits touch its path. Merging a release PR triggers the matching build, GitHub artifact upload, NuGet publish, and release publication jobs in `release-please.yml`.

```bash
gh pr list --repo grafana/pyroscope-dotnet --label "autorelease: pending"
```

## Configuration

- `release-please-config.json` — release-please settings and files to bump
- `.release-please-manifest.json` — current released version per component
