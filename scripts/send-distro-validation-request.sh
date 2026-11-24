#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${VALIDATION_MODE:?VALIDATION_MODE is required}"
: "${DISTRO_REPOSITORY:?DISTRO_REPOSITORY is required}"
: "${KERNEL_REPOSITORY:?KERNEL_REPOSITORY is required}"
: "${KERNEL_RUN_ID:?KERNEL_RUN_ID is required}"
: "${KERNEL_RUN_ATTEMPT:?KERNEL_RUN_ATTEMPT is required}"
: "${KERNEL_BUILD_ID:?KERNEL_BUILD_ID is required}"
: "${KERNEL_S3_PREFIX:?KERNEL_S3_PREFIX is required}"
: "${SUBJECT:?SUBJECT is required}"
: "${HEAD_SHA:?HEAD_SHA is required}"
: "${REQUEST_ID:?REQUEST_ID is required}"

[[ "$VALIDATION_MODE" =~ ^canonical-(premerge|tag)$ ]] || { echo "::error::Invalid validation mode: ${VALIDATION_MODE}" >&2; exit 1; }
[[ "$DISTRO_REPOSITORY" == "qualcomm-linux/qcom-distro-images" ]] || { echo "::error::Unexpected distro repository." >&2; exit 1; }
[[ "$KERNEL_REPOSITORY" == "qualcomm-linux/ubuntu-qcom-kernel" ]] || { echo "::error::Unexpected kernel repository." >&2; exit 1; }
[[ "$KERNEL_RUN_ID" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel run ID." >&2; exit 1; }
[[ "$KERNEL_RUN_ATTEMPT" =~ ^[0-9]+$ ]] || { echo "::error::Invalid kernel run attempt." >&2; exit 1; }
[[ "$KERNEL_BUILD_ID" == "${KERNEL_RUN_ID}-${KERNEL_RUN_ATTEMPT}" ]] || { echo "::error::Kernel build ID does not match its run identity." >&2; exit 1; }
[[ "$KERNEL_S3_PREFIX" == "pkg/${VALIDATION_MODE#canonical-}/ubuntu-qcom-kernel" ]] || { echo "::error::Unexpected kernel S3 prefix: ${KERNEL_S3_PREFIX}" >&2; exit 1; }
[[ -n "$SUBJECT" ]] || { echo "::error::Empty validation subject." >&2; exit 1; }
[[ "$HEAD_SHA" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid commit SHA." >&2; exit 1; }
[[ "$REQUEST_ID" == "${KERNEL_BUILD_ID}-${HEAD_SHA}" ]] || { echo "::error::Request ID does not match the build and commit SHA." >&2; exit 1; }

# pr_number duplicates subject for receivers that predate it.
payload="$(jq -n \
  --arg event_type "${VALIDATION_MODE}-validation" \
  --arg validation_mode "$VALIDATION_MODE" \
  --arg kernel_repository "$KERNEL_REPOSITORY" \
  --arg kernel_run_id "$KERNEL_RUN_ID" \
  --arg kernel_run_attempt "$KERNEL_RUN_ATTEMPT" \
  --arg kernel_build_id "$KERNEL_BUILD_ID" \
  --arg kernel_s3_prefix "$KERNEL_S3_PREFIX" \
  --arg subject "$SUBJECT" \
  --arg head_sha "$HEAD_SHA" \
  --arg request_id "$REQUEST_ID" \
  '{
    event_type: $event_type,
    client_payload: ({
      validation_mode: $validation_mode,
      kernel_repository: $kernel_repository,
      kernel_run_id: $kernel_run_id,
      kernel_run_attempt: $kernel_run_attempt,
      kernel_build_id: $kernel_build_id,
      kernel_s3_prefix: $kernel_s3_prefix,
      subject: $subject,
      head_sha: $head_sha,
      request_id: $request_id
    } + (if $validation_mode == "canonical-premerge" then {pr_number: $subject} else {} end))
  }')"

gh api --method POST "repos/${DISTRO_REPOSITORY}/dispatches" --input - <<< "$payload"

echo "[INFO] Dispatched distro validation request ${REQUEST_ID}."
