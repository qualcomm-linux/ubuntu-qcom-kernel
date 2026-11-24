#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

: "${GITHUB_EVENT_PATH:?GITHUB_EVENT_PATH is required}"
: "${BUCKET:?BUCKET is required}"

emit_output() {
  echo "$1=$2" >> "$GITHUB_OUTPUT"
}

payload="$(jq '.client_payload' "$GITHUB_EVENT_PATH")"
request_id="$(jq -r '.request_id' <<< "$payload")"
kernel_build_id="$(jq -r '.kernel_build_id' <<< "$payload")"
kernel_s3_prefix="$(jq -r '.kernel_s3_prefix' <<< "$payload")"
head_sha="$(jq -r '.head_sha' <<< "$payload")"
distro_result="$(jq -r '.distro_result' <<< "$payload")"
distro_run_id="$(jq -r '.distro_run_id' <<< "$payload")"
distro_run_attempt="$(jq -r '.distro_run_attempt' <<< "$payload")"
# Callbacks that predate these fields are always pre-merge.
validation_mode="$(jq -r '.validation_mode // "canonical-premerge"' <<< "$payload")"
subject="$(jq -r '.subject // .pr_number' <<< "$payload")"

[[ "$(jq -r '.action' "$GITHUB_EVENT_PATH")" == "${validation_mode}-distro-result" ]] || { echo "::error::Unexpected callback event type." >&2; exit 1; }
[[ "$kernel_build_id" =~ ^[0-9]+-[0-9]+$ ]] || { echo "::error::Invalid kernel build ID." >&2; exit 1; }
[[ "$kernel_s3_prefix" == "pkg/${validation_mode#canonical-}/ubuntu-qcom-kernel" ]] || { echo "::error::Unexpected kernel S3 prefix: ${kernel_s3_prefix}" >&2; exit 1; }
[[ "$head_sha" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid commit SHA." >&2; exit 1; }
[[ "$distro_result" =~ ^(success|failure|cancelled|skipped)$ ]] || { echo "::error::Invalid distro result." >&2; exit 1; }
[[ "$distro_run_id" =~ ^[0-9]+$ && "$distro_run_attempt" =~ ^[0-9]+$ ]] || { echo "::error::Invalid distro run identity." >&2; exit 1; }
[[ "$request_id" == "${kernel_build_id}-${head_sha}" ]] || { echo "::error::Request ID does not match the kernel build and commit SHA." >&2; exit 1; }

IFS=- read -r kernel_run_id kernel_run_attempt <<< "$kernel_build_id"
kernel_run="$(gh api "repos/qualcomm-linux/ubuntu-qcom-kernel/actions/runs/${kernel_run_id}")"

case "$validation_mode" in
  canonical-premerge)
    [[ "$subject" =~ ^[0-9]+$ ]] || { echo "::error::Invalid pull request number." >&2; exit 1; }

    jq -e \
      --argjson attempt "$kernel_run_attempt" \
      --arg head_sha "$head_sha" \
      '.event == "pull_request" and
       .path == ".github/workflows/premerge-pr.yml" and
       .run_attempt == $attempt and
       .conclusion == "success" and
       .head_sha == $head_sha' \
      <<< "$kernel_run" >/dev/null || {
        echo "::error::Kernel workflow run does not match the callback context." >&2
        exit 1
      }

    # GET /commits/{sha}/pulls does not reliably resolve commits that only
    # exist on a fork (reachable in this repo solely via the hidden
    # refs/pull/<n>/head ref, never an actual branch). List open pull
    # requests against resolute-qcom-devel directly and match on head.sha
    # instead, which works the same for same-repo and fork-originated PRs.
    pull_requests="$(
      gh api --paginate "repos/qualcomm-linux/ubuntu-qcom-kernel/pulls" \
        -H "Accept: application/vnd.github+json" \
        -X GET \
        -f state=open \
        -f base=resolute-qcom-devel \
        | jq -s --arg sha "$head_sha" '[.[][] | select(.head.sha == $sha)]'
    )"
    [[ "$(jq 'length' <<< "$pull_requests")" == "1" ]] || { echo "::error::Unable to identify one matching Canonical pull request." >&2; exit 1; }
    [[ "$(jq -r '.[0].number' <<< "$pull_requests")" == "$subject" ]] || { echo "::error::Callback pull request number does not match the kernel commit." >&2; exit 1; }
    ;;

  canonical-tag)
    [[ "$subject" =~ ^Ubuntu-qcom-[0-9]+\.[0-9]+\.[0-9]+-[0-9]+\.[0-9]+$ ]] || { echo "::error::Invalid Canonical tag." >&2; exit 1; }

    # The run is still in progress when it dispatches, so check the build job
    # and not the run conclusion. Either entry point of validate-kernel-tag.yml
    # is acceptable: the sync calls it, a maintainer can dispatch it.
    jq -e \
      --argjson attempt "$kernel_run_attempt" \
      '.event == "workflow_dispatch" and
       (.path == ".github/workflows/fetch-source-pkg.yml" or
        .path == ".github/workflows/validate-kernel-tag.yml") and
       .run_attempt == $attempt' \
      <<< "$kernel_run" >/dev/null || {
        echo "::error::Kernel workflow run does not match the callback context." >&2
        exit 1
      }
    # Match the suffix build-kernel.yml gives its job, so renaming the calling
    # job does not silently drop the check.
    gh api --paginate "repos/qualcomm-linux/ubuntu-qcom-kernel/actions/runs/${kernel_run_id}/jobs" \
      | jq -se '[.[].jobs[] | select(.name | endswith("/ Build"))] | length >= 1 and all(.[]; .conclusion == "success")' >/dev/null || {
        echo "::error::Kernel build job did not succeed for run ${kernel_run_id}." >&2
        exit 1
      }

    # sync-mirror.sh refuses to move a re-pointed upstream tag, so the tag
    # cannot change under us.
    [[ "$(gh api "repos/qualcomm-linux/ubuntu-qcom-kernel/commits/${subject}" --jq '.sha')" == "$head_sha" ]] \
      || { echo "::error::Tag ${subject} does not point at ${head_sha}." >&2; exit 1; }
    ;;

  *)
    echo "::error::Invalid validation mode: ${validation_mode}" >&2; exit 1
    ;;
esac

distro_run="$(gh api "repos/qualcomm-linux/qcom-distro-images/actions/runs/${distro_run_id}")"
jq -e \
  --argjson attempt "$distro_run_attempt" \
  '.event == "repository_dispatch" and
   .path == ".github/workflows/canonical-premerge-validation.yml" and
   .run_attempt == $attempt' \
  <<< "$distro_run" >/dev/null || {
    echo "::error::Distro workflow run does not match the callback context." >&2
    exit 1
  }

distro_build_id="${distro_run_id}-${distro_run_attempt}"
distro_ref="$(jq -r '.head_sha' <<< "$distro_run")"
image_s3_prefix="qualcomm-linux/${kernel_s3_prefix}/${kernel_build_id}"
[[ "$distro_ref" =~ ^[0-9a-f]{40}$ ]] || { echo "::error::Invalid distro source SHA." >&2; exit 1; }

distro_run_url="$(jq -r '.html_url' <<< "$distro_run")"

state=failure
description="Canonical distro image validation failed"
check_conclusion="$distro_result"
if [[ "$distro_result" == "success" ]]; then
  marker="$(mktemp)"
  trap 'rm -f "$marker"' EXIT
  aws s3 cp "s3://${BUCKET}/${image_s3_prefix}/distro-validation.json" "$marker" >/dev/null
  jq -e \
    --arg request_id "$request_id" \
    --arg distro_build_id "$distro_build_id" \
    --arg distro_ref "$distro_ref" \
    --arg kernel_build_id "$kernel_build_id" \
    --arg kernel_s3_prefix "$kernel_s3_prefix" \
    '.status == "success" and
     .request_id == $request_id and
     .distro_build_id == $distro_build_id and
     .distro_ref == $distro_ref and
     .kernel_build_id == $kernel_build_id and
     .kernel_s3_prefix == $kernel_s3_prefix and
     (.images | sort) == ([
       "qcom-ubuntu-iot-resolute-desktop-canonical.images.tar.gz",
       "qcom-ubuntu-iot-resolute-server-canonical.images.tar.gz"
     ] | sort)' "$marker" >/dev/null || {
      echo "::error::Distro completion marker does not match the callback context." >&2
      exit 1
    }
  state=success
  description="Canonical server and desktop distro images passed"
fi

emit_output state "$state"
emit_output description "$description"
emit_output check-conclusion "$check_conclusion"
emit_output validation-mode "$validation_mode"
emit_output subject "$subject"
emit_output head-sha "$head_sha"
emit_output request-id "$request_id"
emit_output kernel-build-id "$kernel_build_id"
emit_output distro-build-id "$distro_build_id"
emit_output image-s3-prefix "$image_s3_prefix"
emit_output distro-run-url "$distro_run_url"
