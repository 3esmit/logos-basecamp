<!--
Thanks for the PR. A short template so triage and review don't stall.
Delete sections that don't apply — don't leave placeholder text.
-->

## Summary

<!-- One or two sentences: what changes and why. -->

## Linked issues

<!-- e.g. Closes #123, Refs #456. Leave blank if none. -->

## Screenshots / recordings

<!-- Required for any user-visible UI change. Before/after if it's a fix. -->

## Test plan

<!-- How you validated the change. Tick what applies. -->

- [ ] `nix build .#app` succeeds
- [ ] `nix build .#smoke-test -L` passes
- [ ] `nix build .#integration-test -L` passes (if UI-visible)
- [ ] Doctests pass (`nix build .#doctests -L` if touched)
- [ ] Manual verification on:
  - [ ] Linux (AppImage)
  - [ ] Linux (Nix local build)
  - [ ] macOS
- [ ] N/A — docs/CI/tooling only

## Release notes

<!-- One line for the changelog. Prefix with fix:/feat:/chore:/docs:/test:/ci:
     to match the commit-style already used on master. -->

## Checklist

- [ ] Branch prefixed `fix/`, `feat/`, `chore/`, `docs/`, `test/`, or `ci/`
- [ ] No unrelated changes bundled in
- [ ] `CLAUDE.md` / `README.md` / `docs/` updated if behaviour or build steps changed
- [ ] Uncommitted binaries, screenshots, or `.DS_Store` files removed
