#!/usr/bin/env bash
# analyze_qsfp_service_log.sh — summarize a qsfp_service DAEMON log (not a gtest log) for a
# link test failure window: what the transceiver / port state machines were doing and WHY they
# did not reach the state the link test waited for. Pure text parsing, no infra deps.
#
# Usage:
#   analyze_qsfp_service_log.sh <logfile|-> [--tcvr 6,10,14] [--from "MMDD HH:MM:SS"] [--to "MMDD HH:MM:SS"]
#                               [--at "MMDD HH:MM:SS"]
#
#   --tcvr   0-based transceiver IDs to focus on (as printed in "[SM]Transceiver:N"). Default:
#            the transceivers with the most non-benign failures in the window.
#   --from/--to  glog time bounds ("0921 18:40:00"); lines without a glog timestamp are kept
#            only if they fall between in-window lines.
#   --at     the failure time (glog time of the link test failure line): checks the log covers it
#            and splits signature counts into before/after the failure (after = not causal).
#
# Output: qsfp_service (re)starts; fatal/crash lines; state-machine failure REASONS aggregated
# per transceiver (the text after "... failed:"), with benign agent-restart reasons separated;
# known root-cause signatures; the final state and last transitions of each focus transceiver;
# port state machine transitions; and a normalized WARN/ERR histogram.
#
# The root-cause detector list below mirrors the qsfp signatures owned by the debug-qsfp-hw-test
# skill (references/failure-signatures.md). Keep them in sync when either changes.
set -euo pipefail

SRC=""; TCVR=""; FROM=""; TO=""; AT=""
need() { if [ "$#" -lt 2 ] || [ -z "$2" ]; then echo "error: $1 needs a value" >&2; exit 2; fi; }
while [ "$#" -gt 0 ]; do
  case "$1" in
    --tcvr) need "$@"; TCVR="$2"; shift 2;;
    --from) need "$@"; FROM="$2"; shift 2;;
    --to)   need "$@"; TO="$2"; shift 2;;
    --at)   need "$@"; AT="$2"; shift 2;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) if [ -z "$SRC" ]; then SRC="$1"; shift; else echo "unknown arg: $1" >&2; exit 2; fi;;
  esac
done
if [ -z "$SRC" ]; then echo "usage: $0 <logfile|-> [--tcvr ids] [--from 'MMDD HH:MM:SS'] [--to 'MMDD HH:MM:SS']" >&2; exit 2; fi
if [ "$SRC" = "-" ]; then SRC=/dev/stdin; elif [ ! -r "$SRC" ]; then echo "error: cannot read '$SRC'" >&2; exit 2; fi

awk -v TCVR="$TCVR" -v FROM="$FROM" -v TO="$TO" -v AT="$AT" '
function clip(s, n){ if (n == "") n = 300; return (length(s) > n) ? substr(s,1,n) "  ...[truncated]" : s }
function norm(s){ gsub(/0x[0-9a-fA-F]+/, "0xN", s); gsub(/[0-9]+/, "N", s); return s }
function benignreason(r){
  return (r ~ /still initializing or is exiting/ || r ~ /Dropping unsent request/ || r ~ /Connection closed/ ||
          r ~ /Connection refused|Socket not open|connect failed/ || r ~ /exit already started/ || r ~ /PhyManager is not set/)
}
BEGIN{
  n = split(TCVR, tl, ","); for (i = 1; i <= n; i++) if (tl[i] != "") focus[tl[i] + 0] = 1; nfocus = n;
  inwin = (FROM == "") ? 1 : 0; nstart = 0; nfatal = 0; nlines = 0; nb = 0; npsm = 0; lastpid = "";
}
{
  line = $0; pid = "";
  if (match(line, /\[[0-9]+\]: /)) pid = substr(line, RSTART + 1, RLENGTH - 4)
  sub(/^[A-Z][a-z][a-z] +[0-9]+ [0-9:]+ [^ ]+ [^ ]+\[[0-9]+\]: /, "", line)
  ts = "";
  if (match(line, /^[IVWEF][0-9][0-9][0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]/)) ts = substr(line, 2, 13)
  if (ts != "") {
    if (allfirst == "") allfirst = ts; alllast = ts;
    inwin = 1;
    if (FROM != "" && TO != "" && FROM > TO) inwin = (ts >= FROM || ts <= TO);   # year rollover
    else { if (FROM != "" && ts < FROM) inwin = 0; if (TO != "" && ts > TO) inwin = 0 }
  }
  if (!inwin) next
  if (line ~ /BspTransceiverIOTrace/) { nb++; next }
  nlines++; if (ts != "") { if (firstts == "") firstts = ts; lastts = ts }
  sev = substr(line, 1, 1);
}
# ---- service (re)starts ----------------------------------------------------------
pid != "" && pid != lastpid { if (lastpid != "") { nstart++; starts[nstart] = ts " (pid " lastpid " -> " pid ")" } lastpid = pid }
line ~ /QsfpServer.cpp:[0-9]+\] QSFP Service PHY SDK Version:/ { nboot++; boots[nboot] = ts }
# ---- fatal ------------------------------------------------------------------------
line ~ /Check failed:|^F[0-9][0-9][0-9][0-9] |terminate called|\*\*\* Aborted at|\*\*\* SIGSEGV|ERROR: AddressSanitizer|what\(\): / {
  nfatal++; if (nfatal <= 8) fatal[nfatal] = clip(line, 300)
}
# ---- transceiver state machine ----------------------------------------------------
line ~ /\[SM\]Transceiver:[0-9]+ / {
  t = line; sub(/^.*\[SM\]Transceiver:/, "", t); t = t + 0; seen[t] = 1;
  if (line ~ /State changed from [A-Z_]+ to [A-Z_]+/) {
    s = line; sub(/^.*State changed from /, "", s); sub(/ to /, "->", s); sub(/[^A-Z_>-].*$/, "", s);
    trans[t] = (t in trans) ? trans[t] "\n" : ""; trans[t] = trans[t] "      " ts "  " s; ntrans[t]++;
    to = s; sub(/^.*->/, "", to); final[t] = to; finalts[t] = ts;
  }
  if (line ~ / failed: ?|returned False/) {
    w = line; sub(/^.*\[SM\]Transceiver:[0-9]+ /, "", w);
    r = w; wrapper = w;
    if (index(w, "failed:") > 0) { wrapper = substr(w, 1, index(w, "failed:") - 1) "failed"; r = substr(w, index(w, "failed:") + 7) } else r = "(" w ")"
    sub(/^facebook::fboss::(thrift::)?/, "", r);
    key = t SUBSEP wrapper SUBSEP norm(clip(r, 180));
    if (!(key in rc)) { rfirst[key] = ts; rtext[key] = clip(r, 260) }
    rc[key]++; rlast[key] = ts;
    if (benignreason(r)) benigncnt[t]++; else if (wrapper !~ /readyTransceiver returned False/) { realcnt[t]++; }
  }
}
# ---- port state machine (port manager mode) ---------------------------------------
line ~ /PortStateMachine\.h:[0-9]+\] \[Port:[^]]*\] [A-Za-z]+ failed:/ {
  w = line; sub(/^.*PortStateMachine\.h:[0-9]+\] \[Port:[^]]*\] /, "", w);
  wrapper = "[PortSM] " substr(w, 1, index(w, "failed:") - 1) "failed"; r = substr(w, index(w, "failed:") + 7);
  sub(/^apache::thrift::(transport::)?/, "", r); sub(/^facebook::fboss::(thrift::)?/, "", r);
  key = "port" SUBSEP wrapper SUBSEP norm(clip(r, 180));
  if (!(key in rc)) { rfirst[key] = ts; rtext[key] = clip(r, 260) }
  rc[key]++; rlast[key] = ts;
}
line ~ /PortStateMachineController.cpp:[0-9]+\] Failed to apply PortStateMachineUpdate \[Port: PortID\([0-9]+\)/ {
  q = line; sub(/^.*\[Port: PortID\(/, "", q); sub(/\).*$/, "", q); if (!(q in pfailseen)) { pfailseen[q] = 1; npfail++; pfaillist = pfaillist (npfail <= 40 ? (pfaillist == "" ? "" : ",") q : "") }
}
line ~ /\[Port: [^,]*, PortID: [0-9]+\] State changed from/ {
  p = line; sub(/^.*\[Port: /, "", p); sub(/\] State changed.*$/, "", p);
  s = line; sub(/^.*State changed from /, "", s); sub(/ to /, "->", s);
  psm[p] = s; psmts[p] = ts; if (!(p in pseen)) { pseen[p] = 1; plist[++npsm] = p }
}
# ---- root-cause signatures (context: WHY) -----------------------------------------
{
  sig = "";
  if (line ~ /Unsupported Application/) sig = "Unsupported Application (module does not support the requested speed/host-lane application)";
  else if (line ~ /Unknown media lane code byte/) sig = "Unknown media lane code byte (blank/garbled EEPROM)";
  else if (line ~ /is missing transceiverManagementInterface|is missing moduleMediaInterface/) sig = "module unreadable / no management interface";
  else if (line ~ /no tunable optics config|Tunable optics/) sig = "tunable optics config gap";
  else if (line ~ /BspTransceiverIO::(read|write)\(\) failed|failed to read management interface|Read at offset .* failed/) sig = "I2C read/write failure";
  else if (line ~ /BaldEagleError|BcmPhyError|MdioError|SaiApiError|BcmError/) sig = "vendor PHY/SAI error";
  else if (line ~ /eyes still zero/) sig = "XPHY lane eyes zero (no signal into the external PHY)";
  else if (line ~ /CDB command failed/) sig = "CMIS CDB command failed";
  else if (line ~ /[Ff]irmware upgrade.*(fail|error)|upgradeFirmware.*(fail|error)|Failed to upgrade/) sig = "optics firmware upgrade failure";
  else if (line ~ /FBOSS_EVENT\(LINK_ALERT\)/) sig = "module latched link-fault flags (LINK_ALERT)";
  else if (line ~ /Load Shedding Due to Queue Timeout/) sig = "wedge_agent Thrift LOAD SHEDDING (agent overloaded; qsfp cannot program IPHY)";
  else if (line ~ /Could not find the valid speed combo of media intf id/) sig = "no valid speed combo for media interface + lanemask (profile vs module)";
  else if (line ~ /Datapath didn.t come out of deactivated state/) sig = "CMIS datapath stuck deactivated (module did not apply datapath config)";
  else if (line ~ /remediation_enabled=false/) sig = "remediation DISABLED by qsfp config (remediation tests cannot pass)";
  else if (line ~ /failed to release reset TCVR/) sig = "CPLD reset-release failure (causal only if it persists after startup)";
  else if (line ~ /[Rr]emediat(e|ion|ing)/ && line ~ /^[WE]/) sig = "remediation";
  else if (line ~ /ModuleFault|fwFault|module fault/ && line ~ /^[WE]/) sig = "module fault";
  if (sig != "") {
    st = "?"; if (match(line, /Transceiver:? ?[0-9]+/)) { st = substr(line, RSTART, RLENGTH); gsub(/[^0-9]/, "", st) }
    k = sig SUBSEP st; if (!(k in sc)) { sfirst[k] = ts; stext[k] = clip(line, 280) } sc[k]++;
    if (AT != "" && ts != "" && ts > AT) safter[sig]++;
    if (!(sig in sigtot)) { sigorder[++nsig] = sig } sigtot[sig]++;
    if (st != "?") { sigt[sig] = (sig in sigt) ? sigt[sig] : ""; if (index("," sigt[sig] ",", "," st ",") == 0) sigt[sig] = sigt[sig] (sigt[sig] == "" ? "" : ",") st }
  }
}
# ---- histogram ---------------------------------------------------------------------
line ~ /^[WEF][0-9][0-9][0-9][0-9] / {
  h = line; sub(/^[WEF][0-9]+ [0-9:.]+ +[0-9]+ /, "", h); h = norm(clip(h, 170));
  if (h ~ /still initializing or is exiting|Dropping unsent request|getConfigAppliedInfo|PhyManager is not set|ALL_PUBLISHERS_GONE|exit already started|Thrift serialization is only defined|preferred certificate file/) { nbenign++; next }
  if (h ~ /Event:TCVR_EV_[A-Z_]+ Failed to apply TransceiverStateMachineUpdate/) { ncompanion++; next }
  hist[h]++
}
END{
  if (nlines == 0 || firstts == "") {
    if (allfirst == "") { print "error: no glog records at all (empty file or not a qsfp_service log)" > "/dev/stderr"; exit 1 }
    if (AT != "") printf "Failure time %s covered:  %s  (whole log spans %s -> %s)\n", AT, (AT >= allfirst && AT <= alllast) ? "YES (but no lines inside --from/--to; widen the window)" : "NO  <-- this log does not span the failure: WRONG LOG (earlier run / earlier service start). Do not use it as evidence", allfirst, alllast;
    printf "error: no records inside --from %s --to %s; this log spans %s -> %s\n", FROM, TO, allfirst, alllast > "/dev/stderr";
    exit 3
  }
  print "==================== QSFP_SERVICE LOG ANALYSIS ====================";
  printf "Lines in window:          %d  (%s -> %s)%s\n", nlines, firstts, lastts, (FROM != "" || TO != "") ? "  [filtered --from " FROM " --to " TO "]" : "";
  printf "I2C trace lines skipped:  %d\n", nb;
  if (AT != "") printf "Failure time %s covered:  %s  (whole log spans %s -> %s)\n", AT, (AT >= allfirst && AT <= alllast) ? "YES" : "NO  <-- this log does not span the failure: WRONG LOG (earlier run / earlier service start). Get the right one before concluding anything", allfirst, alllast;
  if (nboot > 0) { printf "qsfp_service boots:       %d  at:", nboot; for (i = 1; i <= nboot && i <= 12; i++) printf " [%s]", boots[i]; print "" }
  if (nstart > 0) { printf "PID changes (restarts):   %d:", nstart; for (i = 1; i <= nstart && i <= 12; i++) printf " %s;", starts[i]; print "" }
  print  "  (tests like qsfpWarmbootIsHitLess / qsfpColdbootAfterAgentUp restart qsfp_service on purpose;";
  print  "   an unexpected restart or boot inside the window = crash — check the fatal section)";
  if (nfatal > 0) { print ""; printf "FATAL / crash lines:      %d\n", nfatal; for (i = 1; i <= nfatal && i <= 8; i++) printf "    %s\n", fatal[i] }

  # choose focus transceivers if none given: most non-benign failures
  if (nfocus == 0) {
    for (k = 1; k <= 8; k++) { best = -1; bt = ""; for (t in realcnt) if (!(t in focus) && realcnt[t] > best) { best = realcnt[t]; bt = t } if (bt == "") break; focus[bt] = 1; nauto++ }
    if (nauto == 0) {  # no hard SM failures: fall back to transceivers named by root-cause signatures
      for (k in sc) { split(k, kk, SUBSEP); if (kk[2] != "?") sigcnt[kk[2]] += sc[k] }
      for (k = 1; k <= 8; k++) { best = -1; bt = ""; for (t in sigcnt) if (!(t in focus) && sigcnt[t] > best) { best = sigcnt[t]; bt = t } if (bt == "") break; focus[bt] = 1; nauto++ }
      if (nauto > 0) focusnote = "  (auto: no hard state-machine failures; picked transceivers with the most root-cause signature hits — pass --tcvr for the ones the link test waited on)"
    }
  }
  print "";
  if (nsig > 0) {
    print "Root-cause signatures (context — the WHY; count / transceivers / first occurrence):";
    if (nfocus > 0) print "  (focus = the --tcvr transceivers; other-tcvr = modules the test did not wait on: usually NOT evidence; global = no transceiver id, e.g. agent load shedding)";
    for (i = 1; i <= nsig; i++) {
      s = sigorder[i]; fcnt = 0; ftc = ""; ocnt = 0; gcnt = 0;
      for (k in sc) { split(k, kk, SUBSEP); if (kk[1] != s) continue; if (kk[2] == "?") gcnt += sc[k]; else if (kk[2] in focus) { fcnt += sc[k]; ftc = ftc (ftc == "" ? "" : ",") kk[2] } else ocnt += sc[k] }
      if (nfocus > 0) printf "  - %-70s focus x%d [%s]  other-tcvr x%d  global x%d%s\n", s, fcnt, ftc, ocnt, gcnt, (s in safter) ? "  (after failure: " safter[s] ")" : "";
      else printf "  - %-70s x%-6d tcvr [%s]%s\n", s, sigtot[s], sigt[s], (s in safter) ? "  (after failure: " safter[s] ")" : "";
      shown = 0; for (k in sc) { split(k, kk, SUBSEP); if (kk[1] == s && shown < 2 && (nfocus == 0 || kk[2] == "?" || (kk[2] in focus))) { printf "      first@%s: %s\n", sfirst[k], stext[k]; shown++ } }
    }
  } else print "Root-cause signatures:    none of the known ones matched.";

  print "";
  print "State-machine failure reasons (WARN wrappers; text after \"failed:\" is the WHY), grouped:";
  # group identical (wrapper, normalized reason) across transceivers
  for (key in rc) {
    split(key, kk, SUBSEP); t = kk[1];
    if (benignreason(rtext[key])) continue;
    if (nfocus > 0 && t != "port" && !(t in focus)) { otherfail += rc[key]; continue }
    g = kk[2] SUBSEP kk[3];
    if (!(g in gc)) { gorder[++ng] = g; gtext[g] = rtext[key]; gfirst[g] = rfirst[key]; glast[g] = rlast[key]; gt[g] = "" }
    gc[g] += rc[key]; if (rfirst[key] < gfirst[g]) gfirst[g] = rfirst[key]; if (rlast[key] > glast[g]) glast[g] = rlast[key];
    if (t != "port") { gt[g] = gt[g] (gt[g] == "" ? "" : ",") t; if (t in focus) gfocus[g] = 1 } else gt[g] = "ports"
  }
  shown = 0;
  for (pass = 1; pass <= 2; pass++) for (i = 1; i <= ng; i++) {
    g = gorder[i]; split(g, kk, SUBSEP);
    transient = (kk[1] ~ /readyTransceiver returned False/);
    if ((pass == 1 && transient) || (pass == 2 && !transient)) continue;
    if (shown >= 25) break;
    printf "  %-34s x%-6d tcvr [%s]  %s -> %s%s\n      %s\n", kk[1], gc[g], gt[g], gfirst[g], glast[g], (transient ? "  (transient retry; normal during bring-up)" : ""), gtext[g]; shown++
  }
  if (ng == 0) print "  (none)";
  if (otherfail > 0) printf "  (%d failure lines on transceivers outside --tcvr not shown)\n", otherfail;
  if (npfail > 0) printf "  port state machine updates failed on %d distinct PortID(s): %s%s\n", npfail, pfaillist, (npfail > 40 ? ",..." : "");
  bsum = 0; for (t in benigncnt) bsum += benigncnt[t];
  if (bsum > 0) printf "  benign agent-restart/exit reasons suppressed: %d (\"switch is still initializing or is exiting\", \"Dropping unsent request\")\n", bsum;

  print "";
  print "Focus transceivers — final state and last transitions:" focusnote;
  for (t in focus) {
    printf "  tcvr %s: final=%s (at %s), %d transitions%s\n", t, (t in final ? final[t] : "(no transition in window)"), (t in finalts ? finalts[t] : "-"), ntrans[t] + 0, (t in seen ? "" : "  [never mentioned in window]");
    if (t in trans) { m = split(trans[t], tt, "\n"); for (j = (m > 8 ? m - 7 : 1); j <= m; j++) print tt[j] }
  }
  if (npsm > 0) {
    print ""; printf "Port state machine (port manager mode), %d port(s), last transition:\n", npsm;
    for (i = 1; i <= npsm && i <= 30; i++) printf "  %-14s %s  %s\n", plist[i], psmts[plist[i]], psm[plist[i]];
  }

  print ""; print "Top WARN/ERR/FATAL messages (numbers normalized to N; benign suppressed: " nbenign "; \"Event:... Failed to apply\" companions of the wrapper WARNs above: " ncompanion "):";
  for (k = 1; k <= 20; k++) { best = 0; bh = ""; for (h in hist) if (hist[h] > best) { best = hist[h]; bh = h } if (bh == "") break; printf "  %6d  %s\n", best, bh; delete hist[bh] }
  print "";
  print "Reading tips:";
  print "  - A transceiver the link test waited on that never reaches ACTIVE (or TRANSCEIVER_PROGRAMMED in";
  print "    port-manager mode) is the qsfp-side cause; its failure reasons above say why.";
  print "  - \"switch is still initializing or is exiting\" / \"Dropping unsent request\" = wedge_agent was";
  print "    restarting between tests; benign unless it is the only thing happening for many minutes.";
  print "  - Media rxLos with healthy TX on this side usually means no light from the PEER (fiber/peer optic).";
  print "==================================================================";
}
' "$SRC"
