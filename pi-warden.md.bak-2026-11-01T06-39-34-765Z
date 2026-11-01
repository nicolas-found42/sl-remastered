# No hardcoded secrets
Source code must not contain passwords, API keys, tokens, or connection URLs with credentials.
Read them from the environment, a function parameter, or the config module.

# Comments explain why, not what
A comment states a reason, a constraint, a workaround, or a non-obvious invariant. A comment
that restates what the next line plainly does is a violation.

# Errors are not swallowed
A `catch` block must handle the error, report it, or re-raise it. An empty catch block, or
one whose body is only a comment, is a violation.

# No partial implementations
Implement features fully. A comment that says "for now", "simplified", or "later", or a
stub body, is a violation. If a part genuinely cannot be done, say so in your reply instead
of stubbing it.

# Do not run destructive commands that erase uncommitted work
`git reset --hard`, `git checkout -- .`, `git clean -fd`, and similar commands that discard
untracked or uncommitted changes are forbidden. These destroy work that has no backup. If a
clean tree is needed, create a worktree instead or ask the user.

# Issues live in GitHub Issues for nicolas-found42/sl-remastered via the gh CLI
source: AGENTS.md:5
Issues live in GitHub Issues for `nicolas-found42/sl-remastered`, operated through the `gh` CLI.
A file that creates, links, or tells the reader to file issues in another tracker (Jira, Linear,
GitLab, Trello, a TODO/ISSUES file used as a tracker) or in another GitHub repository (for
example `gh issue create --repo mattpocock/skills` or a `github.com/<other-owner>/<repo>/issues`
link) is a violation.

# Triage labels use the default vocabulary
source: AGENTS.md:9
Triage labels are exactly `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`,
and `wontfix`. A file that applies or names a triage-state label with a different string, such
as `gh issue edit --add-label "triage"`, `needs-information`, `ready`, `agent-ready`, `afk`, or
`won't fix`, is a violation. Non-triage labels such as `wayfinder:map` are not covered by this
rule.

# One GLOSSARY.md at the repo root
source: AGENTS.md:13
paths: **/GLOSSARY.md, **/GLOSSARY-MAP.md
The project is single-context: there is one `GLOSSARY.md`, at the repo root. A `GLOSSARY.md` in
any subdirectory (for example `src/<context>/GLOSSARY.md` or `docs/GLOSSARY.md`), or a
`GLOSSARY-MAP.md` anywhere, is a violation.

# ADRs live in docs/adr/ at the repo root
source: AGENTS.md:13
paths: **/*.md
Architecture decision records belong in `docs/adr/` at the repo root. A file shaped like an ADR
(a numbered `NNNN-title.md` name, an "ADR" title, or Status/Context/Decision/Consequences
sections) placed anywhere else, such as `docs/decisions/`, `adr/`, or `src/<context>/docs/adr/`,
is a violation.
