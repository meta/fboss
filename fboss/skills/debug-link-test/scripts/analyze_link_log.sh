#!/usr/bin/env bash
# analyze_link_log.sh — parse an FBOSS link test (agent ensemble link test) log, or a hw agent
# log, and surface the REAL failure while suppressing benign noise. Pure text parsing (POSIX awk).
#
# Usage:
#   analyze_link_log.sh <logfile>
#   cat log | analyze_link_log.sh -
#
# Reports: the test; the gtest verdict line or why there is none; every gtest Failure block with
# its phase (SetUp / test body / TearDown) and the line just before it; the SetUp gate that fired;
# a per-port table decoded from the IPHY/XPHY/transceiver dump printed for down ports; fatal
# CHECKs, terminate/SAI-init/SDK errors, sanitizer reports; aborts classified as self-abort vs
# external (watchdog) kill with meaningful stack frames and the last SetUp stage reached; agent
# Thrift RPC saturation; and whether the qsfp_service log is needed next (with a ready --tcvr list
# and a time window). Candidate list only: confirm by reading the log around the reported lines.
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
function clip(s, n){ if (n == "") n = 400; return (length(s) > n) ? substr(s,1,n) "  ...[truncated]" : s }
function val(s){ sub(/^.*= /, "", s); sub(/,$/, "", s); gsub(/"/, "", s); return s }
function statename(n){
  return (n=="0")?"NOT_PRESENT":(n=="1")?"PRESENT":(n=="2")?"DISCOVERED":(n=="3")?"IPHY_PORTS_PROGRAMMED": \
         (n=="4")?"XPHY_PORTS_PROGRAMMED":(n=="5")?"TRANSCEIVER_PROGRAMMED":(n=="6")?"ACTIVE": \
         (n=="7")?"INACTIVE":(n=="8")?"UPGRADING":(n=="9")?"TRANSCEIVER_READY":("?" n)
}
function lanes(v,   s, i, b){  # per-lane bitmask -> lane list
  v = v + 0; if (v <= 0) return "-";
  s = ""; for (i = 0; i < 16; i++) { b = int(v / (2 ^ i)) % 2; if (b == 1) s = s (s == "" ? "" : ",") i }
  return s
}
function noiseframe(l){  # runtime / crash-handler / gtest scaffolding frames
  return (l ~ /signalHandler/ || l ~ /__tsan/ || l ~ /__sanitizer/ || l ~ /sighandler/ ||
          l ~ /pthread_kill/ || l ~ /[ :(]raise/ || l ~ /[ :(]abort/ || l ~ /__interceptor/ ||
          l ~ /verbose_terminate/ || l ~ /__cxxabiv/ || l ~ /std::terminate/ || l ~ /__gnu_cxx/ ||
          l ~ /__libc_start/ || l ~ /libc_start_call_main/ || l ~ /_start$/ || l ~ /folly::symbolizer/ ||
          l ~ /HandleExceptionsInMethod/ || l ~ /HandleSehExceptions/ || l ~ / testing::/ ||
          l ~ /\(unknown\)/ || l ~ /execute_native_thread/ || l ~ /start_thread/ || l ~ /__clone/ ||
          l ~ /google::LogMessage/ || l ~ /folly::LogCategory/ || l ~ /folly::LogStream/ ||
          l ~ /folly::detail::/ || l ~ /__GI_/ || l ~ /killpg|__kill|syscall/ || l ~ /__asan|__lsan|__ubsan/ ||
          l ~ /operator new|operator delete|std::__cxx11::basic_string|std::_Rb_tree|std::__detail|std::_Function_handler/)
}
# glog "MMDD HH:MM:SS" -> seconds (month approximated as 31 days; only used for spans)
function gsec(ts,   mo, d, h, m, s){
  mo = substr(ts,1,2) + 0; d = substr(ts,3,2) + 0; h = substr(ts,6,2) + 0; m = substr(ts,9,2) + 0; s = substr(ts,12,2) + 0;
  return (((mo * 31 + d) * 24 + h) * 60 + m) * 60 + s
}
function span(a, b,   x){ x = gsec(b) - gsec(a); return (x >= 0) ? x : -1 }
function isbenign(l){
  return (l ~ /fboss_mka_service_conn|PacketStreamClient.cpp|getConfigAppliedInfo thrift request|RestartTimeTracker|FsdbStreamClient|FsdbPublisher|LINK_SNAPSHOT_EVENT|ThreadHeartbeat|XPHY info missing|runImmediatelyOrRunInFbossEventBaseThreadAndWait/ || \
          l ~ /LldpManager.cpp:[0-9]+\] Port [0-9]+: [0-9]+: Not present in config/)
}
BEGIN{
  test = "(none seen)"; nfail = 0; nchk = 0; nabort = 0; nqerr = 0; nport = 0; ctx = 0; instack = 0;
  gate = ""; gatedetail = ""; gatefail = ""; exitgraceful = 0; exitcode = ""; nbenign = 0; firstts = ""; lastts = "";
  curport = ""; sec = ""; nexc = 0; nvend = 0; nsan = 0; nlldp = 0; badtcvrs = ""; nfabric = 0; nrec = 0;
  prev1 = ""; nrpc = 0; lastgate = ""; lastgatedetail = ""; gatecount = 0; aggpass = ""; ngf = 0;
}
{
  line = $0
  if (line ~ /^[A-Z][a-z][a-z] +[0-9]+ [0-9][0-9]:[0-9][0-9]:[0-9][0-9] [^ ]+ [^ ]+\[[0-9]+\]: /)
    sub(/^[A-Z][a-z][a-z] +[0-9]+ [0-9][0-9]:[0-9][0-9]:[0-9][0-9] [^ ]+ [^ ]+\[[0-9]+\]: /, "", line)
  if (match(line, /^[IVWEF][0-9][0-9][0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]/)) {
    ts = substr(line, 2, 13); if (firstts == "") firstts = ts; lastts = ts; nrec++
  }
  isdump = (curport != "" && line ~ /(AgentEnsembleLinkTest|LinkTest)\.cpp:[0-9]+\]/)
}
# ---- gtest boundaries ---------------------------------------------------------
index(line, "[ RUN      ]") > 0 { test = substr(line, index(line, "]") + 2); nrec++; next }
index(line, "[  FAILED  ]") > 0 {
  n = substr(line, index(line, "]") + 2); nrec++
  if (n !~ /^[0-9]+ test/ && n !~ /listed below/ && n != "") gtestfailed[++ngf] = n
  next
}
index(line, "[       OK ]") > 0 { gtestok = substr(line, index(line, "]") + 2); nrec++; next }
index(line, "[  SKIPPED ]") > 0 { gtestskip = substr(line, index(line, "]") + 2); nrec++; next }
index(line, "[  PASSED  ]") > 0 { aggpass = substr(line, index(line, "]") + 2); nrec++; next }
# ---- crash stack capture ------------------------------------------------------
instack == 1 && line ~ /^\*\*\* Signal [0-9]+ \(SIG[A-Z]+\)/ {
  sigtext[nabort] = clip(line, 260);
  rp = ""; fp = "";
  if (match(line, /received by PID [0-9]+/)) rp = substr(line, RSTART + 16, RLENGTH - 16)
  if (match(line, /maybe from PID [0-9]+/)) fp = substr(line, RSTART + 15, RLENGTH - 15)
  kind = "self-abort (the process raised it: CHECK/terminate/abort; read the stack)"
  if (fp != "" && rp != "" && fp != rp && line ~ /sent by kill/) {
    kind = "EXTERNAL kill from PID " fp ": almost always the runner `timeout -s ABRT <cap>` watchdog, i.e. a HANG/TIMEOUT"; external[nabort] = 1
  }
  abortkind[nabort] = kind; next
}
instack == 1 {
  if (line ~ /@ +(0x)?[0-9a-f][0-9a-f][0-9a-f][0-9a-f]/) {
    if (line ~ / main$| main \(/ || line ~ /\) main/) { instack = 0; next }
    if (!noiseframe(line) && nframe < 14) { frames[nabort] = frames[nabort] "    # " clip(line, 220) "\n"; nframe++ }
    next
  } else if (line ~ /^[[:space:]]+(\/|->)/ || line ~ /^[[:space:]]*$/ || line ~ /^\*\*\*/ || line ~ /^==[0-9]+==/) {
    next
  } else { instack = 0 }
}
line ~ /^\*\*\* Aborted at [0-9]+/ || line ~ /^\*\*\* SIGSEGV|^\*\*\* SIGBUS|^\*\*\* SIGILL|^\*\*\* SIGFPE/ {
  nabort++; abortline[nabort] = NR; abortts[nabort] = lastts; aborttext[nabort] = clip(line, 200);
  instack = 1; nframe = 0; frames[nabort] = ""; nrec++; next
}
# ---- gtest failure blocks -----------------------------------------------------
line ~ /:[0-9]+: Failure$/ || line ~ /unknown file: Failure$/ {
  nfail++; failline[nfail] = NR; failhead[nfail] = line; failtest[nfail] = test; failbody[nfail] = "";
  failprev[nfail] = prev1; failts[nfail] = lastts; ctx = 7; nrec++; next
}
ctx > 0 {
  if (line ~ /^[[:space:]]*$/ || line ~ /^[IVWEF][0-9][0-9][0-9][0-9] /) { ctx = 0 }   # context ends; fall through
  else {
    failbody[nfail] = failbody[nfail] "    | " clip(line) "\n"; ctx--
    if (line ~ /thrown in SetUp\(\)/) failphase[nfail] = "SetUp"
    else if (line ~ /thrown in the test body/) failphase[nfail] = "test body"
    else if (line ~ /thrown in TearDown\(\)/) failphase[nfail] = "TearDown"
    if (gate == "") {
      g = ""; d = line
      if (line ~ /Unexpected Link status 0 for /) g = (failphase[nfail] == "SetUp") ? "G1 SetUp link-up gate (cabled ports never came UP; the named test body never ran)" : "link-up wait inside the test body (ports did not come back UP)"
      else if (line ~ /Unexpected Link status 1 for /) g = "link-DOWN wait (ports stayed UP when expected down)"
      else if (line ~ /don.t have expected TransceiverStateMachineState/) {
        g = "G2 transceiver-state gate (qsfp_service side)"
        if (match(line, /Transceivers:\[[0-9,]*\]/)) badtcvrs = substr(line, RSTART + 14, RLENGTH - 15)
      }
      else if (line ~ /don.t have expected PortStateMachineState/) g = "G3 port-state gate (qsfp_service port manager)"
      else if (line ~ /TransceiverInfo was never populated/) g = "qsfp_service returned no present transceiver"
      else if (line ~ /QSFP Service no longer alive|QSFP Service run state no longer active/) g = "TearDown: qsfp_service died / not ACTIVE during the test"
      else if (line ~ /FSDB no longer alive/) g = "TearDown: FSDB died during the test"
      else if (line ~ /SW Agent RSS memory/) g = "agent memory above threshold"
      if (g != "") { gate = g; gatedetail = d; gatefail = nfail }
    }
    next
  }
}
# ---- fatal CHECK / glog F / terminate / SAI / sanitizer ------------------------
line ~ /Check failed:/ || line ~ /^F[0-9][0-9][0-9][0-9] / { nchk++; chk[nchk] = NR ": " clip(line); nrec++; prev1 = line; next }
line ~ /terminate called/ || line ~ /what\(\): / || line ~ /^Terminated due to: / || line ~ /Failed to create sai entity/ || line ~ /switch initialization failed/ || line ~ /encountered a fatal error/ || line ~ /Failed to set attribute .*(NOT SUPPORTED|NOT_SUPPORTED)/ || line ~ /Unsupported field\/action for aclEntry/ {
  nexc++; exc[nexc] = NR ": " clip(line); prev1 = line; next
}
line ~ /bcm_init\(\) Failed|initialization command "[A-Z_]+" failed|Error: Invalid parameter ; |SAI_STATUS_FAILURE|sai_api_initialize failed/ {
  nvend++; if (nvend <= 3) vend[nvend] = NR ": " clip(line, 250); nrec++; next
}
line ~ /ERROR: (AddressSanitizer|LeakSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer)|WARNING: ThreadSanitizer|SUMMARY: (Address|Leak|Thread|UndefinedBehavior)Sanitizer/ {
  nsan++; if (nsan <= 4) san[nsan] = NR ": " clip(line); nrec++; next
}
# ---- per-port debug dump printed by the link-up gate (logLinkDbgMessage) --------
line ~ /Debug information for / {
  p = line; sub(/^.*Debug information for /, "", p); curport = p; sec = "";
  if (!(p in pseen)) { pseen[p] = 1; plist[++nport] = p }
  next
}
isdump && line ~ /IPHY info missing for / { piphy[curport] = "missing"; sec = ""; next }
isdump && line ~ /XPHY info missing for / { pxphy[curport] = "missing"; sec = ""; next }
isdump && line ~ /Transceiver info missing for / { ptcvr[curport] = "MISSING"; sec = ""; next }
isdump && line ~ /IPHY INFO: / { sec = "iphy"; next }
isdump && line ~ /XPHY INFO: / { sec = "xphy"; pxphy[curport] = "present"; next }
isdump && line ~ /Transceiver INFO: / { sec = "tcvr"; ptcvr[curport] = "present"; next }
isdump && sec == "iphy" {
  if (line ~ /signalDetectLive \(bool\) = /) { if (line ~ /= true/) psdT[curport]++; else psdF[curport]++ }
  else if (line ~ /cdrLockLive \(bool\) = /) { if (line ~ /= true/) pcdrT[curport]++; else pcdrF[curport]++ }
  next
}
isdump && sec == "xphy" {
  if (line ~ /signalDetectLive \(bool\) = / && line ~ /= false/) pxsdF[curport]++
  next
}
isdump && sec == "tcvr" {
  if (line ~ / present \(bool\) = / && !(curport in ppresent)) ppresent[curport] = val(line)
  else if (line ~ /^.*\]     3: port \(i32\) = / && !(curport in pid)) pid[curport] = val(line)
  else if (line ~ /stateMachineState \(i32\) = / && !(curport in pstate)) pstate[curport] = statename(val(line))
  else if (line ~ /partNumber \(string\) = / && !(curport in ppn)) ppn[curport] = val(line)
  else if (line ~ / tcvrName \(string\) = / && !(curport in ptn)) ptn[curport] = val(line)
  else if (line ~ / rxLos \(i32\) = / && !(curport in prxlos)) prxlos[curport] = val(line)
  else if (line ~ / rxLol \(i32\) = / && !(curport in prxlol)) prxlol[curport] = val(line)
  else if (line ~ /communicationError \(bool\) = true/) pcomm[curport] = 1
  next
}
curport != "" && !isdump { curport = ""; sec = "" }   # dump ended; process this line normally
# ---- last SetUp stage entered (explains a watchdog kill) ------------------------
line ~ /Checking link status on / {
  lastgate = "G1 link-up wait (waitForLinkStatus) entered at " lastts; lastgatedetail = clip(line, 200)
  c = line; sub(/^.*Checking link status on /, "", c); gatecount = split(c, tmp, ",")
}
line ~ /Checking qsfp TransceiverStateMachineState on / { lastgate = "G2 transceiver-state wait entered at " lastts; lastgatedetail = clip(line, 200); gatecount = 0 }
line ~ /Checking qsfp PortStateMachineState on / { lastgate = "G3 port-state wait entered at " lastts; lastgatedetail = clip(line, 200); gatecount = 0 }
line ~ /Multi Switch Link Test setup ready/ { lastgate = "SetUp finished at " lastts " (the test body was running)"; lastgatedetail = ""; gatecount = 0 }
# ---- agent Thrift RPC saturation (e.g. qsfp_service -> agent programInternalPhyPorts) --------
line ~ /ThriftHandler.cpp:[0-9]+\] \[[^]]*\] [A-Za-z]+ thrift request received/ {
  m = line; sub(/^.*ThriftHandler.cpp:[0-9]+\] \[[^]]*\] /, "", m); sub(/ .*$/, "", m); rpcin[m]++; if (!(m in rpcseen)) { rpcseen[m] = 1; rpcl[++nrpc] = m }
}
line ~ /ThriftHandler.cpp:[0-9]+\] \[[^]]*\] [A-Za-z]+ thrift request succeeded in [0-9]+ms/ {
  m = line; sub(/^.*ThriftHandler.cpp:[0-9]+\] \[[^]]*\] /, "", m); sub(/ .*$/, "", m);
  l = line; sub(/^.*succeeded in /, "", l); sub(/ms.*$/, "", l); rpcok[m]++; if (l + 0 > rpcmax[m] + 0) rpcmax[m] = l + 0
}
# ---- qsfp_service / fabric / lldp context -------------------------------------
line ~ /Failed to call qsfp_service/ { nqerr++; if (nqerr == 1) qfirst = NR ": " clip(line, 250); qlast = lastts }
line ~ /Wrong lldp neighbor size|No lldp neighbors on/ { nlldp++; if (nlldp == 1) lldpfirst = NR ": " clip(line, 250) }
line ~ /No fabric end points on/ { nfabric++ }
# ---- process exit markers -----------------------------------------------------
line ~ /tearDownAgentEnsemble\(\) for warmboot|\[Exit\] Total graceful Exit time/ { exitgraceful = 1 }
line ~ /Service result=exit-code exit_status=[0-9]+/ { v = line; sub(/^.*exit_status=/, "", v); exitcode = v }
line ~ /Return Code: -?[0-9]+/ { v = line; sub(/^.*Return Code: /, "", v); exitcode = v }
# ---- benign accounting / previous-line memory ---------------------------------
{ if (isbenign(line)) nbenign++; else if (line !~ /^[[:space:]]*$/) prev1 = line }
END{
  if (NR == 0 || nrec == 0) {
    print "error: no link test / glog / gtest records found (empty or not a link test log)" > "/dev/stderr"; exit 1
  }
  print "==================== LINK TEST LOG ANALYSIS ====================";
  printf "Lines scanned:            %d\n", NR;
  printf "Test (from [ RUN ]):      %s\n", test;
  if (firstts != "") { s = span(firstts, lastts); printf "Log time span (glog):     %s -> %s%s\n", firstts, lastts, (s >= 0) ? "  (~" s " s)" : "" }
  if (ngf > 0) { printf "gtest [  FAILED  ]:       "; for (i = 1; i <= ngf; i++) printf "%s%s", (i > 1 ? "; " : ""), gtestfailed[i]; print "" }
  else if (gtestok != "" || aggpass != "") {
    printf "gtest passed:             %s%s\n", (gtestok != "" ? "[       OK ] " gtestok : ""), (aggpass != "" ? "  [  PASSED  ] " aggpass : "");
    print  "    NOTE: the binary PASSED. If the result is still FAILED, the verdict came from OUTSIDE this log";
    print  "    (multi-switch \"Hw Agent exited with non-zero status code.\" + hw agent log link, a postflight";
    print  "    health check, or a runner rule): read the result record message section.";
  }
  else if (gtestskip != "") printf "gtest [  SKIPPED ]:       %s\n", gtestskip;
  else if (exitgraceful) print "gtest verdict line:       none — the process exited through the warmboot graceful-exit path (cold_boot runs with --setup_for_warmboot exit(1) on failure before gtest prints [  FAILED  ]). The Failure block below IS the verdict.";
  else print "gtest verdict line:       none (killed/aborted before gtest summary, or truncated log)";
  if (exitcode != "") printf "Exit status:              %s\n", exitcode;
  print "";
  if (nfail > 0) {
    printf "gtest Failure blocks:     %d  (chronological; the first is usually the trigger — later ones are cascades only if they depend on it)\n", nfail;
    for (i = 1; i <= nfail && i <= 6; i++) {
      printf "\n[gtest-failure #%d] line %d  ts %s  phase: %s  (test: %s)\n", i, failline[i], failts[i], (failphase[i] == "" ? "test body (EXPECT/ASSERT)" : failphase[i]), failtest[i];
      if (failprev[i] != "" && !isbenign(failprev[i])) printf "    (line before) %s\n", clip(failprev[i], 260);
      printf "    %s\n%s", clip(failhead[i]), failbody[i];
    }
    if (nfail > 6) printf "    ... %d more Failure blocks omitted\n", nfail - 6;
  } else print "gtest Failure blocks:     0";
  if (gate != "") { print ""; printf "GATE / CLASS:             %s  (from gtest-failure #%s)\n    %s\n", gate, gatefail, clip(gatedetail, 600) }
  if (nchk > 0) { print ""; printf "Fatal CHECK / glog F:     %d\n", nchk; for (i = 1; i <= nchk && i <= 5; i++) printf "    %s\n", chk[i] }
  if (nexc > 0) { print ""; printf "terminate / fatal / SAI:  %d\n", nexc; for (i = 1; i <= nexc && i <= 5; i++) printf "    %s\n", exc[i] }
  if (nvend > 0) { print ""; printf "Vendor SDK init errors:   %d (context for a SAI init abort)\n", nvend; for (i = 1; i <= nvend && i <= 3; i++) printf "    %s\n", vend[i] }
  if (nsan > 0) { print ""; printf "Sanitizer reports:        %d\n", nsan; for (i = 1; i <= nsan && i <= 4; i++) printf "    %s\n", san[i] }
  selfabort = 0;
  if (nabort > 0) {
    print "";
    for (i = 1; i <= nabort && i <= 3; i++) {
      s = span(firstts, abortts[i]);
      printf "[abort #%d] line %d  last glog ts before abort: %s%s\n    %s\n    %s\n    => %s\n", i, abortline[i], abortts[i], (s >= 0) ? "  (~" s " s after first log line)" : "", aborttext[i], sigtext[i], abortkind[i];
      if (!(i in external)) selfabort = 1;
      if (frames[i] != "") printf "  meaningful frames:\n%s", frames[i];
      if ((i in external) && lastgate != "") {
        printf "  last SetUp stage reached: %s\n    %s\n", lastgate, lastgatedetail;
        if (gatecount > 0) printf "  ports checked by that wait: %d (candidate set; which ones were down is NOT known — the process was killed before the down-port dump)\n", gatecount;
        print  "  => a gate entered but never finished = SetUp was still polling at the cap. Root cause = why links/transceivers";
        print  "     never converged; mechanism = timeout (waitForLinkStatus refetches the full port status for every bad port";
        print  "     each retry, so many down ports on a slow sanitizer build stretch the ~5 min gate past the cap).";
      }
      if ((i in external) && frames[i] !~ /fboss::|SetUp|TestBody/) print "  NOTE: these frames are from the thread that RECEIVED the signal (e.g. a log writer), not necessarily the stuck one; use the last test-thread log lines before the abort.";
    }
  }
  if (nport > 0) {
    print "";
    printf "Per-port debug dump (%d down port(s), printed by the link-up wait):\n", nport;
    print  "  port            tcvr id / name     present  tcvrState               partNumber            media rxLos lanes / rxLol lanes   IPHY sigDet T/F  cdrLock T/F  XPHY";
    for (i = 1; i <= nport && i <= 40; i++) {
      p = plist[i];
      tn = (p in pid ? pid[p] : "-") " / " (p in ptn ? ptn[p] : "-");
      pr = (p in ppresent ? ppresent[p] : (p in ptcvr ? ptcvr[p] : "-")) ((p in pcomm) ? "+commErr" : "");
      xp = (p in pxphy ? pxphy[p] : "-"); if (pxsdF[p] + 0 > 0) xp = xp " (sigDet false x" pxsdF[p] ")";
      printf "  %-15s %-18s %-8s %-23s %-21s %-15s / %-15s %d/%d            %d/%d          %s\n", p, tn, pr, (p in pstate ? pstate[p] : "-"), (p in ppn ? ppn[p] : "-"), (p in prxlos ? lanes(prxlos[p]) : "-"), (p in prxlol ? lanes(prxlol[p]) : "-"), psdT[p] + 0, psdF[p] + 0, pcdrT[p] + 0, pcdrF[p] + 0, xp;
    }
    if (nport > 40) printf "  ... %d more ports\n", nport - 40;
    print "  (tcvr id is 0-based. media rxLos/rxLol are per-lane bitmasks from the module: lanes listed = no light / no lock";
    print "   on the LINE side. Clean media lanes + IPHY sigDet/cdrLock false = host side; tcvrState != ACTIVE = qsfp_service.)";
  }
  rpchdr = 0;
  for (i = 1; i <= nrpc; i++) { m = rpcl[i]; if (rpcmax[m] + 0 >= 30000) {
    if (!rpchdr) { print ""; print "Agent Thrift RPC saturation (calls into the agent that took >= 30 s):"; rpchdr = 1 }
    printf "  %-32s received %d, completed %d, slowest %d ms\n", m, rpcin[m], rpcok[m], rpcmax[m] } }
  if (rpchdr) print "  (qsfp_service drives programInternalPhyPorts; slow/unanswered calls here explain qsfp \"Load Shedding\" / timeouts.)";
  if (nqerr > 0) { print ""; printf "qsfp_service RPC failures: %d (last at %s)\n    first: %s\n", nqerr, qlast, qfirst }
  if (nlldp > 0) { printf "LLDP neighbor problems:   %d\n    first: %s\n", nlldp, lldpfirst }
  if (nfabric > 0) printf "Fabric endpoint missing:  %d line(s)\n", nfabric;
  printf "Benign lines counted:     %d (mka reconnect, config-applied polls, fsdb, restart tracker, XPHY-missing, LLDP not-in-config)\n", nbenign;

  # ---- qsfp_service hand-off decision ------------------------------------------
  need = "no"; why = "no qsfp_service-owned failure evidence in this log";
  if (gate ~ /^G2|^G3|never populated|qsfp_service died/) { need = "YES"; why = "the failing gate is evaluated by qsfp_service" }
  else if (nqerr >= 3) { need = "YES"; why = "repeated qsfp_service RPC failures (service down/restarting?)" }
  else if (nport > 0) {
    bad = 0; for (i = 1; i <= nport; i++) { p = plist[i]; if ((p in pstate && pstate[p] != "ACTIVE") || (p in prxlos && prxlos[p] + 0 > 0) || (p in ppresent && ppresent[p] != "true") || (p in ptcvr && ptcvr[p] == "MISSING") || (p in pcomm)) bad++ }
    if (bad > 0) { need = "YES"; why = bad " down port(s) show a transceiver not ACTIVE / dark media lanes / not present / comm error" }
    else { need = "maybe"; why = "transceivers look ACTIVE with clean media lanes; suspect host-side SerDes/IPHY, peer, or cabling first" }
  } else if (rpchdr) { need = "YES"; why = "agent RPCs from qsfp_service were slow/unanswered; confirm on the qsfp side" }
  else if (nchk == 0 && nexc == 0 && !selfabort && nsan == 0 && test ~ /Optics|Prbs|QsfpFsdb|Transceiver|Remediation|TxDisable|xPhy|qsfp/) { need = "maybe"; why = "no agent-side killer found and the test exercises qsfp_service-owned state" }
  ids = ""; for (i = 1; i <= nport; i++) { p = plist[i]; if (p in pid) { t = pid[p]; if (!(t in tseen)) { tseen[t] = 1; ids = ids (ids == "" ? "" : ",") t } } }
  if (badtcvrs != "") { n = split(badtcvrs, bb, ","); for (i = 1; i <= n; i++) if (!(bb[i] in tseen)) { tseen[bb[i]] = 1; ids = ids (ids == "" ? "" : ",") bb[i] } }
  print "";
  printf "QSFP_SERVICE_LOG_NEEDED:  %s  (%s)\n", need, why;
  if (need != "no") {
    if (ids != "") printf "  focus transceivers:     --tcvr %s\n", ids;
    if (firstts != "") printf "  time window (glog):     --from \"%s\" --to \"%s\"\n", firstts, lastts;
    print "  next: get the qsfp_service log covering this window and run analyze_qsfp_service_log.sh with the flags above.";
  }
  print "===============================================================";
}
' "$SRC"
