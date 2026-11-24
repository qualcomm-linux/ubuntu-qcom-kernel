#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
# Resolves the context for one distro image validation. The mode it emits
# decides the rest of the names:
#   Check Run    qcom-distro-images/<mode>
#   packages     pkg/<mode without canonical->/ubuntu-qcom-kernel
#   dispatches   <mode>-validation, <mode>-distro-result
#
set -euo pipefail

: "${GITHUB_EVENT_NAME:?GITHUB_EVENT_NAME is required}"

emit_output() {
  local name="$1"
  local value="$2"
  echo "${name}=${value}" >> "$GITHUB_OUTPUT"
}

validate_subject() {
  local mode="$1"
  local subject="$2"

  case "$mode" in
    canonical-premerge)
      [[ "$subject" =~ ^[0-9]+$ ]] || { echo "::error::Invalid pull request number: ${subject}" >&2; exit 1; }
      ;;
    canonical-tag)
      [[ "$subject" =~ ^Ubuntu-qcom-[0-9]+\.[0-9]+\.[0-9]+-[0-9]+\.[0-9]+$ ]] || { echo "::error::Invalid Canonical tag: ${subject}" >&2; exit 1; }
      ;;
    *)
      echo "::error::Invalid validation mode: ${mode}" >&2; exit 1
      ;;
  esac
}

validate_common() {
  local mode="$1"
  local subject="$2"
  local run_id="$3"
  local run_attempt="$4"
  local head_sha="$5"

  validate_subject "$mode" "$subject"
  [[ "$run_id" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel workflow run ID: ${run_id}" >&2; exit 1; }
  [[ "$run_attempt" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel workflow run attempt: ${run_attempt}" >&2; exit 1; }
  [[ "$head_sha" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid commit SHA: ${head_sha}" >&2; exit 1; }

  emit_output validation-mode "$mode"
  emit_output subject "$subject"
  emit_output kernel-build-id "${run_id}-${run_attempt}"
  emit_output kernel-run-id "$run_id"
  emit_output kernel-run-attempt "$run_attempt"
  emit_output kernel-s3-prefix "pkg/${mode#canonical-}/ubuntu-qcom-kernel"
  emit_output head-sha "$head_sha"
  emit_output request-id "${run_id}-${run_attempt}-${head_sha}"
}

if [[ "$GITHUB_EVENT_NAME" == "workflow_run" ]]; then
  : "${GITHUB_EVENT_PATH:?GITHUB_EVENT_PATH is required}"

  action="$(jq -r '.action' "$GITHUB_EVENT_PATH")"
  event="$(jq -r '.workflow_run.event' "$GITHUB_EVENT_PATH")"
  workflow_path="$(jq -r '.workflow_run.path' "$GITHUB_EVENT_PATH")"
  repository="$(jq -r '.workflow_run.repository.full_name' "$GITHUB_EVENT_PATH")"

  [[ "$event" == "pull_request" ]] || { echo "::error::Unexpected triggering event: ${event}" >&2; exit 1; }
  [[ "$workflow_path" == ".github/workflows/premerge-pr.yml" ]] || { echo "::error::Unexpected triggering workflow path: ${workflow_path}" >&2; exit 1; }
  [[ "$repository" == "$GITHUB_REPOSITORY" ]] || { echo "::error::Unexpected triggering repository: ${repository}" >&2; exit 1; }

  [[ "$action" == "completed" ]] || { echo "::error::Unexpected workflow_run activity: ${action}" >&2; exit 1; }

  run_id="$(jq -r '.workflow_run.id' "$GITHUB_EVENT_PATH")"
  run_attempt="$(jq -r '.workflow_run.run_attempt' "$GITHUB_EVENT_PATH")"
  conclusion="$(jq -r '.workflow_run.conclusion' "$GITHUB_EVENT_PATH")"
  head_sha="$(jq -r '.workflow_run.head_sha' "$GITHUB_EVENT_PATH")"

  [[ "$head_sha" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid triggering workflow head SHA: ${head_sha}" >&2; exit 1; }

  # GET /commits/{sha}/pulls does not reliably resolve commits that only
  # exist on a fork (reachable in this repo solely via the hidden
  # refs/pull/<n>/head ref, never an actual branch). List open pull
  # requests against resolute-qcom-devel directly and match on head.sha
  # instead, which works the same for same-repo and fork-originated PRs.
  pull_requests="$(
    gh api --paginate "repos/${GITHUB_REPOSITORY}/pulls" \
      -H "Accept: application/vnd.github+json" \
      -X GET \
      -f state=open \
      -f base=resolute-qcom-devel \
      | jq -s --arg sha "$head_sha" '[.[][] | select(.head.sha == $sha)]'
  )"
  pr_count="$(jq 'length' <<< "$pull_requests")"
  [[ "$pr_count" == "1" ]] || {
    echo "::error::Expected exactly one open resolute-qcom-devel pull request for ${head_sha}; found ${pr_count}." >&2
    exit 1
  }
  pr_number="$(jq -r '.[0].number' <<< "$pull_requests")"

  validate_common canonical-premerge "$pr_number" "$run_id" "$run_attempt" "$head_sha"
  emit_output kernel-conclusion "$conclusion"
  if [[ "$conclusion" == "success" ]]; then
    emit_output should-validate "true"
  else
    emit_output should-validate "false"
  fi
  exit 0
fi

# Used by the sync workflow for a freshly mirrored tag, and to re-validate by
# hand. A re-run gets a new run ID, so its request ID cannot collide with the
# Check Run of the attempt it replaces.
if [[ "$GITHUB_EVENT_NAME" == "workflow_dispatch" ]]; then
  : "${VALIDATION_MODE:?VALIDATION_MODE is required}"
  : "${SUBJECT:?SUBJECT is required}"
  : "${KERNEL_RUN_ID:?KERNEL_RUN_ID is required}"
  : "${KERNEL_RUN_ATTEMPT:?KERNEL_RUN_ATTEMPT is required}"

  # The subject reaches a URL path below, so check it first.
  validate_subject "$VALIDATION_MODE" "$SUBJECT"

  if [[ "$VALIDATION_MODE" == "canonical-tag" ]]; then
    # Dereferences an annotated tag to its commit in one call.
    head_sha="$(gh api "repos/${GITHUB_REPOSITORY}/commits/${SUBJECT}" --jq '.sha')"
  else
    : "${COMMIT_SHA:?COMMIT_SHA is required}"
    head_sha="$COMMIT_SHA"
  fi

  validate_common "$VALIDATION_MODE" "$SUBJECT" "$KERNEL_RUN_ID" "$KERNEL_RUN_ATTEMPT" "$head_sha"
  emit_output kernel-conclusion "success"
  emit_output should-validate "true"
  exit 0
fi

echo "::error::Unsupported event: ${GITHUB_EVENT_NAME}" >&2
exit 1
