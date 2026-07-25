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
grep -Fq -- '-f target_commitish="$GITHUB_SHA"' "$workflow"
grep -Fq 'group: basecamp-release-${{ needs.release_contract.outputs.version }}' "$workflow"
grep -Fq 'GH_REPO: ${{ github.repository }}' "$workflow"
test "$(grep -Fc -- '--paginate --slurp' "$workflow")" -eq 2
test "$(grep -Fc 'git/matching-refs/tags/$' "$workflow")" -eq 3
test "$(grep -Fc "'any(.[][]; .tag_name == \$tag)'" "$workflow")" -eq 2
test "$(grep -Fc "'any(.[]; .ref == \$ref)'" "$workflow")" -eq 2
grep -Fq 'id: create_tag' "$workflow"
grep -Fq 'id: create_release' "$workflow"
grep -Fq -- '-f ref="refs/tags/$VERSION"' "$workflow"
grep -Fq -- '-f sha="$GITHUB_SHA"' "$workflow"
grep -Fq -- '-F draft=true' "$workflow"
grep -Fq -- '-F prerelease=true' "$workflow"
grep -Fq -- '-f make_latest=false' "$workflow"
grep -Fq -- '-F generate_release_notes=true' "$workflow"
grep -Fq -- '--data-binary @"artifacts/$name"' "$workflow"
grep -Fq '"https://uploads.github.com/repos/$GH_REPO/releases/$RELEASE_ID/assets?name=$name"' "$workflow"
grep -Fq 'sha256sum --check SHA256SUMS' "$workflow"
grep -Fq 'RELEASE_ID: ${{ steps.create_release.outputs.id }}' "$workflow"
grep -Fq 'RELEASE_OWNED: ${{ steps.create_release.outputs.owned }}' "$workflow"
grep -Fq '"repos/$GH_REPO/releases/$RELEASE_ID"' "$workflow"
grep -Fq '"repos/$GH_REPO/releases/assets/$asset_id"' "$workflow"
grep -Fq 'test "$tag_target" = "$GITHUB_SHA"' "$workflow"
grep -Fq 'test "$published_tag_target" = "$GITHUB_SHA"' "$workflow"
grep -Fq "steps.publish_release.outcome != 'success'" "$workflow"
grep -Fq 'if [[ "$RELEASE_OWNED" == "true" ]]; then' "$workflow"
grep -Fq 'can_delete_tag=false' "$workflow"
grep -Fq -- '--force-with-lease="refs/tags/$VERSION:$GITHUB_SHA"' "$workflow"
grep -Fq 'origin ":refs/tags/$VERSION"' "$workflow"
grep -Fq 'exit "$cleanup_failed"' "$workflow"
! grep -Fq -- '--cleanup-tag' "$workflow"
! grep -Fq 'softprops/action-gh-release' "$workflow"
! grep -Fq 'gh release ' "$workflow"
! grep -Fq 'select(.tag_name == $tag and .draft == true)' "$workflow"
! grep -Fq '"repos/$GH_REPO/git/refs/tags/$VERSION"' "$workflow"
! grep -Fq "release/**" "$workflow"
