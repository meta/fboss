#!/bin/bash
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#
# Run a mono (single-process) AgentHwTest: cold boot then conditional warm boot.
#
# Usage: bash run_mono_test.sh <binary_path> <config_path> <test_filter> <user> [ld_library_path]
#
# Example (Broadcom):
#   bash run_mono_test.sh \
#     /root/skhare/sai_agent_hw_test-brcm-12.2.0.0_dnx_odp \
#     /root/skhare/meru800bia.agent.materialized_JSON \
#     AgentVoqSwitchTest.addRemoveNeighbor \
#     skhare
#
# Example (Leaba — with LD_LIBRARY_PATH):
#   bash run_mono_test.sh \
#     /root/skhare/sai_agent_hw_test-leaba-24.8.3001 \
#     /root/skhare/wedge400c.agent.materialized_JSON \
#     AgentEmptyTest.CheckInit \
#     skhare \
#     /root/skhare/lib/dyn/
#
# Leaba: BASE_OUTPUT_DIR (the directory holding the SDK's res/) defaults to
# /root/<user>; set it in the environment to use another runtime directory.
# Set ASIC in the environment if the platform needs it (e.g. ASIC=GR2_A0 on G200).

set -u

BINARY="$1"
CONFIG="$2"
TEST_FILTER="$3"
TARGET_USER="$4"
LD_LIB_PATH="${5:-}"

# Set Leaba environment variables if LD_LIBRARY_PATH provided
if [ -n "${LD_LIB_PATH}" ]; then
  export LD_LIBRARY_PATH="${LD_LIB_PATH}"
  export BASE_OUTPUT_DIR="${BASE_OUTPUT_DIR:-/root/${TARGET_USER}}"
  echo "=== Leaba SDK env: LD_LIBRARY_PATH=${LD_LIBRARY_PATH}, BASE_OUTPUT_DIR=${BASE_OUTPUT_DIR}, ASIC=${ASIC:-} ==="
fi

CB_LOG="/root/${TARGET_USER}/sai_replayer-cb.log"
WB_LOG="/root/${TARGET_USER}/sai_replayer-wb.log"
CB_OUT="/root/${TARGET_USER}/mono_test-cb.out"

# Match argv[0] only: a bare `pgrep -f` also matches the shell that launched this
# script with the binary path as an argument, and killing it ends the session.
kill_procs() {
  local sig="${1:--TERM}"
  for pat in sai_agent_hw_test wedge_agent fboss_sw_agent; do
    pgrep -f "^[^ ]*${pat}" 2>/dev/null | xargs -r kill "$sig" 2>/dev/null
  done
}

# --- Clean state ---
kill_procs
sleep 1
kill_procs -9
rm -rf /dev/shm/fboss

# --- Cold boot ---
echo "=== Cold boot: ${TEST_FILTER} ==="
"${BINARY}" \
  --config "${CONFIG}" \
  --gtest_filter="${TEST_FILTER}" \
  --enable-replayer --sai_log "${CB_LOG}" \
  --setup-for-warmboot 2>&1 | tee "${CB_OUT}"
cb_exit=${PIPESTATUS[0]}

# --setup-for-warmboot exits 0 even when an EXPECT failed during cold boot, so
# the captured output decides; without it, fail closed rather than pass.
if [ "$cb_exit" -eq 0 ] && [ ! -s "${CB_OUT}" ]; then
  echo "=== Cold boot exited 0 but its output was not captured in ${CB_OUT} ==="
  cb_exit=1
fi
if [ "$cb_exit" -eq 0 ]; then
  cb_failures=$(grep -acE ':[0-9]+: Failure$' "${CB_OUT}")
  if [ "$cb_failures" -gt 0 ]; then
    echo "=== Cold boot exited 0 but logged ${cb_failures} gtest failure(s) ==="
    cb_exit=1
  fi
fi

# --- Warm boot (only if cold boot passed) ---
wb_exit=-1
if [ "$cb_exit" -eq 0 ]; then
  echo ""
  echo "=== Warm boot: ${TEST_FILTER} ==="
  "${BINARY}" \
    --config "${CONFIG}" \
    --gtest_filter="${TEST_FILTER}" \
    --enable-replayer --sai_log "${WB_LOG}"
  wb_exit=$?
fi

# --- Summary ---
echo ""
echo "=== Test Results ==="
if [ "$cb_exit" -eq 0 ]; then
  echo "Cold boot: PASSED (exit code 0)"
else
  echo "Cold boot: FAILED (exit code $cb_exit)"
fi
if [ "$wb_exit" -eq -1 ]; then
  echo "Warm boot: SKIPPED (cold boot failed)"
elif [ "$wb_exit" -eq 0 ]; then
  echo "Warm boot: PASSED (exit code 0)"
else
  echo "Warm boot: FAILED (exit code $wb_exit)"
fi
echo "=== Log Files ==="
echo "Test output (cold boot): ${CB_OUT}"
echo "SAI replayer (cold boot): ${CB_LOG}"
if [ "$wb_exit" -ne -1 ]; then
  echo "SAI replayer (warm boot): ${WB_LOG}"
fi

# Exit with failure if any test failed
if [ "$cb_exit" -ne 0 ] || [ "$wb_exit" -gt 0 ]; then
  exit 1
fi
exit 0
