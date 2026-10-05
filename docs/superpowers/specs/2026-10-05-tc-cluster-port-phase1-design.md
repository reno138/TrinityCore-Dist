# c9core cluster port to TrinityCore 3.3.5 — Phase 1 design

Date: 2026-10-05
Status: approved in discussion, pending written-spec review
Source of the port: c9core `main` at f6d4a147f (AzerothCore-based, github.com/reno138/c9core)
Target: TrinityCore `3.3.5` at 092eb27b20, branch `cluster` in `~/source/Trinitycore`

## 1. Goal and phasing

Port the c9core distributed-realm code ("cluster code") from its AzerothCore base
onto a fresh TrinityCore 3.3.5 tree. The AC-based c9core stays as it is; this is a
second, TC-based implementation of the same design and wire protocol.

The port is phased. Each phase has its own spec, plan and success predicate.

| Phase | Scope | Success |
| --- | --- | --- |
| 1 (this spec) | worldserver cluster core: bus, routing, handoff on map, teleport and zone paths | two TC nodes hand off a real client on all three paths |
| 2 | cross-node features: chat, mail notify, group, LFG, battleground queue, arena, social, calendar, guild | each feature works across two nodes |
| 3 | sidecar apps: nodemgr, clustermgr (TUI + web) | both manage a TC node |

## 2. Decisions taken

1. **Phased, not monolithic.** A monolithic port has no checkable success condition until everything links.
2. **Reuse `~/source/Trinitycore` in place.** Fetched and reset to `origin/3.3.5`; work on branch `cluster`; `3.3.5` stays a pristine tracking branch.
3. **Build on wow-node-01 only** (VM 170, PVE node1, Ubuntu 26.04). No laptop build.
4. **TC databases on mariadb-01** (192.0.2.110): new schemas `tc_auth`, `tc_characters`, `tc_world`, user `trinity` scoped to them. Nothing shared with the AC schemas.
5. **All three handoff paths in phase 1:** map, teleport, zone.
6. **Faithful port.** Behaviour as it runs on the AC four-node lab today, known gaps included. Improvement ideas go to a register (section 8) and are discussed, not built in.
7. **No rebranding of TC.** TC macros, logging, namespaces and file layout stay. No `C9_` renames, no touching files that carry no hook.
8. **Port method: hunk transplant with an API map.** Clean diff of c9core against its AC merge base (950684036), rename noise and non-cluster patches dropped, the 45 new files copied and translated, each hook re-applied by hand at the equivalent TC function.
9. **The Go nats-server broker is not vendored.** TC nodes use the broker already running on wow-node-01 (`c9-nats`, :4222). A broker is an external dependency of the TC port, like the database.

## 3. What is ported and what is not

### 3.1 From the c9core diff (385 files vs AC 950684036)

- **Ported:** the 45 new files; the hooks in the phase-1 files listed in section 5; the two SQL changes; the `ClusterServer.*` conf block.
- **Dropped as noise:** about 800 lines of `AC_*_API` → `C9_*_API` and `ACORE_` → `C9CORE_` renames across 186 files with ≤5 changed lines; header/attribution changes.
- **Dropped as not cluster code:** PathGenerator, TargetedMovementGenerator, WaypointMovementGenerator, Transport.cpp gameplay fixes unrelated to peer sync, the ToCloud9 strip (TC9Sidecar deletions, InstanceSaveMgr, TradeHandler, Battleground.cpp deletions), MapBuilder, ConditionMgr, Spell.cpp, Item.cpp, DatabaseWorkerPool, Timer.h, custom_script_loader. TC has none of the ToCloud9 code, so there is nothing to strip.
- **Deferred to phase 2:** hooks in LFGMgr, LFGHandler, BattleGroundHandler, GroupHandler, Group, MiscHandler, CalendarMgr, SocialMgr, Guild, Pet, ChatHandler.
- **Deferred to phase 3:** `src/server/apps/nodemgr`, `src/server/apps/clustermgr`, `BoostProcess.h`, the two sidecar `.conf.dist` files and the systemd unit.

### 3.2 Wire protocol

Unchanged. NatsBus keeps its full message-type enum, subject layout, HMAC sealing (ClusterAuth) and dispatcher. Phase-2 message handlers are present but empty (debug log only) so a phase-1 node and a later phase-2 node can share a bus without crashing each other. Config key names, including the case-sensitive `ClusterServer.NatsURL`, are kept.

## 4. AC → TC API map

Written to `docs/superpowers/specs/2026-10-05-ac-to-tc-api-map.md` before any code moves and used mechanically. Known entries:

| AzerothCore (c9core) | TrinityCore 3.3.5 |
| --- | --- |
| `LOG_INFO/ERROR/WARN/DEBUG/TRACE/FATAL("cat", ...)` (238 uses in new files) | `TC_LOG_INFO/ERROR/WARN/DEBUG/TRACE/FATAL("cat", ...)` |
| `sConfigMgr->GetOption<T>(key, def)` (56 uses) | `sConfigMgr->GetIntDefault / GetBoolDefault / GetStringDefault / GetFloatDefault(key, def)` |
| `sWorldSessionMgr->FindSession / AddSession / ...` | `sWorld->FindSession / AddSession / ...` |
| `Acore::` | `Trinity::` |
| `MapMgr`, `sMapMgr` | `MapManager`, `sMapMgr` |
| `AuthCrypt::Init` | `WorldPacketCrypt::Init(K)` / `Init(K, serverKey, clientKey)` |
| `SMSG_REDIRECT_CLIENT` | `SMSG_CONNECT_TO` (0x50D, same payload) |
| `MotionTransport` / `StaticTransport` | single `Transport : GameObject, TransportBase` |
| `PlayerUpdates.cpp` (`Player::Update`, `UpdateZone`, `UpdateArea`) | same functions in `Player.cpp` |
| `WorldSession::KickPlayer(bool)` overload | `KickPlayer(std::string const& reason)` only |
| `CharacterDatabase.AsyncCommitTransaction(trans)` + callback | `TransactionCallback` → `.AfterComplete(std::function<void(bool)>)` |
| `AddonMgr` | `AddonMgr` (present in TC, verify signature) |
| `ObjectAccessor::FindPlayer` etc. | same names, verify signatures |

The map is extended as the port finds new deltas; every entry added is a one-liner.

## 5. Phase 1 slabs (one commit each, each compiles before the next starts)

1. **Dependency slab.** `dep/cnats/` = c9core `deps/cnats` (nats.c 3.8.0). Wired in `dep/CMakeLists.txt` as in c9core: static lib, no TLS, no streaming, no examples, `EXCLUDE_FROM_ALL`, `-std=c11` override. Game library links `nats_static`. `dep/PackageList.txt` entry added.
2. **Shared cluster slab.** `src/server/shared/Cluster/`: `ClusterAuth.{h,cpp}`, `PskCrypt.{h,cpp}`, `ClusterMgmtProtocol.h`. No game dependencies; only log/config translation.
3. **Core slab.** `src/server/game/Server/`: `NatsBus`, `ClusterMgr`, `ClientRedirect`, `PlayerTransfer`, `PlayerStateSync`, `SharedPlayerCache`, `SharedPlayerState.h`, `PacketTrace`. Phase-2 handlers in NatsBus stubbed (see 3.2).
4. **Hook slab.** Hooks placed in these TC functions:
   - `WorldSocket.cpp/.h`: constructor, `Update`, `ReadHandler`/`ReadDataHandler`, `HandleAuthSession` + callback (redirect token path, per-connection crypt init with redirect seeds), `SendPacket` drop once redirected out.
   - `WorldSession.cpp/.h`: constructor/destructor registration, `Update` (no inbound processing when redirected out), `LogoutPlayer(save, redirecting)`, `SendPacket` drop once `_redirectedOut`, `SetRedirectedOut`, redirected-out sessions exempt from AntiDOS kicks and from the stale-session cleanup, `SendAddonsInfo`, `InitializeSessionCallback`.
   - `CharacterHandler.cpp`: `LoginQueryHolder::Initialize`, `HandlePlayerLoginOpcode` (map/zone ownership routing; zone redirect deliberately skipped at login), `HandlePlayerLoginFromDB` (redirect-token arrival, "landed on wrong node" disconnect, no MOTD resend on arrival, 0-HP arrival → `KillPlayer`), `HandlePlayerLoginToCharInWorld`, `HandleCharFactionOrRaceChangeCallback`.
   - `Player.cpp/.h`: dwell timer + zone-ownership check in `Update`, `UpdateZone`, `UpdateArea` (requires `IsAlive()`); `TeleportTo` cross-node routing with `SetRedirectedOut`; `KillPlayer`, `ResurrectPlayer`, `SpawnCorpseBones`; `SendInitialPacketsAfterAddToMap` cast-packet suppression until in world; state-sync delta hooks in `_SaveCharacter`, `_SaveTalents`, `_SaveGlyphs`, `_SaveEquipmentSets`, `_SaveSpellCooldowns`, `_SaveEntryPoint`, `learnSpell`, `LearnTalent`, `resetTalents`, `ActivateSpec`, `GiveLevel`, `ModifyHonorPoints`, `ModifyArenaPoints`, `RemoveAtLoginFlag`, `RemovePet`, `SetRandomWinner`, `ApplySpellMod`, `LeaveBattleground`, `_LoadSkills`, `_LoadBrewOfTheMonth`; `Say`/`Yell`/`TextEmote` hooks are phase 2 (chat relay) and are skipped.
   - `World.cpp`: `LoadConfigSettings` (cluster keys), `SetInitialWorldSettings` (bus init, `SetWorldReady`), `Update` (bus tick), `ShutdownCancel`, `LoadDBVersion`.
   - `MapManager.cpp`: `CreateBaseMap` ghost map for non-local maps; `GenerateInstanceId` node offset.
   - `TransportMgr.cpp`, `Transport.cpp`: continental transport peer sync (`SendTransportSync`/`QueryTransportSync`), adapted from `MotionTransport` to TC `Transport` (`CreateTransport`, `SpawnContinentTransports`, `Update`, `MoveToNextWaypoint`, `UpdatePassengerPositions`).
   - `WorldPacketCrypt.cpp`: redirect-seed key derivation (AC's `AuthCrypt::Init` hook).
   - `worldserver/Main.cpp`: node id, bus bootstrap, `ClearOnlineAccounts` scoped to `owning_node_id`, `StartDB`. `CliRunnable.cpp`: RA command relay.
5. **SQL and conf slab.** `sql/updates/characters/3.3.5/2026_10_05_00_characters_owning_node_id.sql` (adds `owning_node_id TINYINT UNSIGNED NOT NULL DEFAULT 0` + index after `online`; `transguid` → `INT UNSIGNED`). The `account_redirect`/`cluster_node_status` drops are AC-only and not needed. `ClusterServer.*` block appended to `worldserver.conf.dist`.

## 6. Environment

- **VMs:** `qm start 170` (wow-node-01) and `171` (wow-node-02) from root@node1. Node 1 builds, node 2 receives `~/tc-335/bin` and data by rsync. AC install `~/335` on both stays present and **stopped**.
- **Checkout/build on node 1:** `~/source/c9core-tc` (clone of the laptop repo's `cluster` branch), cmake into `~/source/c9core-tc/build`, install prefix `~/tc-335`. Build log at `build/build.log`; it is grepped, never pasted whole.
- **Ports:** worldserver 8085, authserver 3724, same as AC; the TC realm takes the ports while AC is stopped. No VyOS or realmlist change.
- **Databases:** `tc_auth`, `tc_characters`, `tc_world` on mariadb-01; world from the latest TDB 335 full dump; TC's updater runs on node 1 only; node 2 `Updates.EnableDatabases = 0`.
- **Bus:** `c9-nats` on wow-node-01:4222, same `ClusterServer.AuthKey`. AC and TC worldservers must never be on the bus at the same time (shared subjects).
- **Client data:** TC extractors on node 1 from the QNAP `wow-335` share → `~/tc-335/data` (dbc, maps, vmaps). **mmaps skipped in phase 1**, `mmap.enablePathFinding = 0`.
- **Realm split for the test:** node 1 = maps 0, 530 + zone 1637 (Orgrimmar); node 2 = maps 1, 571.
- **Node MTU** stays 1500 on the routed VLANs (DBnet path).

## 7. Failure handling

- **Attribution:** every bring-up failure is a porting defect first. The question is always "what does the AC node do here, and where did the translation diverge". Only a demonstrated AC-side bug becomes a design finding, and that goes to the register.
- **Build:** one slab at a time; a break points at one slab.
- **Runtime:** `Logger.network = 3`; `PacketTrace.Enable` and `RedirectDebug` on for the test window only (RedirectDebug logs session keys); tcpdump + offline ARC4-drop1024 decrypt (HMAC-SHA1(seeds, session_key) for redirect connections) when the packet stream is in question. Reassemble by byte offset, not per segment.
- **Rollback:** stop TC services, start AC services. TC databases are separate; the AC realm is never at risk.
- **Known gaps carried over unchanged:** destination never verifies the client's 20-byte redirect proof; ghosts do not hand off; zone nodes must also own their zones' maps (spawning is not zone-scoped); AntiDOS policy is whatever TC ships.

## 8. Improvement register

`docs/superpowers/improvements.md` on the `cluster` branch. One paragraph per entry: what, why, cost, risk. Reviewed at each phase boundary. Initial entries:

1. Drop the vendored Go nats-server (decided for TC, section 2.9).
2. Verify the client's 20-byte redirect proof at the destination.
3. Zone-scoped spawning with a single ownership predicate: a player belongs on node N iff N owns the player's zone, or nobody claims that zone and N owns the map.
4. Split NatsBus (3,350 lines) into transport, routing and per-feature handlers after phase 2 lands.

## 9. Phase 1 success predicate and tests

**Success:** two TC worldservers, built from one commit on `cluster`, running on wow-node-01 and wow-node-02 against mariadb-01, hand off a stock 12340 client whose character has talents and active auras, on all three paths (map, teleport, zone). After each handoff the client sees one loading screen and no disconnect; the character arrives alive with the same HP, auras, talents, pet state and inventory; the source node has no leftover session for that character.

**Does not count:** compiles but no handoff run; works only with a bare character; works only on the map path; works only when the client shares a LAN segment with both nodes; any phase-2 feature claimed as working; a handoff that succeeds only after a relog.

**Auditor checklist (from the 2026-10-02 bugs):**
1. Packets sent to a session after it was marked redirected out.
2. Stale-session cleanup kicking the source's own redirected-out session.
3. Dead or 0-HP character handed off alive.
4. Teleport-path handoff missing the redirected-out mark.
5. Cast visuals (SPELL_START/GO/PLAY_SPELL_VISUAL/IMPACT) replayed to an arrival before it is in world.
6. MOTD resent on arrival.

**Test sequence** (named character, both node logs captured to files per test):
1. Cold login landing on the correct node.
2. Cold login landing on the wrong node → redirected.
3. Orgrimmar walk-in and walk-out (zone path).
4. Hearthstone across continents (teleport path).
5. Deeprun Tram or boat (map path).
6. Death on a zone border, release.
7. Node 2 restart while node 1 is up: dead-node routing drop and re-announce.

**Verification before "done":** a fresh-context review of the hook slab against the AC source, file by file, hunting the checklist, before the two-node test. Every "passed" claim in the final report points at a log line or a capture.

## 10. Out of scope for phase 1

Phase 2 and 3 items (section 3.1), mmaps, public GitHub repo for the TC port, any change to the AC-based c9core, any improvement-register item.
