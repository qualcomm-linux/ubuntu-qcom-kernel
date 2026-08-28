#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${DISTRO_REPOSITORY:?DISTRO_REPOSITORY is required}"
: "${KERNEL_REPOSITORY:?KERNEL_REPOSITORY is required}"
: "${KERNEL_RUN_ID:?KERNEL_RUN_ID is required}"
: "${KERNEL_RUN_ATTEMPT:?KERNEL_RUN_ATTEMPT is required}"
: "${KERNEL_BUILD_ID:?KERNEL_BUILD_ID is required}"
: "${KERNEL_S3_PREFIX:?KERNEL_S3_PREFIX is required}"
: "${PR_NUMBER:?PR_NUMBER is required}"
: "${HEAD_SHA:?HEAD_SHA is required}"
: "${REQUEST_ID:?REQUEST_ID is required}"

[[ "$DISTRO_REPOSITORY" == "qualcomm-linux/qcom-distro-images" ]] || { echo "::error::Unexpected distro repository." >&2; exit 1; }
[[ "$KERNEL_REPOSITORY" == "qualcomm-linux/pkg-linux-qcom-canonical" ]] || { echo "::error::Unexpected kernel repository." >&2; exit 1; }
[[ "$KERNEL_RUN_ID" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel run ID." >&2; exit 1; }
[[ "$KERNEL_RUN_ATTEMPT" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel run attempt." >&2; exit 1; }
[[ "$KERNEL_BUILD_ID" == "${KERNEL_RUN_ID}-${KERNEL_RUN_ATTEMPT}" ]] || { echo "::error::Kernel build ID does not match its run identity." >&2; exit 1; }
[[ "$KERNEL_S3_PREFIX" == "pkg/premerge/pkg-linux-qcom-canonical" ]] || { echo "::error::Unexpected kernel S3 prefix." >&2; exit 1; }
[[ "$PR_NUMBER" =~ ^[0-9]+$ ]] || { echo "::error::Invalid pull request number." >&2; exit 1; }
[[ "$HEAD_SHA" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid pull request head SHA." >&2; exit 1; }
[[ "$REQUEST_ID" == "${KERNEL_BUILD_ID}-${HEAD_SHA}" ]] || { echo "::error::Request ID does not match the build and head SHA." >&2; exit 1; }

payload="$(jq -n \
  --arg event_type "canonical-premerge-validation" \
  --arg kernel_repository "$KERNEL_REPOSITORY" \
  --arg kernel_run_id "$KERNEL_RUN_ID" \
  --arg kernel_run_attempt "$KERNEL_RUN_ATTEMPT" \
  --arg kernel_build_id "$KERNEL_BUILD_ID" \
  --arg kernel_s3_prefix "$KERNEL_S3_PREFIX" \
  --arg pr_number "$PR_NUMBER" \
  --arg head_sha "$HEAD_SHA" \
  --arg request_id "$REQUEST_ID" \
  '{
    event_type: $event_type,
    client_payload: {
      kernel_repository: $kernel_repository,
      kernel_run_id: $kernel_run_id,
      kernel_run_attempt: $kernel_run_attempt,
      kernel_build_id: $kernel_build_id,
      kernel_s3_prefix: $kernel_s3_prefix,
      pr_number: $pr_number,
      head_sha: $head_sha,
      request_id: $request_id
    }
  }')"

gh api --method POST "repos/${DISTRO_REPOSITORY}/dispatches" --input - <<< "$payload"

echo "[INFO] Dispatched distro validation request ${REQUEST_ID}."
