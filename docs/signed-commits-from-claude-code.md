# Signed (verified) commits from Claude Code sessions

This repository requires all commits to be signed (enforced by a repository
ruleset: *"Commits must have verified signatures"*). This was assumed to
make Claude Code remote sessions (claude.ai/code) unusable here. It doesn't:

## Plain `git commit` + `git push` just works

**No setup is required.** Claude Code remote sessions come with commit
signing already configured out of the box — the session container ships a
global gitconfig that signs every commit locally with an SSH key registered
as a signing key on the `claude` GitHub account. For reference, this is
what the pre-provisioned config looks like (do not add this yourself):

```gitconfig
user.email=noreply@anthropic.com
user.signingkey=/home/claude/.ssh/commit_signing_key.pub
gpg.format=ssh
commit.gpgsign=true
```

So the ordinary workflow needs no special steps — and because signing
happens in git itself, **anything git can create is signed**: regular
commits, merge commits (`git merge --no-ff`), reverts, cherry-picks,
renames, file-mode changes, and submodule pointer bumps. 

Verified empirically on this repo: plain pushes from a Claude Code remote
session — including a two-parent merge commit — were accepted by the
"verified signatures" ruleset, and the resulting commits carry an
`-----BEGIN SSH SIGNATURE-----` block with
`verification.verified: true, reason: "valid"`
(`GET /repos/{owner}/{repo}/commits/{sha}`).

## ⚠️ Do NOT use the GitHub MCP file tools for commits

Inside Claude Code sessions the GitHub MCP tools that create commits
through the API produce *unsigned* commits and get rejected by the ruleset
with `409 Repository rule violations found — Commits must have verified
signatures`:

- `create_or_update_file` / `delete_file` (REST contents API): GitHub would
  normally sign these server-side, but the MCP proxy injects an explicit
  author/committer identity, which suppresses GitHub's automatic signing.
- `push_files` (low-level git data API): never signed.

Use plain `git commit` + `git push` instead.

**TL;DR: nothing to configure — just use normal git commands and avoid the
MCP file-commit tools.**
