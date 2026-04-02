---
description: Prepare and publish a new pyroscope-dotnet release
allowed-tools: Bash(git *), Bash(gh *), Read, Edit
---

# Release

Prepare a new `vX.Y.Z-pyroscope` release. Follow these steps in order.

## Steps

### 1. Determine the next version

Check the current version in `Pyroscope/Pyroscope/Pyroscope.csproj` and
`profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h`,
then increment the patch number (e.g. `0.14.3` → `0.14.4`).

### 2. Create a release branch and bump the version

```bash
git checkout main && git pull
git checkout -b prepare-vX.Y.Z-pyroscope
```

Edit **both** files:

- `Pyroscope/Pyroscope/Pyroscope.csproj` — update `PackageVersion`,
  `AssemblyVersion`, and `FileVersion`
- `profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h` —
  update `PYROSCOPE_SPY_VERSION`

Commit and push:

```bash
git add Pyroscope/Pyroscope/Pyroscope.csproj \
        profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h
git commit -m "prepare vX.Y.Z-pyroscope"
git push -u origin prepare-vX.Y.Z-pyroscope
```

### 3. Open a PR and wait for CI

```bash
gh pr create --repo grafana/pyroscope-dotnet \
  --title "prepare vX.Y.Z-pyroscope" \
  --body "Bump version to vX.Y.Z-pyroscope."
```

Watch CI until all checks pass:

```bash
gh run list --repo grafana/pyroscope-dotnet --branch prepare-vX.Y.Z-pyroscope
gh run watch <run-id> --repo grafana/pyroscope-dotnet --exit-status
```

Wait for the PR to be merged before continuing.

### 4. Tag the release

```bash
git checkout main && git pull
git tag vX.Y.Z-pyroscope
git push upstream vX.Y.Z-pyroscope
```

### 5. Write the release changelog

Get the commits since the previous tag, skipping upstream dd-trace commits
(those with `#8xxx` or `#7xxx` PR numbers) and chore(deps) bumps:

```bash
git log vX.Y.(Z-1)-pyroscope..vX.Y.Z-pyroscope --oneline \
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
`X.Y.Z-glibc` and `X.Y.Z-musl`:

```markdown
## What's Changed

- <description> (#NNN)
- ...

## Docker images

- [`pyroscope/pyroscope-dotnet:X.Y.Z-glibc`](https://hub.docker.com/r/pyroscope/pyroscope-dotnet/tags?name=X.Y.Z-glibc)
- [`pyroscope/pyroscope-dotnet:X.Y.Z-musl`](https://hub.docker.com/r/pyroscope/pyroscope-dotnet/tags?name=X.Y.Z-musl)

**Full Changelog**: https://github.com/grafana/pyroscope-dotnet/compare/vX.Y.(Z-1)-pyroscope...vX.Y.Z-pyroscope
```

### 6. Publish the GitHub release

The tag may already have a draft release created by CI. Use `edit` to update it;
fall back to `create` if it does not exist:

```bash
gh release edit vX.Y.Z-pyroscope --repo grafana/pyroscope-dotnet \
  --title "vX.Y.Z-pyroscope" \
  --notes "$(cat <<'EOF'
<changelog>
EOF
)"
```
