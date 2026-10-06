# Improvement register — c9core cluster port to TrinityCore

One entry per item: what, why, cost, risk. Reviewed at each phase boundary. Nothing here is built without an explicit decision; items marked **APPLIED** were approved and are in the `cluster` branch, items marked **BACKPORT** are AC-side (c9core) bugs found by the port.

## Decided / applied in phase 1

1. **Do not vendor the Go nats-server** — APPLIED. The TC tree carries only `dep/cnats`; the broker (`c9-nats` on wow-node-01) is an external dependency.
2. **TC-native login behaviour on the redirect path** — APPLIED (user decision). Error replies encrypted with the redirect keys; ban / IP-lock / country-lock checks before the token wait; negative mute-time fix-up. AC does none of these on the redirect path.
3. **Pre-delete cleanup on the three login kick/teardown paths** — APPLIED, BACKPORT. `CleanupsBeforeDelete()` instead of `RemoveAllAuras()`. AC's identical `~Unit` aura asserts make the double-redirect and token-failure kicks a latent crash in c9core; TC additionally needs it because instance binds hold `Player*`.
4. **Forced save on delayed (spell-cast) teleports** — APPLIED, BACKPORT. Hearthstone/portal teleports re-enter `TeleportTo` with the far semaphore up, so `SaveToDB` was a no-op and the handoff row carried only the position. Semaphore cleared/restored around the save.
5. **Transport offsets captured before the detach** — APPLIED, BACKPORT. The snapshot ran in the commit callback after the synchronous detach zeroed the movement-info transport fields; boat/zeppelin arrivals landed at the transport origin.
6. **Zone-fire guards and redirected-out hard guards** — APPLIED, BACKPORT. The dwell fire aborts while a far teleport is pending / the session is redirected out / the player is out of world; `SaveToDB` and the far `TeleportTo` branch return early on a redirected-out session (save-before-load).
7. **Deactivation drain** — APPLIED, BACKPORT. `CleanupsBeforeDelete` + unconditional `RemovePlayerFromMap(p, true)` (AC deletes without cleanup; TC needs it for trade/duel/transport back-pointers).
8. **Null-guarded `CreateBaseMap` callers** — APPLIED (PoolMgr, GameEventMgr as AC; InstanceSaveMgr and cs_go beyond AC, BACKPORT).
9. **Stock-TC isolation** — APPLIED. `NatsBus::Update` inert unless initialised; transport sync gated on cluster + DB guid; `CMSG_AUTH_CONTINUED_SESSION` rejected on stock; no PONG to a redirected-out session.
10. **Commit-latency observability** — APPLIED. One INFO line per handoff path reporting save-dispatch → commit-callback ms.

## Open — phase 1 follow-ups (decide after the handoff tests)

11. **Send the redirect from the commit callback.** Today the client reconnects as soon as the save is dispatched and the destination parks the socket up to 3 s for the token. Moving `RedirectClient`/`SuspendClient`/`SetRedirectedOut` into the `AfterComplete` callback removes the race entirely. Decide with the measured latency from item 10. Also: make the 3 s wait configurable.
12. **Token tied to the source session's callback processor.** If the old connection dies before the commit callback runs, the session is freed and the token is never published; the destination fails after 3 s. Use a world-level callback processor. Same in AC.
13. **Pet rows written on zone-path teardown.** `CleanupsBeforeDelete → RemoveFromWorld → UnsummonPetTemporaryIfAny → RemovePet(PET_SAVE_AS_CURRENT)` saves the stale pet after the destination has loaded. Same in AC.
14. **Source keeps broadcasting after a teleport handoff.** `m_pendingZoneReroute` is not set on the teleport path, so the source publishes state deltas with itself as owner until the socket closes. Same in AC.
15. **`ClaimPlayer` semantics at zone fire** claim for the source node; peers record the source as owner. Same in AC.
16. **Bus down: wrong-node cold login loops.** The wrong-node block gates on `IsEnabled()` only; with NATS down the client is redirected without a token and gets `AUTH_FAILED` every attempt. Gate on `IsConnected()` and log in locally instead.
17. **New node learns routes only at the peers' next periodic announce.** A freshly started node registers peers when they re-announce; until then its route table is empty. Add a "request announce" on join.
18. **Transport resync skips teleport frames.** `HandleTransportSync → InitializeToTime` on a running transport leaves it on its current map if the correction crosses a map boundary.
19. **`GROUP_REROUTE_TO_MAP` relay** teleports with the member's current x/y/z, which belong to a different map (phase 2 path).
20. **Stuck loading screen** when no destination node or no token can be produced on the teleport path (`SMSG_TRANSFER_PENDING` sent, no `NewWorld`). Same in AC.
21. **`CHAR_UPD_CHARACTER_POSITION`** now sets `instance_id = 0` and is `CONNECTION_BOTH`; stock `SavePositionInDB` inherits that. Use a separate cluster statement.
22. **`ClearOnlineAccounts`** account and battleground resets are realm-scoped, not node-scoped (TC's two lines left as upstream).
23. **Config hygiene:** `ClusterServer.NodeId` defaults to 1 in `ClusterMgr.cpp:262` and in the conf `Default:` line but 0 in `Main.cpp`/`NatsBus.cpp`; validate 1–255; `NodeDeadThreshold` is a dead key.
24. **CLI:** non-tty `std::getline` blocks shutdown when stdin is a held-open pipe (matters for the phase 3 supervisor); interactive Ctrl-D now stops the server (AC semantics, differs from upstream).
25. **Unguarded `CreateBaseMap` dereferences** remain in `OutdoorPvP.cpp:661` and `Transport.cpp:706`; only reachable for continents in legacy `InstanceServer.Enable` mode.
26. **Double `BroadcastPlayerTransferFull`** on cache-activate login and in `LearnTalent` (via `LearnSpell`); a burst on spell-chain relearn.
27. **Zone-scoped spawning with a single ownership predicate** — now also the way to drop the full-continent preload a zone node needs today (cluster nodes must run `GridUnload = 0` + `BaseMapLoadAllGrids = 1`: on-demand grid loading caused arrival hangs and a crash window between stale-copy deactivation and the new arrival in the same grid, 2026-10-05). (from the original register): a zone node must also load its zones' maps; proper fix = a player belongs on node N iff N owns the zone, or nobody claims the zone and N owns the map.
28. **Verify the client's 20-byte redirect proof** at the destination (original register item).
29. **Split NatsBus** (3,350 lines) after phase 2 lands.
30. **Non-owned instanceable map lookups are never cached** (`GetAreaId/GetZoneId` on such maps take the maps lock and log DEBUG every call).

## Backport list to c9core (AzerothCore)
Items 3, 4, 5, 6, 7, 8 (InstanceSaveMgr/cs_go part), and the audit findings 12–16, 18, 20.
