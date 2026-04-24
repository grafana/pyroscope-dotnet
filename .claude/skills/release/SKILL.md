---
description: Prepare and publish a new pyroscope-dotnet release
allowed-tools: Bash(git *), Bash(gh *), Read, Edit
---

# Release

Prepare a new `vX.Y.Z-pyroscope` release. The process is mostly automated by
three workflows; the human role is to review, write the changelog, and publish.

## Steps

### 1. Check the version bump PR

Every push to `main` triggers `release-version-bump-pr.yml`, which creates a PR
with the `prepare-release` label that bumps the patch version.

```bash
gh pr list --repo grafana/pyroscope-dotnet --label prepare-release
```

For a minor or major bump, trigger manually:

```bash
gh workflow run release-version-bump-pr.yml --repo grafana/pyroscope-dotnet -f version_bump=minor
```

### 2. Merge the version bump PR

Review and merge the PR. This triggers `release-draft.yml`, which:
- Creates a draft GitHub release with auto-generated notes
- Builds Docker images with a `-draft` tag suffix
- Uploads profiler tarballs and managed helper (`.dll`, `.nupkg`) to the draft

Wait for the workflow to complete:

```bash
gh run list --repo grafana/pyroscope-dotnet --workflow release-draft.yml
gh run watch <run-id> --repo grafana/pyroscope-dotnet --exit-status
```

### 3. Write the release changelog

Get the commits since the previous tag, skipping upstream dd-trace commits
(those with `#8xxx` or `#7xxx` PR numbers) and chore(deps) bumps:

```bash
git log vX.Y.(Z-1)-pyroscope..HEAD --oneline \
  | grep -v '#[78][0-9]\{3\}' \
  | grep -v 'chore(deps)'
```

For each merged PR, fetch its title:

```bash
gh pr view <number> --repo grafana/pyroscope-dotnet --json title -q .title
```

Format the changelog as one bullet per PR. Only list the **latest** upstream
merge (e.g. `merge upstream v3.39.0 (#NNN)`), not every intermediate one.

Include Docker Hub image links for the published images. The images are
published to `pyroscope/pyroscope-dotnet` on Docker Hub with tags
`X.Y.Z-glibc` and `X.Y.Z-musl`. Update the draft release:

```bash
gh release edit vX.Y.Z-pyroscope --repo grafana/pyroscope-dotnet \
  --notes "$(cat <<'EOF'
## What's Changed

- <description> (#NNN)
- ...

## Docker images

- [`pyroscope/pyroscope-dotnet:X.Y.Z-glibc`](https://hub.docker.com/r/pyroscope/pyroscope-dotnet/tags?name=X.Y.Z-glibc)
- [`pyroscope/pyroscope-dotnet:X.Y.Z-musl`](https://hub.docker.com/r/pyroscope/pyroscope-dotnet/tags?name=X.Y.Z-musl)

**Full Changelog**: https://github.com/grafana/pyroscope-dotnet/compare/vX.Y.(Z-1)-pyroscope...vX.Y.Z-pyroscope
EOF
)"
```

### 4. Publish the draft release

```bash
gh release edit vX.Y.Z-pyroscope --repo grafana/pyroscope-dotnet --draft=false
```

This triggers `release-publish.yml`, which:
- Promotes Docker images by creating tags without the `-draft` suffix
- Creates `latest-glibc` and `latest-musl` manifest tags
- Publishes the NuGet package to nuget.org
