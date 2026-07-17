# Creating signed (verified) commits from Claude Code / bots

This repository requires all commits to be signed (enforced by a repository
ruleset: *"Commits must have verified signatures"*). Commits made from
ephemeral environments (Claude Code on the web, CI runners, bots) normally
cannot be signed locally because no GPG/SSH signing key is available there.

There are two workarounds, both verified against this repository (see the
commit that added this file — it shows as **Verified**).

## Option 1: Claude Code remote sessions — plain `git push` just works

Claude Code on the web / remote sessions push through a Claude-managed git
proxy. The proxy **signs every pushed commit with an SSH key** registered
with GitHub, so the commits arrive as `verified: true, reason: "valid"` and
pass the signature ruleset. No special steps are needed:

```bash
git commit -m "..."
git push -u origin <branch>
```

This was verified empirically on this repo: a plain push from a Claude Code
remote session was accepted by the "verified signatures" ruleset, and the
resulting commit carries an `-----BEGIN SSH SIGNATURE-----` block with
`verification.verified: true`.

## Option 2: Let GitHub create the commit server-side (the release-please way)

Any commit that GitHub itself creates is automatically signed with GitHub's
`web-flow` GPG key. This is how `release-please` and other bots produce
verified commits.

1. **GraphQL `createCommitOnBranch` mutation** — many files in a single
   commit; commits created through this mutation are always signed by GitHub:

   ```graphql
   mutation ($input: CreateCommitOnBranchInput!) {
     createCommitOnBranch(input: $input) {
       commit { oid }
     }
   }
   ```

   with input:

   ```json
   {
     "branch": {
       "repositoryNameWithOwner": "grafana/pyroscope-dotnet",
       "branchName": "my-branch"
     },
     "expectedHeadOid": "<current head SHA of my-branch>",
     "message": { "headline": "my commit message" },
     "fileChanges": {
       "additions": [
         { "path": "path/to/file", "contents": "<base64-encoded content>" }
       ],
       "deletions": [{ "path": "path/to/removed-file" }]
     }
   }
   ```

2. **REST contents API** — one file per commit:
   `PUT /repos/{owner}/{repo}/contents/{path}`
   (also `DELETE .../contents/{path}` for deletions).

   ⚠️ Caveat: GitHub only signs these commits when the request does **not**
   override the `author`/`committer` fields. If a proxy or tool injects an
   explicit author/committer identity, the commit is created unsigned and
   the signature ruleset rejects it with
   `409 Repository rule violations found — Commits must have verified signatures`.
   This is exactly what happens with the GitHub MCP `create_or_update_file`
   tool inside Claude Code remote sessions — use Option 1 there instead.

3. **Merges performed by GitHub** (merge queue, squash/merge button,
   `PUT /repos/{owner}/{repo}/pulls/{n}/merge`) are also signed by GitHub.

## What does NOT produce signed commits

- The low-level **git data API** (`POST /repos/.../git/commits` +
  `PATCH /repos/.../git/refs/...`) creates *unverified* commits unless you
  supply your own `signature` field. The GitHub MCP `push_files` tool uses
  this API, so avoid it when signatures are required.
- Regular `git commit` + `git push` from a machine without a signing key
  (outside of Claude Code's signing proxy, see Option 1).

## Verifying

Check the commit through the API
(`GET /repos/{owner}/{repo}/commits/{sha}`): the `verification` object must
have `verified: true` and `reason: "valid"`.
