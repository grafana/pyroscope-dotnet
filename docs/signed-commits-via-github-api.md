# Creating signed (verified) commits via the GitHub API

This repository requires all commits to be signed (enforced by a repository
ruleset: *"Commits must have verified signatures"*). Commits made from
ephemeral environments (Claude Code on the web, CI runners, bots) normally
cannot be signed locally because no GPG/SSH signing key is available there.

The workaround: **let GitHub create the commit server-side**. Any commit that
GitHub itself creates is automatically signed with GitHub's `web-flow` GPG key
and shows up as **Verified**. This is exactly how `release-please` and other
bots produce verified commits.

## API endpoints that produce signed commits

1. **GraphQL `createCommitOnBranch` mutation** — many files in a single
   commit; this is what `release-please` uses. Commits created through this
   mutation are always signed by GitHub:

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
   explicit author/committer identity, the commit is created unsigned and a
   signature ruleset will reject it with
   `409 Repository rule violations found — Commits must have verified signatures`.

3. **Merges performed by GitHub** (merge queue, squash/merge button,
   `PUT /repos/{owner}/{repo}/pulls/{n}/merge`) are also signed by GitHub.

## What does NOT produce signed commits

- The low-level **git data API** (`POST /repos/.../git/commits` +
  `PATCH /repos/.../git/refs/...`) creates *unverified* commits unless you
  supply your own `signature` field. The GitHub MCP `push_files` tool uses
  this API, so avoid it when signatures are required.
- Regular `git commit` + `git push` from a machine without a signing key.

## Verifying

Check the commit through the API (`get_commit` MCP tool or
`GET /repos/{owner}/{repo}/commits/{sha}`): the `verification` object must
have `verified: true` and `reason: "valid"`, with the signer being GitHub's
`web-flow` key (`noreply@github.com`).
