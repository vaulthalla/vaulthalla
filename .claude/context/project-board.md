# Project board (GitHub Projects v2)

The Roadmap board (org project #4, public) is meant to be read and driven by agents. Its
meaning lives in `.github/project/schema.yml`: fields, options, `area:*` labels, views and the
Ready gate. `tools/project/board.py` reconciles a project to it. Read the schema before triaging
or picking up board work; do not infer field meaning from option names.

## Model

- **On the issue** (travels with it): Issue Type (org-level: Bug, Feature, Task, Docs, Packaging,
  Security, Research), sub-issues (anything L+), blocked-by (replaces a "Blocked" status),
  `area:*` labels (where the code is; each area lists paths, context docs, seed Hazards/Proof),
  `needs:repro` / `needs:decision`.
- **On the board**: Status (Inbox → Triaged → Ready → Claimed → In review → Done, plus Parked),
  Priority, Size, Clarity, Autonomy, Hazards (multi; `none` = assessed, empty = unknown),
  Proof (multi; = `tools/dev/verify.sh` profiles + `lab-smoke`), Agent, Handoff, Last touched.
- Automation must key off labels/issue events: Actions can trigger on `issues` (`labeled`,
  ...), but project field changes (`projects_v2_item`) only reach org webhooks and Apps.

## Working an item (agents)

1. Pick from the **Agent queue** view (`status:Ready`, Autonomy Autonomous or Checkpointed).
   Autonomy is capped by Hazards (`max_autonomy` in the schema). Checkpointed means stop for
   approval on the plan and before any Hazard action.
2. Claim: Status = Claimed, Agent = your session/agent name, Last touched = today.
3. Read the context docs of every `area:*` label; obey each Hazard's rule (they mirror the
   CLAUDE.md hard rules).
4. Prove with every Proof value; report in the CLAUDE.md handoff format.
5. Leave: PR open → Status = In review; stopping early → Handoff = next action (or a link to a
   handoff comment), keep Last touched current. Claims older than 7 days are reclaimable.

Field writes: `gh project item-edit` (single-select/text/date), multi-select only via
`gh api graphql` (`updateProjectV2ItemFieldValue` with `multiSelectOptionIds`). The installed
`gh` is 2.45.0 (Ubuntu); it has no native multi-select, view, issue-type or sub-issue commands.

## Tool

```bash
python3 tools/project/board.py lint                 # Ready gate + stale claims, read-only
python3 tools/project/board.py apply                # dry run against #4 (all commands default to dry run)
python3 tools/project/board.py snapshot > snap.json # backup before production writes
```

Writes need `--apply`; writes to #4 also need `--production`; repo labels need `--labels`;
issue label writes in `migrate` need `--issue-writes`. `apply` preserves option ids (renames
keep values), keeps retired options while in use, never drops unknown options, and compares
view columns as a set (GitHub imposes its own column order). Group-by, sort and board column
field are not settable via API: `apply` prints them as a by-hand list (tracked in #181).
`migrate` is schema-driven: it moves items off `retire_options` and seeds empty Hazards/Proof
from area labels; retiring an option is `apply` → `migrate` → `apply`.
`ProjectV2.items(query:)` evaluates view filter syntax, which is how filters were verified.
`seed-sandbox` copies items into a private copy (`gh project copy`) for rehearsing schema
changes; it strips the copy's built-in workflows first (the copy brings Auto-close enabled).

Gotchas: `gh issue create --project` fails on gh 2.45 (it queries the removed classic Projects
API); create the issue, then add it (#4's auto-add workflow usually picks it up). Draft items
cannot carry labels, so `migrate` skips them.

## Rollout status

Schema v1 is live on #4 since 2026-10-09 (rehearsed first on a since-deleted sandbox copy).
Pre-rollout snapshot: `.claude/scratch/board/prod-4-pre-rollout-2026-10-09.json` (gitignored;
holds the deleted Domain/Subsystem/Contributor Fit/Estimate values). Open triage left by the
migration (see `board.py lint`): most open issues lack an issue type; #96 is Ready at XL;
#118 is Claimed without Agent; #156 needs a blocked-by link and sub-issues. View group/sort/
column settings by hand: #181 (Parked, low priority while there are no outside contributors).
