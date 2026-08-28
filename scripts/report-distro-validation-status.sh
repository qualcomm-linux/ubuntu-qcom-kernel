#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${REPOSITORY:?REPOSITORY is required}"
: "${COMMIT_SHA:?COMMIT_SHA is required}"
: "${STATE:?STATE is required}"
: "${DESCRIPTION:?DESCRIPTION is required}"
: "${TARGET_URL:?TARGET_URL is required}"

STATUS_CONTEXT="${STATUS_CONTEXT:-qcom-distro-images/canonical-premerge}"

[[ "$REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || { echo "::error::Invalid repository: ${REPOSITORY}" >&2; exit 1; }
[[ "$COMMIT_SHA" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid commit SHA: ${COMMIT_SHA}" >&2; exit 1; }
[[ "$STATE" =~ ^(error|failure|pending|success)$ ]] || { echo "::error::Invalid status state: ${STATE}" >&2; exit 1; }

description="${DESCRIPTION:0:140}"

gh api \
  --method POST \
  "repos/${REPOSITORY}/statuses/${COMMIT_SHA}" \
  -f state="$STATE" \
  -f target_url="$TARGET_URL" \
  -f description="$description" \
  -f context="$STATUS_CONTEXT" \
  >/dev/null

echo "[INFO] Reported ${STATUS_CONTEXT}=${STATE} for ${COMMIT_SHA}."
