#!/usr/bin/env bash

set -euo pipefail

workflow=".github/workflows/build.yml"
version="$(tr -d '\n' < VERSION)"

[[ "$version" =~ ^0\.[0-9]+\.[0-9]+-alpha\.[1-9][0-9]*$ ]]
grep -Fq "## [${version}]" CHANGELOG.md
grep -Fq 'workflow_dispatch:' "$workflow"
test "$(grep -Fc "if: github.event_name == 'workflow_dispatch'" "$workflow")" -eq 2
test "$(grep -Fc 'needs: release_contract' "$workflow")" -eq 4
grep -Fq 'test "$GITHUB_REF" = "refs/heads/master"' "$workflow"
grep -Fq 'sha256sum ./*.AppImage ./*.app.tar.gz > SHA256SUMS' "$workflow"
grep -Fq 'target_commitish: ${{ github.sha }}' "$workflow"
grep -Fq 'group: basecamp-release-${{ needs.release_contract.outputs.version }}' "$workflow"
grep -Fq 'id: release_guard' "$workflow"
grep -Fq 'draft: true' "$workflow"
grep -Fq 'prerelease: true' "$workflow"
grep -Fq 'make_latest: false' "$workflow"
grep -Fq 'fail_on_unmatched_files: true' "$workflow"
grep -Fq 'sha256sum --check SHA256SUMS' "$workflow"
grep -Fq 'test "$tag_target" = "$GITHUB_SHA"' "$workflow"
grep -Fq 'gh release edit "$VERSION" --draft=false --prerelease --latest=false' "$workflow"
grep -Fq "if: failure() && steps.release_guard.outputs.cleanup == 'true'" "$workflow"
grep -Fq 'gh release delete "$VERSION" --yes --cleanup-tag || true' "$workflow"
! grep -Fq "release/**" "$workflow"
