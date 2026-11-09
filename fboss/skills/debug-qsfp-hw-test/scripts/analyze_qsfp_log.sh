#!/usr/bin/env bash
# analyze_qsfp_log.sh — parse a qsfp_hw_test log and surface the REAL failure(s),
# suppressing the benign noise. Open source: pure text parsing, no infra deps.
#
# Usage:
#   analyze_qsfp_log.sh <logfile>
#   cat log | analyze_qsfp_log.sh -        # read from stdin
#
# Output: failing gtest(s); terminal killer signatures (gtest assertion, CHECK/FATAL,
# exception, signal); the crash stack (meaningful frames only) when the process aborts;
# retry/context lines (state-machine WARN wrappers that name the underlying reason but
# are NOT the terminal verdict); an I2C read-failure aggregate; and a benign-line count.
# Treat as a candidate list — confirm by reading the log around the reported lines.
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <logfile|->" >&2
  exit 2
fi
SRC="$1"
if [ "$SRC" = "-" ]; then SRC=/dev/stdin; elif [ ! -r "$SRC" ]; then
  echo "error: cannot read '$SRC'" >&2; exit 2
fi

awk '
function clip(s){ return (length(s) > 500) ? substr(s,1,500) "  …[truncated]" : s }
# Frames that are pure runtime/crash-handler/gtest scaffolding — never the cause.
function noiseframe(l){
  return (l ~ /signalHandler/ || l ~ /__tsan/ || l ~ /__sanitizer/ || l ~ /sighandler/ ||
          l ~ /pthread_kill/ || l ~ /[ :]raise/ || l ~ /[ :]abort/ || l ~ /__interceptor_abort/ ||
          l ~ /verbose_terminate/ || l ~ /__cxxabiv/ || l ~ /std::terminate/ ||
          l ~ /__libc_start/ || l ~ /libc_start_call_main/ || l ~ /_start$/ ||
          l ~ /HandleExceptionsInMethod/ || l ~ / testing::/ || l ~ /\(unknown\)/ ||
          l ~ /execute_native_thread/ || l ~ /start_thread/ || l ~ /__clone/)
}
BEGIN{
  cur="(before first test / setup)";
  nfail=0; nsig=0; nbenign=0; nhint=0; ctx=0; instack=0; nframe=0;
}
# ---- gtest boundaries -------------------------------------------------------
index($0,"[ RUN      ]")>0 { cur=substr($0, index($0,"]")+2); instack=0; ctx=0; next }
index($0,"[       OK ]")>0 { cur="(between tests)"; instack=0; ctx=0; next }
index($0,"[  FAILED  ]")>0 {
  name=substr($0, index($0,"]")+2);
  kbtag="";
  if (name ~ /\[KNOWN BAD/) kbtag="   <-- KNOWN BAD (suppressed, not a regression)";
  # inline per-test FAILED carries an elapsed "(NNNNNN ms)"; the summary block does not
  ms=-1;
  if (match(name, /\([0-9]+ ms\)/)) {
    t=substr(name, RSTART+1, RLENGTH-4); ms=t+0;
    gsub(/ *\([0-9]+ ms\)/, "", name);
  }
  gsub(/^ +| +$/, "", name);
  if (name != "" && name !~ /^[0-9]+ FAILED TEST/ && name !~ /listed below:?$/ && name !~ /^[0-9]+ tests?,/) {
    dup=0;
    for (i=1;i<=nfail;i++) if (failed[i]==name) { dup=1; if (ms!=-1) { failms[i]=ms; } if (kbtag!="") { failkb[i]=kbtag; } break; }
    if (!dup) { failed[++nfail]=name; failms[nfail]=ms; failkb[nfail]=kbtag; }
  }
  cur="(between tests)"; instack=0; ctx=0;
  next
}
# ---- crash stack capture (runs right after a crash/signal line) -------------
# The stack IS the diagnosis for an abort. Print the meaningful frames only.
instack==1 {
  if ($0 ~ /@ +(0x)?[0-9a-f]{4,}/) {
    if (index($0," main")>0) { instack=0; next }        # stop at main()
    if (!noiseframe($0) && nframe<15) {
      # strip the journal "host qsfp_hw_test[pid]:" prefix if present
      fr=$0; sub(/^.*qsfp_hw_test\[[0-9]+\]: */, "", fr);
      printf "    # %s\n", clip(fr); nframe++;
    }
    next
  } else if ($0 ~ /-> / || $0 ~ /qsfp_hw_test\[[0-9]+\]:[[:space:]]+\// || $0 ~ /^[[:space:]]+(\/|->)/) {
    next   # indented source-path continuation of a frame; skip, stay in stack
  } else { instack=0 }   # a real log line ends the stack; fall through
}
# ---- state-machine WARN wrappers: retry/context, NOT terminal ---------------
# These name the underlying reason (vendor error, tunable-optics gap, bad EEPROM)
# but are WARN + retry — the terminal verdict is the SetUp gate / gtest assertion.
$0 ~ /programExternalPhyPorts failed|readyTransceiver failed|programTransceiver failed|discover transceiver failed|programInternalPhyPorts failed|tryRemediateTransceiver failed/ {
  nhint++;
  printf "\n[retry/context — WARN, not the verdict] line %d  (in: %s)\n    %s\n", NR, cur, clip($0);
  next
}
# ---- terminal failure signatures -------------------------------------------
{
  sig="";
  if ($0 ~ /:[0-9]+: Failure/ || $0 ~ /unknown file: Failure/) sig="gtest-assert";
  else if ($0 ~ /Check failed:/ || $0 ~ /Check failure stack trace/ || $0 ~ /^F[0-9][0-9][0-9][0-9] / || $0 ~ /LogMessageFatal/) sig="CHECK/FATAL";
  else if ($0 ~ /terminate called/ || $0 ~ /FbossError/ || $0 ~ /what\(\):/ || \
           $0 ~ /Never got / || $0 ~ /Never refreshed / || $0 ~ /Timed out waiting/ || \
           $0 ~ /C\+\+ exception with description/ || $0 ~ /thrown in the test body/ || \
           $0 ~ /accessing unset optional/ || $0 ~ /Verify with retry failed/ || \
           $0 ~ /Failed to create sai entity/ || $0 ~ /is missing transceiverManagementInterface/ || \
           $0 ~ /is missing moduleMediaInterface/ || $0 ~ /Unknown media lane code byte/ || \
           $0 ~ /BaldEagleError|BcmPhyError|SaiApiError|BcmError|MdioError/) sig="exception";
  else if ($0 ~ /SIGSEGV/ || $0 ~ /SIGABRT/ || $0 ~ /signal killed/ || $0 ~ /\*\*\* Aborted at/ || $0 ~ /\*\*\* SIGSEGV/ || $0 ~ /Service exit status:/) sig="crash/signal";
  if (sig != "") {
    nsig++;
    printf "\n[%s] line %d  (in: %s)\n    %s\n", sig, NR, cur, clip($0);
    if (sig=="crash/signal") { instack=1; nframe=0; ctx=0 }
    else { ctx = (sig=="gtest-assert") ? 4 : 2 }
    next;
  }
}
# ---- I2C/hardware error aggregate (context, not verdict) --------------------
# Hundreds across modules = bus/FPGA/systemic; a handful on one module = module/cable.
/BspTransceiverIO::read\(\) failed/ || /failed to read management interface/ {
  ni2c++;
  if (match($0, /tcvr [0-9]+/)) {
    id=substr($0, RSTART+5, RLENGTH-5);
    if (!(id in i2cseen)) { i2cseen[id]=1; i2cids[++ni2cu]=id; }
  }
  next
}
# ---- context lines right after a (non-crash) signature ---------------------
ctx>0 {
  if ($0 ~ /^[[:space:]]*$/) { ctx=0 }         # stop at blank line
  else { printf "    | %s\n", clip($0); ctx-- }
  next
}
# ---- benign noise accounting ------------------------------------------------
{
  if ($0 ~ /BspTransceiverIOTrace/ || $0 ~ /LINK_SNAPSHOT_EVENT/ || $0 ~ /FsdbPublisher/ || \
      $0 ~ /FsdbStreamClient/ || $0 ~ /CommonThriftUtils/ || $0 ~ /QsfpFsdbSubscriber/ || \
      $0 ~ /ThreadHeartbeat/ || $0 ~ /not returned after refresh/ || $0 ~ /not managed by qsfp_service/ || \
      $0 ~ /doesn.t have expected state=/ || $0 ~ /don.t meet the expected state/ || \
      $0 ~ /^V[0-9]/ || $0 ~ /^I[0-9]/) nbenign++;
}
END{
  print  "==================== QSFP HW TEST LOG ANALYSIS ====================";
  printf "Lines scanned:            %d\n", NR;
  printf "Terminal signatures:      %d\n", nsig;
  printf "Retry/context hints:      %d (named reasons; not the verdict)\n", nhint;
  printf "Benign/info lines seen:   %d (suppressed above)\n", nbenign;
  if (ni2c>0) {
    printf "I2C read failures:        %d lines across %d module(s):", ni2c, ni2cu;
    for (i=1;i<=ni2cu && i<=10;i++) printf " %s", i2cids[i];
    if (ni2cu>10) printf " ...";
    print "";
  }
  print  "";
  if (nfail>0) {
    print "Failing gtest(s):";
    for (i=1;i<=nfail;i++) {
      to = (failms[i]>=600000) ? "   <-- near timeout cap: suspect HANG/TIMEOUT" : "";
      if (failms[i]==-1) printf "  - %s  (summary only, no elapsed)%s%s\n", failed[i], failkb[i], to;
      else printf "  - %s  (%d ms)%s%s\n", failed[i], failms[i], failkb[i], to;
    }
  } else {
    print "No [  FAILED  ] marker found.";
    print "  (log may be truncated/tail-only, or the run aborted in SetUp before a test body.)";
  }
  if (nsig==0 && nhint>0) {
    print "";
    print "No terminal killer line, but retry/context hints above name the underlying reason";
    print "(programming never completed). The terminal failure is the SetUp gate giving up.";
  } else if (nsig==0) {
    print "";
    print "No real-failure signature matched. Likely a TIMEOUT/HANG or a SETUP abort —";
    print "check the run return code / elapsed time and the SetUp()/ensemble init region.";
  }
  print  "";
  print  "Reading tips:";
  print  "  - When several [gtest-assert] lines share one test window, the FIRST is usually the";
  print  "    trigger; later ones are cascades.";
  print  "  - For a crash, the [crash/signal] stack frames above localize it; a `terminate` can";
  print  "    MASK the original error (nothing logged before it) — trust the stack.";
  print  "  - [retry/context] lines are the WHY; the terminal line is the verdict.";
  print  "  Then use references/failure-signatures.md on the lines BEFORE the killer.";
  print  "===================================================================";
}
' "$SRC"
