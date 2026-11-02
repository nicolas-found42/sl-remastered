## Development workflow

- **Start work:** read [the agent command guide](tools/parity/agent-tools.md) for developer lanes, hook/CI setup, experiment results, capacity checks, evidence indexing, Jev packets and trace inspection. Use `tools/parity/dev.py` for the applicable lane.
- **Run experiments or captures:** follow [shared admission](tools/parity/iteration.md) and [retention rules](tools/parity/storage.md). Preserve existing checkout edits and retained evidence; keep `ref/` read-only. Native/Stock captures use their specialized adapters to preserve foreground/input and exercise audio privately.
- **Resume parity work:** read the entire [full Port specification](docs/specs/full-port-1.0.0.md), then regenerate the current evidence index using the command guide. Check bound artifacts and unresolved requirements; historical checkpoint status does not authorize resuming a paused goal or prove completion.
- **Review and finish:** apply [CODING_STANDARDS.md](CODING_STANDARDS.md) during review. Run checks appropriate to the change and the required integration gate; report actual failures and unexecuted checks separately. Use bound evidence for Jev review and completion claims.

## Agent skills

### Issue tracker

Issues live in GitHub Issues for `nicolas-found42/sl-remastered` (via the `gh` CLI). See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
