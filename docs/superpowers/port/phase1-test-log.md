# Phase 1 handoff tests — runbook and log

Build under test: `cluster` branch **704a410128** (worldserver sha256 `10624659bf88c5e7…`, identical on all three nodes).
Spec: `docs/superpowers/specs/2026-10-05-tc-cluster-port-phase1-design.md` §9.

## Realm layout (three nodes; a node that advertises zones never claims a map, so the zone node is separate)

| Node | VM | Address | `ClusterServer.Maps` | `ClusterServer.Zones` | Role |
| --- | --- | --- | --- | --- | --- |
| 1 | 170 wow-node-01 | 192.0.2.20 | `0,530` | `""` | Eastern Kingdoms + Outland; authserver; NATS broker; DB updater |
| 2 | 171 wow-node-02 | 192.0.2.21 | `1,571` | `""` | Kalimdor + Northrend |
| 3 | 172 wow-node-03 | 192.0.2.22 | `1` (terrain only, not advertised) | `1637` | Orgrimmar zone node |

Databases: `tc_auth`, `tc_characters`, `tc_world` on mariadb-01 (192.0.2.110), user `trinity` (password in the credentials file, row "TC cluster port DBs (2026-10-05)").
Client: stock 3.3.5a build 12340; realmlist `set realmlist 192.0.2.20`. Test account: `test` / `test`, GM level 3.

## Operating the nodes

Each worldserver runs in a tmux session named `tcnode1|2|3` on its VM; logs under `~/tc-335/logs/`.

```bash
# start (node N): on the VM
tmux new -d -s tcnodeN "cd ~/tc-335/bin && ./worldserver -c ~/tc-335/etc/worldserver.conf 2>&1 | tee -a ~/tc-335/logs/nodeN.txt"
# console: tmux attach -t tcnodeN   (detach: Ctrl-b d)
# stop: send "server shutdown 1" to the console, or tmux send-keys -t tcnodeN "server shutdown 1" Enter
# authserver (node 1 only): tmux new -d -s tcauth "cd ~/tc-335/bin && ./authserver -c ~/tc-335/etc/authserver.conf 2>&1 | tee -a ~/tc-335/logs/auth.txt"
```

Per test window, on every node: set `ClusterServer.PacketTrace.Enable = 1` and `ClusterServer.RedirectDebug = 1` in `worldserver.conf`, then `reload config` on the console; set both back to 0 and reload afterwards (RedirectDebug logs session keys). Mark the start of each test in every node log:

```bash
for h in 20 21 22; do ssh wow@192.0.2.10.$h 'echo "=== TEST N $(date -u +%FT%TZ) ===" >> ~/tc-335/logs/node*.txt'; done
```

Audit greps after each test (run on each node; expected results in the table):

```bash
L=~/tc-335/logs/node*.txt
grep -nE "SMSG_CONNECT_TO|redirect auth|redirected out|handoff: save commit|arrived via redirect|kicked|AntiDOS|MOTD|SPELL_START|Unable|Error" $L | tail -40
```

## Tests

Character requirements: level 10+, at least one talent point spent, one long-duration buff active (e.g. a class self-buff) before every handoff.

| # | Test | Steps | Expected | Result | Log pointer |
| --- | --- | --- | --- | --- | --- |
| 1 | Cold login, correct node | Character in Elwynn Forest (map 0); log in | Lands on node 1; `characters.owning_node_id = 1`; no `SMSG_CONNECT_TO` in any log | | |
| 2 | Cold login, wrong node | Character in Durotar (map 1); log in | Node 1 logs `SMSG_CONNECT_TO` to 192.0.2.21; node 2 logs redirect auth success and auto-login; one loading screen; no MOTD on arrival; no login sparkle | | |
| 3 | Zone path | From Durotar walk into Orgrimmar, stand 3 s, walk out | Node 2 → node 3 handoff (`handoff: save commit … ms`, then `SMSG_CONNECT_TO`); arrival on node 3 with same HP/buff/talents; walk-out hands back to node 2; no `kicked` on any node | | |
| 4 | Teleport path | From Kalimdor hearth to a Stormwind inn | Node 2 logs `SMSG_CONNECT_TO` + `redirected out`; node 1 arrival; node 2 shows the session gone with no save line after the redirect | | |
| 5 | Map path (transport) | Boat Menethil (map 0) → Theramore (map 1), or Deeprun Tram | Node 1 → node 2 handoff on the map change; player stands where they stood on the deck (offsets non-zero in the transfer log) | | |
| 6 | Death on the zone border | Die just inside Orgrimmar's zone, release, resurrect | No handoff while dead (`!IsAlive` re-arm); release works; handoff resumes after resurrection | | |
| 7 | Node restart | Stop node 2 for 70 s, restart | Node 1/3 log the dead node and drop maps 1/571, then re-add on the announce; a Kalimdor login during the gap is refused cleanly | | |

Audit checklist per test (spec §9): no packet to a redirected-out session; no stale-session kick; no 0-HP alive arrival; teleport path marked redirected out; no cast packets before in-world; no MOTD on arrival.

## Results

### 2026-10-05 — map-to-map handoff node 1 → node 2 (teleport path) — PASS (user: "map to map works")
Commit 704a410128. Character "Split" (guid 1), account gmaccount. Node 1 `~/tc-335/logs/Server.log`:635-637 — `Sent SMSG_CONNECT_TO to Split -> 192.0.2.21:8085`, `Sent SMSG_SUSPEND_COMMS`, `save commit … took 89 ms (success=true)`. Node 2 `Server.log`:576-583 — proof received from 192.0.2.1, token not yet present, `validated redirect token … (waited 59ms)`, authenticated via redirect, `auto-login guid 0x1`. No kick, no AntiDOS, no WARN on any node. Latency data for register item 11: commit 89 ms, destination wait 59 ms.

### 2026-10-05 — transport (boat) handoff node 2 → node 1 (map path) — PASS (user: "transports work")
Commit 704a410128. Character "Split" on a boat Kalimdor (map 1) → Eastern Kingdoms (map 0). Node 2 `Server.log`:686-688 — `Sent SMSG_CONNECT_TO to Split -> 192.0.2.20:8085`, `SMSG_SUSPEND_COMMS`, `save commit … took 16 ms (success=true)`. Node 1 `Server.log`:741-750 — proof from 192.0.2.1, `validated redirect token … (waited 0ms)`, auto-login, **`Block1 transport check: onTransport=true entry=176310 offset=(-4.45,5.48,6.10)`**, `Block1 transport reattach OK: entry=176310 map=0 worldPos=(-9021.9,1422.5,6.1)`. Non-zero offsets confirm register item 5 (captured-before-detach fix); the player re-attached to the same boat on the destination map. No kick/AntiDOS/WARN.

### 2026-10-05 — cold login on the wrong node, node 1 → node 2 — PASS
Commit 704a410128. New Horde character "Wuchi" (guid 2) in Durotar (map 1), logged in via the realmlist node 1. Node 1 `Server.log`: `Player Wuchi on wrong node (map 1), redirecting to node 2`, `Sent SMSG_CONNECT_TO … 192.0.2.21:8085`, `SMSG_SUSPEND_COMMS`. Node 2: proof, `validated redirect token … (waited 0ms)`, auto-login guid 0x2, `Block1 transport check: onTransport=false`. Nodes 1 and 3: `Remote player ONLINE … 'wuchi' zone=14 level=1 node=2`. No kick/WARN.

### 2026-10-05 — zone handoff Durotar → Orgrimmar, node 2 → node 3 — PASS (user: "zone transfers work")
Commit 704a410128. Character "Wuchi" (guid 2). Node 2 `Server.log`:729-738 — `entered non-local zone 1637 — dwell timer started (2000ms)`, `zone transfer: zone 1637 -> node 3 [dwell expired]`, `Claiming ownership`, `Sent SMSG_CONNECT_TO … 192.0.2.22:8085` (no SUSPEND on the zone path, as designed), `save commit … took 19 ms (success=true)`, then `Remote player ONLINE … zone=1637 node=3`. Node 3 `Server.log`:722-732 — `Node 2 claimed GUID`, proof, `validated redirect token … (waited 0ms)`, auto-login, arrival. Node 2:702 also shows `LogoutPlayer: Split redirected out, tearing down without save` for the earlier character (redirect-out fast path). No kick/WARN.

