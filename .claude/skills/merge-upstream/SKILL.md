---
description: Merge upstream dd-trace-dotnet changes into the pyroscope-dotnet fork
allowed-tools: Bash(git *), Bash(cmake *), Bash(make *), Bash(gh *), Bash(gh pr create *), Bash(git add -A && git commit *), Bash(*/.claude/skills/merge-upstream/*.sh), Read, Write, Edit, Glob, Grep
---

# Merge Upstream

Merge a specific upstream tag from DataDog/dd-trace-dotnet into the fork.

The user will provide the upstream tag to merge (e.g. `v3.35.0`). If not provided,
determine it automatically:

1. Find previously merged upstream versions:
   ```
   .claude/skills/merge-upstream/find-previous-versions.sh
   ```
2. Take the highest version found and increment the minor version (e.g. `v3.34.0` → `v3.35.0`).
   Then check upstream for the latest patch of that minor version by listing remote tags:
   ```
   git ls-remote --tags upstream 'refs/tags/v<major>.<next_minor>.*'
   ```
   Pick the highest patch version available (e.g. if `v3.35.0` and `v3.35.1` both exist,
   suggest `v3.35.1`). If no tags exist for that minor version, the tag doesn't exist yet — abort.
3. **Always confirm with the user** before proceeding — show the previous version
   found and the proposed next version, and ask the user to confirm or provide a different tag.
4. **Ask the user which branch to base the merge on.** Suggest `main` (recommended)
   but also offer the current branch (`git branch --show-current`) as an option.
   Use the chosen branch as `<base>` in step 3 below.

## Fork context

The upstream project (dd-trace-dotnet) contains both a **tracer** and a **profiler**.
Our fork only uses the **profiler** — the tracer is completely removed.

What we strip from upstream on every merge:
- `tracer/` — we don't use the Datadog tracer at all
- `profiler/src/Demos/`, `profiler/test/`, `profiler/src/Tools/` — upstream demo/test code we don't need
- `shared/test/` — upstream shared test code
- `.azure-pipelines/`, `.gitlab/` — upstream CI configs; we use GitHub Actions only
- `.github/` additions from upstream — we keep only our own pyroscope-specific workflows
- `.claude/` additions from upstream — we keep only our own claude configuration
- `.gitlab-ci.yml` — upstream CI config; we use GitHub Actions only
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
  `--amend` and `--force-push` on the merge commit of the branch created in step 1.
- Only push to the `kk/fork-update-*` branch created for this merge — never to `main`.

The scripts bellow should be executed as is, as executable, without passing it to the bash.  `.claude/skills/merge-upstream/find-previous-versions.sh` instead of `bash .claude/skills/merge-upstream/find-previous-versions.sh`

## Steps

1. **Run the prepare-merge script (steps 1-9)**
   ```
   .claude/skills/merge-upstream/prepare-merge.sh <ref> <base>
   ```
   `<ref>` can be a tag (`v3.38.0`), a commit hash, or a remote ref (`upstream/main`).
   `<base>` is the local branch to build on (e.g. `main`).

   This single script handles everything up through step 9:
   - Adds upstream remote (if missing) and fetches
   - Verifies `<ref>` resolves to a commit (aborts if not)
   - Creates the branch `kk/fork-update-<ref>` from `<base>` (re-creates if it already exists)
   - Starts the merge (`--no-commit --no-ff`)
   - Removes directories not carried in the fork (tracer, demos, tests, CI configs, docs, etc.)
   - Removes files replaced by git submodules (spdlog, ManagedLoader, etc.)
   - Resolves DU conflicts (deleted-by-us / updated-by-upstream)
   - Removes upstream `.github` and `.claude` additions
   - Resolves `.github/CODEOWNERS` to the fork version

   At the end the script prints the list of remaining conflicts, if any.

2. **Resolve conflicts in CMake files first**
   - List all conflicted files: `git diff --name-only --diff-filter=U`
   - Resolve CMake-related conflicts first (`CMakeLists.txt`, `*.cmake` files)
   - Then verify cmake configures successfully:
     ```
     .claude/skills/merge-upstream/cmake_configure.sh
     ```
     Fix any cmake errors before proceeding to other conflicts.

3. **Resolve remaining conflicts**
   - For version conflicts in binary/DLL/package references (e.g. NuGet versions,
     library versions, dependency pinning), always pick the **higher version**
   - Prefer our fork's changes for pyroscope-specific code
   - Ask the user when unsure which side to keep

4. **Verify the build** (only the targets we use)

    **Note:** The build may take significant time (several minutes). Use a generous timeout
    (e.g. 600000ms). The script always removes the old build directory first.

    ```
    .claude/skills/merge-upstream/build.sh
    ```

    Report any build errors and fix them before committing.

5. **Commit the merge**
    - `git add -A && git commit` with message: `merge upstream <tag>`

6. **Push and create a draft PR**
    ```
    git push -u origin kk/fork-update-<version>
    gh pr create --draft --repo grafana/pyroscope-dotnet --base <base> --label "upstream-merge" --title "merge upstream <tag>" --body "Merge upstream dd-trace-dotnet <tag> into the fork."
    ```

7. **Generate PR summary**
    After the PR is created, invoke the `merge-upstream-summary` skill to generate
    a detailed summary and update the PR description.
