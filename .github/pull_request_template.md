## What and why

<!-- What does this change, and what problem does it solve? Link the issue if there is one. -->

## How it was tested

<!-- Tests added or updated, hosts tried, manual steps. -->

## Checklist

- [ ] Builds without warnings
- [ ] `ctest` passes
- [ ] No allocation, locks, I/O or logging added on the audio thread
- [ ] No existing parameter IDs renamed or removed; state/pattern format version bumped with migration if saved data changed meaning
- [ ] Benchmark before/after included if the audio path changed
- [ ] `CHANGELOG.md` and docs updated
