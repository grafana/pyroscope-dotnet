#!/usr/bin/env bash
# prepare-merge.sh <ref> <base-branch>
# Steps 1-9 of the merge-datadog skill.
# <ref> can be a tag (v3.38.0), a commit hash, or a remote ref (dd-trace-dotnet/main).
# <base-branch> is the local branch to base the merge on (e.g. main).
set -euxo pipefail

REF="${1:-}"
BASE="${2:-}"

if [ -z "$REF" ] || [ -z "$BASE" ]; then
  echo "Usage: $0 <ref> <base-branch>"
  echo "  e.g. $0 v3.38.0 main"
  echo "  e.g. $0 dd-trace-dotnet/main main"
  echo "  e.g. $0 abc1234 main"
  exit 1
fi

# Derive a clean version string for the branch name.
# Strip leading 'v', replace '/' with '-' so remote refs like dd-trace-dotnet/main become dd-trace-dotnet-main.
VERSION="${REF#v}"
VERSION="${VERSION//\//-}"
BRANCH="kk/fork-update-${VERSION}"

# ── Ensure remotes and fetch ─────────────────────────────────────────────────
ensure_remotes() {
  echo "==> Ensuring remotes and fetching..."
  local script_dir
  script_dir="$(cd "$(dirname "$0")" && pwd)"
  "$script_dir/find-previously-merged-version.sh" >/dev/null
}

# ── Verify the ref resolves to a commit ──────────────────────────────────────
verify_ref() {
  echo "==> Verifying ref '${REF}' resolves to a commit..."
  if ! git rev-parse --verify "${REF}^{commit}" &>/dev/null; then
    echo "ERROR: '${REF}' does not resolve to a commit. Aborting."
    exit 1
  fi
  echo "    Ref '${REF}' confirmed."
}

# ── Create merge branch from <base> ──────────────────────────────────────────
create_branch() {
  echo "==> Creating branch ${BRANCH} from ${BASE}..."
  if git rev-parse --verify "${BRANCH}" &>/dev/null; then
    git branch -D "${BRANCH}"
  fi
  git checkout -b "${BRANCH}" "${BASE}"
}

# ── Start the merge ──────────────────────────────────────────────────────────
start_merge() {
  echo "==> Starting merge of '${REF}' (--no-commit --no-ff)..."
  # Conflicts are expected; we handle them in subsequent steps.
  git merge "${REF}" --no-commit --no-ff || true
}

# ── Remove directories we don't carry in the fork ────────────────────────────
remove_fork_dirs() {
  echo "==> Removing directories not carried in the fork..."
  git rm -rf --ignore-unmatch \
    tracer \
    profiler/src/Demos \
    shared/src/Datadog.Trace.ClrProfiler.Native \
    shared/test \
    .azure-pipelines \
    .gitlab \
    docs \
    profiler/docs \
    profiler/src/ProfilerEngine/Datadog.Profiler.Native.Windows \
    profiler/src/Tools \
    .gitlab-ci.yml
  # Remove C# integration/managed tests from profiler/test but keep C++ unit tests.
  git rm -rf --ignore-unmatch \
    profiler/test/Datadog.Profiler.IntegrationTests \
    profiler/test/RuntimeMetrics.Tests \
    profiler/test/Directory.Build.props
}

# ── Remove files we replace with git submodules ──────────────────────────────
remove_submodule_files() {
  echo "==> Removing files replaced by git submodules..."
  git rm -rf --ignore-unmatch \
    build/cmake/FindSpdlog.cmake \
    shared/src/native-lib/spdlog \
    build/cmake/FindManagedLoader.cmake
}

# ── Resolve DU conflicts (deleted-by-us / updated-by-upstream) ───────────────
resolve_du_conflicts() {
  echo "==> Resolving DU (deleted-by-us) conflicts..."
  git status --porcelain | grep '^DU ' | cut -c4- | xargs -r git rm -f
  echo "    DU conflicts done"
}

# ── Remove upstream .github and .claude additions ────────────────────────────
remove_upstream_additions() {
  echo "==> Removing upstream .github and .claude additions..."

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

# ── Resolve .github/CODEOWNERS to our fork version ───────────────────────────
resolve_codeowners() {
  echo "==> Resolving .github/CODEOWNERS..."
  if [ -f .github/CODEOWNERS ]; then
    git checkout --ours .github/CODEOWNERS
    git add .github/CODEOWNERS
    echo "    .github/CODEOWNERS resolved to our fork version"
  else
    echo "    .github/CODEOWNERS not present, nothing to do"
  fi
}

# ── Main ──────────────────────────────────────────────────────────────────────
ensure_remotes
verify_ref
create_branch
start_merge
remove_fork_dirs
remove_submodule_files
resolve_du_conflicts
remove_upstream_additions
resolve_codeowners

echo ""
echo "==> Steps complete. Branch: ${BRANCH}"
echo "    Remaining conflicts (if any):"
git diff --name-only --diff-filter=U || true
echo ""
echo "    Next: resolve remaining conflicts (steps 2-3 in SKILL.md), then build (step 4)."
