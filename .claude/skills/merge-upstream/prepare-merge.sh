#!/usr/bin/env bash
# prepare-merge.sh <tag> <base-branch>
# Steps 1-9 of the merge-upstream skill:
#   1. Ensure upstream remote exists and fetch tags
#   2. Verify the target tag exists on the upstream remote
#   3. Create merge branch from <base-branch>
#   4. Start the merge (--no-commit --no-ff)
#   5. Remove directories we don't carry in the fork
#   6. Remove files we replace with git submodules
#   7. Resolve DU conflicts (deleted-by-us / updated-by-upstream)
#   8. Remove upstream .github and .claude additions
#   9. Resolve .github/CODEOWNERS — always keep the fork version
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TAG="${1:-}"
BASE="${2:-}"

if [ -z "$TAG" ] || [ -z "$BASE" ]; then
  echo "Usage: $0 <tag> <base-branch>"
  echo "  e.g. $0 v3.38.0 main"
  exit 1
fi

VERSION="${TAG#v}"
BRANCH="kk/fork-update-${VERSION}"

# ── Step 1: Ensure upstream remote exists and fetch tags ─────────────────────
echo "==> Step 1: Ensuring upstream remote and fetching tags..."
if ! git remote get-url upstream &>/dev/null; then
  git remote add upstream https://github.com/DataDog/dd-trace-dotnet.git
  echo "    upstream remote added"
else
  echo "    upstream remote already exists"
fi
git fetch upstream --tags

# ── Step 2: Verify the target tag exists on the upstream remote ──────────────
echo "==> Step 2: Verifying tag ${TAG} exists upstream..."
result=$(git ls-remote --tags upstream "refs/tags/${TAG}")
if [ -z "$result" ]; then
  echo "ERROR: Tag ${TAG} not found on upstream remote. Aborting."
  exit 1
fi
echo "    Tag ${TAG} confirmed."

# ── Step 3: Create merge branch from <base> ──────────────────────────────────
echo "==> Step 3: Creating branch ${BRANCH} from ${BASE}..."
if git rev-parse --verify "${BRANCH}" &>/dev/null; then
  git branch -D "${BRANCH}"
  echo "    Deleted existing branch ${BRANCH}"
fi
git checkout -b "${BRANCH}" "${BASE}"

# ── Step 4: Start the merge ───────────────────────────────────────────────────
echo "==> Step 4: Starting merge of ${TAG} (--no-commit --no-ff)..."
git merge "${TAG}" --no-commit --no-ff || true
# 'true' because conflicts are expected; we handle them in subsequent steps.

# ── Step 5: Remove directories we don't carry in the fork ────────────────────
echo "==> Step 5: Removing directories not carried in the fork..."
DIRS_TO_REMOVE=(
  tracer
  "profiler/src/Demos"
  "shared/src/Datadog.Trace.ClrProfiler.Native"
  "shared/test"
  ".azure-pipelines"
  ".gitlab"
  docs
  "profiler/test"
  "profiler/src/Tools"
)
for d in "${DIRS_TO_REMOVE[@]}"; do
  if git ls-files --error-unmatch "${d}" &>/dev/null 2>&1 || \
     git status --porcelain -- "${d}" | grep -q .; then
    git rm -rf --ignore-unmatch "${d}"
  fi
done
git rm -f --ignore-unmatch .gitlab-ci.yml

# ── Step 6: Remove files we replace with git submodules ──────────────────────
echo "==> Step 6: Removing files replaced by git submodules..."
git rm -rf --ignore-unmatch build/cmake/FindSpdlog.cmake
git rm -rf --ignore-unmatch shared/src/native-lib/spdlog
git rm -rf --ignore-unmatch build/cmake/FindManagedLoader.cmake

# ── Step 7: Resolve DU conflicts ─────────────────────────────────────────────
echo "==> Step 7: Resolving DU (deleted-by-us) conflicts..."
"${SCRIPT_DIR}/resolve-du-conflicts.sh"

# ── Step 8: Remove upstream .github and .claude additions ────────────────────
echo "==> Step 8: Removing upstream .github and .claude additions..."
"${SCRIPT_DIR}/remove-upstream-github.sh"

# ── Step 9: Resolve .github/CODEOWNERS — always keep our fork version ────────
echo "==> Step 9: Resolving .github/CODEOWNERS..."
if [ -f .github/CODEOWNERS ]; then
  git checkout --ours .github/CODEOWNERS 2>/dev/null || true
  git add .github/CODEOWNERS 2>/dev/null || true
  echo "    .github/CODEOWNERS resolved to our fork version"
else
  echo "    .github/CODEOWNERS not present, nothing to do"
fi

echo ""
echo "==> Steps 1-9 complete. Branch: ${BRANCH}"
echo "    Remaining conflicts (if any):"
git diff --name-only --diff-filter=U || true
echo ""
echo "    Next: resolve remaining conflicts (step 10-11), then build (step 12)."
