#!/usr/bin/env bash
set -ex

VERSION_BUMP="${VERSION_BUMP:-patch}"

current_version=$(.github/scripts/version.sh)
echo "Current version: $current_version"
IFS='.' read -r major minor patch <<< "$current_version"

case "$VERSION_BUMP" in
  "major")
    major=$((major + 1))
    minor=0
    patch=0
    ;;
  "minor")
    minor=$((minor + 1))
    patch=0
    ;;
  "patch")
    patch=$((patch + 1))
    ;;
  *)
    echo "Error: invalid VERSION_BUMP value '${VERSION_BUMP}'. Expected one of: major, minor, patch." >&2
    exit 1
    ;;
esac

VERSION="${major}.${minor}.${patch}"
echo "New version: $VERSION"

sed -i -e "s/<PackageVersion>.*<\/PackageVersion>/<PackageVersion>${VERSION}<\/PackageVersion>/" "Pyroscope/Pyroscope/Pyroscope.csproj"
sed -i -e "s/<AssemblyVersion>.*<\/AssemblyVersion>/<AssemblyVersion>${VERSION}<\/AssemblyVersion>/" "Pyroscope/Pyroscope/Pyroscope.csproj"
sed -i -e "s/<FileVersion>.*<\/FileVersion>/<FileVersion>${VERSION}<\/FileVersion>/" "Pyroscope/Pyroscope/Pyroscope.csproj"
sed -i -e "s/#define PYROSCOPE_SPY_VERSION \".*\"/#define PYROSCOPE_SPY_VERSION \"${VERSION}\"/" "profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h"

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

BRANCH="prepare-release/v${VERSION}-pyroscope"
git checkout -b "${BRANCH}"
git add Pyroscope/Pyroscope/Pyroscope.csproj \
        profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h
git commit -m "version ${VERSION}"
git push --force origin "${BRANCH}"

existing_pr=$(gh pr list --head "${BRANCH}" --json number --jq '.[0].number // empty')
if [ -n "$existing_pr" ]; then
  echo "PR #${existing_pr} already exists for ${BRANCH}, branch has been force-pushed"
else
  gh pr create \
    --base main \
    --head "${BRANCH}" \
    --title "Release v${VERSION}-pyroscope" \
    --label "prepare-release" \
    --body "Automated release PR for version ${VERSION}.

The \`prepare-release\` label on this PR triggers draft release creation when merged.

Once this PR is merged, a draft release will be created automatically."
fi
