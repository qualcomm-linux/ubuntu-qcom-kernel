#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${GITHUB_EVENT_NAME:?GITHUB_EVENT_NAME is required}"

emit_output() {
  local name="$1"
  local value="$2"
  echo "${name}=${value}" >> "$GITHUB_OUTPUT"
}

validate_common() {
  local run_id="$1"
  local run_attempt="$2"
  local pr_number="$3"
  local head_sha="$4"

  [[ "$run_id" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel workflow run ID: ${run_id}" >&2; exit 1; }
  [[ "$run_attempt" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel workflow run attempt: ${run_attempt}" >&2; exit 1; }
  [[ "$pr_number" =~ ^[0-9]+$ ]] || { echo "::error::Invalid pull request number: ${pr_number}" >&2; exit 1; }
  [[ "$head_sha" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid pull request head SHA: ${head_sha}" >&2; exit 1; }

  emit_output kernel-build-id "${run_id}-${run_attempt}"
  emit_output kernel-run-id "$run_id"
  emit_output kernel-run-attempt "$run_attempt"
  emit_output kernel-s3-prefix "pkg/premerge/pkg-linux-qcom-canonical"
  emit_output pr-number "$pr_number"
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

  validate_common "$run_id" "$run_attempt" "$pr_number" "$head_sha"
  emit_output kernel-conclusion "$conclusion"
  if [[ "$conclusion" == "success" ]]; then
    emit_output should-validate "true"
  else
    emit_output should-validate "false"
  fi
  exit 0
fi

if [[ "$GITHUB_EVENT_NAME" == "workflow_dispatch" ]]; then
  : "${KERNEL_RUN_ID:?KERNEL_RUN_ID is required}"
  : "${KERNEL_RUN_ATTEMPT:?KERNEL_RUN_ATTEMPT is required}"
  : "${PR_NUMBER:?PR_NUMBER is required}"
  : "${PR_HEAD_SHA:?PR_HEAD_SHA is required}"

  validate_common "$KERNEL_RUN_ID" "$KERNEL_RUN_ATTEMPT" "$PR_NUMBER" "$PR_HEAD_SHA"
  emit_output kernel-conclusion "success"
  emit_output should-validate "true"
  exit 0
fi

echo "::error::Unsupported event: ${GITHUB_EVENT_NAME}" >&2
exit 1
