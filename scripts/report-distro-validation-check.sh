#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${MODE:?MODE is required}"
: "${REPOSITORY:?REPOSITORY is required}"
: "${COMMIT_SHA:?COMMIT_SHA is required}"
: "${PR_NUMBER:?PR_NUMBER is required}"
: "${REQUEST_ID:?REQUEST_ID is required}"
: "${KERNEL_BUILD_ID:?KERNEL_BUILD_ID is required}"
: "${IMAGE_S3_PREFIX:?IMAGE_S3_PREFIX is required}"
: "${DETAILS_URL:?DETAILS_URL is required}"

CHECK_NAME="qcom-distro-images/canonical-premerge"
CONCLUSION="${CONCLUSION:-}"
DISTRO_BUILD_ID="${DISTRO_BUILD_ID:-pending}"

emit_output() {
  [[ -n "${GITHUB_OUTPUT:-}" ]] || return 0
  echo "$1=$2" >> "$GITHUB_OUTPUT"
}

[[ "$MODE" =~ ^(start|complete)$ ]] || { echo "::error::Invalid Check Run mode: ${MODE}" >&2; exit 1; }
[[ "$REPOSITORY" == "qualcomm-linux/ubuntu-qcom-kernel" ]] || { echo "::error::Unexpected repository: ${REPOSITORY}" >&2; exit 1; }
[[ "$COMMIT_SHA" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid commit SHA: ${COMMIT_SHA}" >&2; exit 1; }
[[ "$PR_NUMBER" =~ ^[0-9]+$ ]] || { echo "::error::Invalid pull request number: ${PR_NUMBER}" >&2; exit 1; }
[[ "$KERNEL_BUILD_ID" =~ ^[0-9]+-[0-9]+$ ]] || { echo "::error::Invalid kernel build ID: ${KERNEL_BUILD_ID}" >&2; exit 1; }
[[ "$REQUEST_ID" == "${KERNEL_BUILD_ID}-${COMMIT_SHA}" ]] || { echo "::error::Request ID does not match the kernel build and commit SHA." >&2; exit 1; }
[[ "$IMAGE_S3_PREFIX" == "qualcomm-linux/pkg/premerge/ubuntu-qcom-kernel/${KERNEL_BUILD_ID}" ]] || { echo "::error::Unexpected image S3 prefix: ${IMAGE_S3_PREFIX}" >&2; exit 1; }
[[ "$DETAILS_URL" =~ ^https://github\.com/qualcomm-linux/(ubuntu-qcom-kernel|qcom-distro-images)/actions/runs/[0-9]+$ ]] || { echo "::error::Unexpected Check Run details URL: ${DETAILS_URL}" >&2; exit 1; }

if [[ "$MODE" == "complete" ]]; then
  [[ "$CONCLUSION" =~ ^(success|failure|cancelled|skipped)$ ]] || { echo "::error::Invalid Check Run conclusion: ${CONCLUSION}" >&2; exit 1; }
  [[ "$DISTRO_BUILD_ID" =~ ^[0-9]+-[0-9]+$ ]] || { echo "::error::Invalid distro build ID: ${DISTRO_BUILD_ID}" >&2; exit 1; }
fi

checks="$({
  gh api \
    --paginate \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "repos/${REPOSITORY}/commits/${COMMIT_SHA}/check-runs?filter=all&per_page=100"
} | jq -s --arg name "$CHECK_NAME" '[.[].check_runs[] | select(.name == $name)]')"

matching_checks="$(jq --arg request_id "$REQUEST_ID" '[.[] | select((.external_id // "") == $request_id)]' <<< "$checks")"
matching_count="$(jq 'length' <<< "$matching_checks")"

if [[ "$matching_count" -gt 1 ]]; then
  echo "::error::Multiple Check Runs use request ID ${REQUEST_ID}." >&2
  exit 1
fi

check_title="Canonical premerge distro validation"
check_summary="Building Canonical server and desktop images for PR #${PR_NUMBER}."
check_text="$IMAGE_S3_PREFIX"

if [[ "$MODE" == "start" ]]; then
  if [[ "$matching_count" == "1" ]]; then
    existing_status="$(jq -r '.[0].status' <<< "$matching_checks")"
    existing_url="$(jq -r '.[0].html_url' <<< "$matching_checks")"
    if [[ "$existing_status" =~ ^(in_progress|completed)$ ]]; then
      emit_output should-dispatch false
      echo "[INFO] Reusing Check Run for ${REQUEST_ID}: ${existing_url}"
      exit 0
    fi
    echo "::error::Check Run for ${REQUEST_ID} has unexpected status ${existing_status}." >&2
    exit 1
  fi

  while IFS=$'\t' read -r check_id external_id status; do
    [[ -n "$check_id" ]] || continue
    [[ "$status" == "in_progress" ]] || continue
    [[ "$external_id" != "$REQUEST_ID" ]] || continue
    [[ "$external_id" =~ ^[0-9]+-[0-9]+-${COMMIT_SHA}$ ]] || continue

    superseded_payload="$(jq -n \
      --arg details_url "$DETAILS_URL" \
      --arg title "$check_title" \
      --arg summary "Superseded by a newer Canonical premerge validation request." \
      --arg text "Superseding request ID: \`${REQUEST_ID}\`" \
      '{
        status: "completed",
        conclusion: "cancelled",
        details_url: $details_url,
        output: {
          title: $title,
          summary: $summary,
          text: $text
        }
      }')"
    gh api \
      --method PATCH \
      -H "Accept: application/vnd.github+json" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      "repos/${REPOSITORY}/check-runs/${check_id}" \
      --input - <<< "$superseded_payload" >/dev/null
  done < <(jq -r '.[] | [.id, (.external_id // ""), .status] | @tsv' <<< "$checks")

  create_payload="$(jq -n \
    --arg name "$CHECK_NAME" \
    --arg head_sha "$COMMIT_SHA" \
    --arg external_id "$REQUEST_ID" \
    --arg details_url "$DETAILS_URL" \
    --arg title "$check_title" \
    --arg summary "$check_summary" \
    --arg text "$check_text" \
    '{
      name: $name,
      head_sha: $head_sha,
      external_id: $external_id,
      status: "in_progress",
      details_url: $details_url,
      output: {
        title: $title,
        summary: $summary,
        text: $text
      }
    }')"
  check_run_url="$(gh api \
    --method POST \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "repos/${REPOSITORY}/check-runs" \
    --input - <<< "$create_payload" \
    --jq '.html_url')"
  emit_output should-dispatch true
  echo "[INFO] Started Check Run for ${REQUEST_ID}: ${check_run_url}"
  exit 0
fi

if [[ "$matching_count" != "1" ]]; then
  echo "::error::No Check Run exists for request ID ${REQUEST_ID}." >&2
  exit 1
fi

check_id="$(jq -r '.[0].id' <<< "$matching_checks")"
existing_status="$(jq -r '.[0].status' <<< "$matching_checks")"
existing_conclusion="$(jq -r '.[0].conclusion // ""' <<< "$matching_checks")"
existing_url="$(jq -r '.[0].html_url' <<< "$matching_checks")"

case "$CONCLUSION" in
  success)
    check_summary="Canonical server and desktop distro images passed for PR #${PR_NUMBER}."
    ;;
  failure)
    check_summary="Canonical distro image validation failed for PR #${PR_NUMBER}."
    ;;
  cancelled)
    check_summary="Canonical distro image validation was cancelled for PR #${PR_NUMBER}."
    ;;
  skipped)
    check_summary="Canonical distro image validation was skipped for PR #${PR_NUMBER}."
    ;;
esac

if [[ "$existing_status" == "completed" ]]; then
  existing_details_url="$(jq -r '.[0].details_url // ""' <<< "$matching_checks")"
  existing_title="$(jq -r '.[0].output.title // ""' <<< "$matching_checks")"
  existing_summary="$(jq -r '.[0].output.summary // ""' <<< "$matching_checks")"
  existing_text="$(jq -r '.[0].output.text // ""' <<< "$matching_checks")"
  if [[ "$existing_conclusion" == "$CONCLUSION" &&
        "$existing_details_url" == "$DETAILS_URL" &&
        "$existing_title" == "$check_title" &&
        "$existing_summary" == "$check_summary" &&
        "$existing_text" == "$check_text" ]]; then
    echo "[INFO] Check Run already completed for ${REQUEST_ID}: ${existing_url}"
    exit 0
  fi
  echo "::error::Completed Check Run for ${REQUEST_ID} does not match the validated callback result." >&2
  exit 1
fi

[[ "$existing_status" == "in_progress" ]] || { echo "::error::Check Run for ${REQUEST_ID} has unexpected status ${existing_status}." >&2; exit 1; }

complete_payload="$(jq -n \
  --arg conclusion "$CONCLUSION" \
  --arg details_url "$DETAILS_URL" \
  --arg title "$check_title" \
  --arg summary "$check_summary" \
  --arg text "$check_text" \
  '{
    status: "completed",
    conclusion: $conclusion,
    details_url: $details_url,
    output: {
      title: $title,
      summary: $summary,
      text: $text
    }
  }')"
check_run_url="$(gh api \
  --method PATCH \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "repos/${REPOSITORY}/check-runs/${check_id}" \
  --input - <<< "$complete_payload" \
  --jq '.html_url')"

echo "[INFO] Completed Check Run for ${REQUEST_ID}: ${check_run_url}"
