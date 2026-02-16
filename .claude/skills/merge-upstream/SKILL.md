---
description: Merge upstream dd-trace-dotnet changes into the pyroscope-dotnet fork
allowed-tools: Bash(git *), Bash(cmake *), Bash(rm -rf _merge_upstreamm_build), Read, Write, Edit, Glob, Grep
---

# Merge Upstream

Merge a specific upstream tag from DataDog/dd-trace-dotnet into the fork.

The user will provide the upstream tag to merge (e.g. `v3.35.0`). If not provided,
determine it automatically:

1. Find previously merged upstream versions:
   ```
   git log --oneline main | grep -iE "merge (tag|upstream)" | grep -oP 'v\d+\.\d+\.\d+' | sort -V | tail -5
   ```
2. Take the highest version found and increment the minor version (e.g. `v3.34.0` → `v3.35.0`)
3. **Always confirm with the user** before proceeding — show the previous version
   found and the proposed next version, and ask the user to confirm or provide a different tag.

## Fork context

The upstream project (dd-trace-dotnet) contains both a **tracer** and a **profiler**.
Our fork only uses the **profiler** — the tracer is completely removed.

What we strip from upstream on every merge:
- `tracer/` — we don't use the Datadog tracer at all
- `profiler/src/Demos/`, `profiler/test/`, `profiler/src/Tools/` — upstream demo/test code we don't need
- `shared/test/` — upstream shared test code
- `.azure-pipelines/`, `.gitlab/` — upstream CI configs; we use GitHub Actions only
- `.github/` additions from upstream — we keep only our own pyroscope-specific workflows
- `docs/` — upstream documentation
- `shared/src/Datadog.Trace.ClrProfiler.Native` — not used in our fork
- `build/cmake/FindSpdlog.cmake`, `shared/src/native-lib/spdlog`, `build/cmake/FindManagedLoader.cmake` — we use git submodules for these instead

When resolving conflicts, keep this context in mind: if a conflict involves code paths
related to the tracer, Azure CI, upstream demos, or upstream .github workflows, our side
(deletion/absence) is correct.

**IMPORTANT — git safety rules:**
- Never create PRs or push to the upstream DataDog repo. All PRs must target
  `grafana/pyroscope-dotnet` with `--base main`.
- Never rebase or rewrite history. The only allowed destructive operations are
  `--amend` and `--force-push` on the merge commit of the branch created in step 2.
- Only push to the `kk/fork-update-*` branch created for this merge — never to `main`.

## Steps

1. **Ensure upstream remote exists and fetch tags**
   ```
   git remote add upstream https://github.com/DataDog/dd-trace-dotnet.git
   ```
   Skip if already present. Fetch upstream tags:
   ```
   git fetch upstream --tags
   ```

2. **Verify the target tag exists upstream**
   ```
   git tag -l <tag>
   ```
   If the tag does not exist, **abort** — inform the user and stop. Do not proceed.

3. **Create a merge branch from main**
   ```
   git checkout -b kk/fork-update-<version> main
   ```

4. **Start the merge (no commit, no fast-forward)**
   ```
   git merge <tag> --no-commit --no-ff
   ```

5. **Remove directories we don't carry in the fork**
   ```
   git rm -rf tracer
   git rm -rf profiler/src/Demos/
   git rm -rf shared/src/Datadog.Trace.ClrProfiler.Native
   git rm -rf shared/test
   git rm -rf .azure-pipelines
   git rm -rf .gitlab
   git rm -rf docs
   git rm -rf profiler/test
   git rm -rf profiler/src/Tools
   ```

6. **Remove files we replace with git submodules**
   ```
   git rm -rf build/cmake/FindSpdlog.cmake
   git rm -rf shared/src/native-lib/spdlog
   git rm -rf build/cmake/FindManagedLoader.cmake
   ```

7. **Clean up files deleted by us but modified upstream (DU conflicts)**
   ```
   git status --porcelain | grep '^DU ' | cut -c4- | xargs -r git rm -f
   ```

8. **Remove any upstream .github files that were added**
   ```
   git status --porcelain | grep '^A ' | grep .github | cut -c4- | xargs -r git rm -f
   ```

9. **Resolve `.github/CODEOWNERS` — always keep the fork version**
   If `.github/CODEOWNERS` has a conflict, always resolve it to the fork (grafana/pyroscope)
   version. The upstream CODEOWNERS contains DataDog-specific team ownership rules that are
   irrelevant to the fork. Check out our side and stage it:
   ```
   git checkout --ours .github/CODEOWNERS
   git add .github/CODEOWNERS
   ```

10. **Resolve conflicts in CMake files first**
   - List all conflicted files: `git diff --name-only --diff-filter=U`
   - Resolve CMake-related conflicts first (`CMakeLists.txt`, `*.cmake` files)
   - Then verify cmake configures successfully:
     ```
     CXX=clang++ CC=clang cmake -S . -B _merge_upstreamm_build
     ```
     Fix any cmake errors before proceeding to other conflicts.

11. **Resolve remaining conflicts**
   - For version conflicts in binary/DLL/package references (e.g. NuGet versions,
     library versions, dependency pinning), always pick the **higher version**
   - Prefer our fork's changes for pyroscope-specific code
   - Ask the user when unsure which side to keep

12. **Verify the build** (only the targets we use)

    ```
    cmake --build _merge_upstreamm_build --target Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64 -j$(nproc)
    ```

    Report any build errors and fix them before committing.

13. **Commit the merge**
    - `git add -A && git commit` with message: `merge upstream <tag>`
    - Do NOT push unless the user asks
