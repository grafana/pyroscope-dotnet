---
description: Summarize an upstream merge PR with diff overview and profiler-related commit log
allowed-tools: Bash(gh *), Bash(git *), Read, Glob, Grep
---

# Merge Upstream Summary

Generate a summary for a PR created by the `merge-upstream` skill and update
the PR description.

If the conversation already contains PR information (number, URL, branch name),
use that directly — do NOT run `gh pr list` to re-discover it.

Only as a last resort, if no PR info is available in context, find the most
recent open PR with the `upstream-merge` label:
```
gh pr list --repo grafana/pyroscope-dotnet --label upstream-merge --state open --limit 1
```

## Steps

1. **Identify the PR and upstream tag**
   - Get PR metadata: `gh pr view <pr> --repo grafana/pyroscope-dotnet --json number,title,baseRefName,headRefName,url`
   - Extract the upstream tag from the PR title (e.g. `merge upstream v3.35.0` → `v3.35.0`)

2. **Get the PR diff and find profiler-related changes**
   ```
   gh pr diff <pr> --repo grafana/pyroscope-dotnet
   ```
   Read through the diff and identify all changes that touch `profiler/` or
   `Profiler` paths. Also collect the commit SHAs from the PR:
   ```
   gh pr view <pr> --repo grafana/pyroscope-dotnet --json commits --jq '.commits[] | [.oid[:8], .oid, .messageHeadline] | @tsv'
   ```
   For each profiler-related commit, build the upstream GitHub URL:
   `https://github.com/DataDog/dd-trace-dotnet/commit/<full-sha>`

3. **Write the summary**

   Compose a PR description in this format:

   ```markdown
   Merge upstream dd-trace-dotnet <tag> into the fork.

   ## Summary

   <2-5 sentence high-level summary of what changed in the profiler, based on
   the diff and commit messages. Focus on functional changes, not mechanical
   merge details.>

   ## Upstream profiler commits

   | Commit | Message |
   |--------|---------|
   | [<short-sha>](https://github.com/DataDog/dd-trace-dotnet/commit/<full-sha>) | <first line of commit message> |
   | ... | ... |

   If there are no profiler-related commits, write "No profiler-specific commits
   in this release." instead of the table.
   ```

4. **Update the PR description**
   ```
   gh pr edit <pr> --repo grafana/pyroscope-dotnet --body "<summary>"
   ```
   Use a heredoc to pass the body to avoid quoting issues.

5. **Report back** — show the user the generated summary and confirm the PR was
   updated. Include the PR URL.
