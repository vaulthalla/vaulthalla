<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->

# Idle CPU fix

The daemon no longer keeps one CPU core busy while it waits for the next vault sync. A background loop
re-checked the sync schedule nonstop, so an idle server showed a constant load of about 1.0 with one core at
100% (1.8.0 and earlier). It now sleeps until the next sync is due and still starts on-demand syncs
immediately.

## Vault permission overrides from the web console

Path-scoped allow/deny overrides on a vault role assignment (for example "deny downloads under `/finance/**`")
can now be listed, added, changed and removed from a vault's Access tab, not only with `vh vault role override`.
