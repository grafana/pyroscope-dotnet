#!/usr/bin/env bash
# prepare-merge.sh <ref> <base-branch>
# Steps 1-9 of the merge-upstream skill.
# <ref> can be a tag (v3.38.0), a commit hash, or a remote ref (upstream/main).
# <base-branch> is the local branch to base the merge on (e.g. main).
set -euxo pipefail

REF="${1:-}"
BASE="${2:-}"

if [ -z "$REF" ] || [ -z "$BASE" ]; then
  echo "Usage: $0 <ref> <base-branch>"
  echo "  e.g. $0 v3.38.0 main"
  echo "  e.g. $0 upstream/main main"
  echo "  e.g. $0 abc1234 main"
  exit 1
fi

# Derive a clean version string for the branch name.
# Strip leading 'v', replace '/' with '-' so remote refs like upstream/main become upstream-main.
VERSION="${REF#v}"
VERSION="${VERSION//\//-}"
BRANCH="kk/fork-update-${VERSION}"

# ── Step 1: Ensure upstream remote exists and fetch ──────────────────────────
step1_ensure_upstream() {
  echo "==> Step 1: Ensuring upstream remote and fetching..."
  if ! git remote get-url upstream &>/dev/null; then
    git remote add upstream https://github.com/DataDog/dd-trace-dotnet.git
  fi
  git fetch upstream --tags
}

# ── Step 2: Verify the ref resolves to a commit ──────────────────────────────
step2_verify_ref() {
  echo "==> Step 2: Verifying ref '${REF}' resolves to a commit..."
  if ! git rev-parse --verify "${REF}^{commit}" &>/dev/null; then
    echo "ERROR: '${REF}' does not resolve to a commit. Aborting."
    exit 1
  fi
  echo "    Ref '${REF}' confirmed."
}

# ── Step 3: Create merge branch from <base> ──────────────────────────────────
step3_create_branch() {
  echo "==> Step 3: Creating branch ${BRANCH} from ${BASE}..."
  if git rev-parse --verify "${BRANCH}" &>/dev/null; then
    git branch -D "${BRANCH}"
  fi
  git checkout -b "${BRANCH}" "${BASE}"
}

# ── Step 4: Start the merge ───────────────────────────────────────────────────
step4_start_merge() {
  echo "==> Step 4: Starting merge of '${REF}' (--no-commit --no-ff)..."
  # Conflicts are expected; we handle them in subsequent steps.
  git merge "${REF}" --no-commit --no-ff || true
}

# ── Step 5: Remove directories we don't carry in the fork ────────────────────
step5_remove_fork_dirs() {
  echo "==> Step 5: Removing directories not carried in the fork..."
  git rm -rf --ignore-unmatch \
    tracer \
    profiler/src/Demos \
    shared/src/Datadog.Trace.ClrProfiler.Native \
    shared/test \
    .azure-pipelines \
    .gitlab \
    docs \
    profiler/src/Tools \
    .gitlab-ci.yml
  # Remove C# integration/managed tests from profiler/test but keep C++ unit tests.
  git rm -rf --ignore-unmatch \
    profiler/test/Datadog.Profiler.IntegrationTests \
    profiler/test/RuntimeMetrics.Tests \
    profiler/test/Directory.Build.props
}

# ── Step 6: Remove files we replace with git submodules ──────────────────────
step6_remove_submodule_files() {
  echo "==> Step 6: Removing files replaced by git submodules..."
  git rm -rf --ignore-unmatch \
    build/cmake/FindSpdlog.cmake \
    shared/src/native-lib/spdlog \
    build/cmake/FindManagedLoader.cmake
}

# ── Step 7: Resolve DU conflicts (deleted-by-us / updated-by-upstream) ───────
step7_resolve_du_conflicts() {
  echo "==> Step 7: Resolving DU (deleted-by-us) conflicts..."
  git status --porcelain | grep '^DU ' | cut -c4- | xargs -r git rm -f
  echo "    DU conflicts done"
}

# ── Step 8: Remove upstream .github and .claude additions ────────────────────
step8_remove_upstream_additions() {
  echo "==> Step 8: Removing upstream .github and .claude additions..."

  local files
  files=$(git status --porcelain | grep '^A ' | grep '\.github' | cut -c4- || true)
  if [ -n "$files" ]; then
    echo "$files" | xargs -r git rm -f
    echo "    upstream .github additions removed"
  else
    echo "    no upstream .github additions found"
  fi

  local claude_files
  claude_files=$(git status --porcelain | grep '^A ' | grep '\.claude' | cut -c4- || true)
  if [ -n "$claude_files" ]; then
    echo "$claude_files" | xargs -r git rm -f
    echo "    upstream .claude additions removed"
  else
    echo "    no upstream .claude additions found"
  fi
}

# ── Step 9: Resolve .github/CODEOWNERS to our fork version ───────────────────
step9_resolve_codeowners() {
  echo "==> Step 9: Resolving .github/CODEOWNERS..."
  if [ -f .github/CODEOWNERS ]; then
    git checkout --ours .github/CODEOWNERS
    git add .github/CODEOWNERS
    echo "    .github/CODEOWNERS resolved to our fork version"
  else
    echo "    .github/CODEOWNERS not present, nothing to do"
  fi
}

# ── Main ──────────────────────────────────────────────────────────────────────
step1_ensure_upstream
step2_verify_ref
step3_create_branch
step4_start_merge
step5_remove_fork_dirs
step6_remove_submodule_files
step7_resolve_du_conflicts
step8_remove_upstream_additions
step9_resolve_codeowners

echo ""
echo "==> Steps 1-9 complete. Branch: ${BRANCH}"
echo "    Remaining conflicts (if any):"
git diff --name-only --diff-filter=U || true
echo ""
echo "    Next: resolve remaining conflicts (steps 2-3 in SKILL.md), then build (step 4)."
