# Improvement register — c9core cluster port to TrinityCore

One paragraph per entry: what, why, cost, risk. Reviewed at each phase boundary.
Nothing here is built without an explicit decision. See the phase 1 design spec, section 8.

## 1. Do not vendor the Go nats-server broker (DECIDED for the TC port, 2026-10-05)

**What:** the TC tree carries only the nats.c client under `dep/cnats`; the broker is an external dependency like the database.
**Why:** removes the Go toolchain from the build host and ~a vendored Go tree; wow-node-01 already runs `c9-nats`.
**Cost:** a from-scratch install of the TC port needs a broker from elsewhere; documented in the README.
**Risk:** AC and TC deploys differ in what "install" produces. Accepted.

## 2. Verify the client's 20-byte redirect proof at the destination

**What:** the destination node currently checks only the one-time token and source IP; the 20-byte proof the client sends on the redirected connection is never verified.
**Why:** a token observed on the wire can be replayed from the same source address within its lifetime.
**Cost:** compute the expected proof from the session key and seeds in `WorldSocket::HandleAuthSession` and compare; small.
**Risk:** low, but it is a behaviour change in the auth path, so it waits until phase 1 passes with faithful behaviour.

## 3. Zone-scoped spawning with a single ownership predicate

**What:** a player belongs on node N iff N owns the player's zone, or nobody claims that zone and N owns the map. Spawning on a zone node is limited to its zones.
**Why:** today a zone node must also own its zones' maps, so two nodes run overlapping spawns on the same map and both write `creature_respawn` for the same GUIDs.
**Cost:** touches `MapManager::CreateBaseMap`, grid loading, `IsMapLocal` call sites (login, dwell timer, far teleport), and respawn persistence. Medium.
**Risk:** medium; this is the one open design problem in the cluster code.

## 4. Split NatsBus

**What:** `NatsBus.cpp` (3,350 lines) into transport (connect, seal, publish, dispatch), routing (announce, status, dead-node), handoff (transfer, token, online/offline) and per-feature handlers (chat, group, LFG, BG, arena).
**Why:** phase 2 adds the feature handlers back; keeping them in one file makes every phase-2 change a conflict magnet and a review burden.
**Cost:** mechanical, but large; do it after phase 2 lands so the split is done once against the full surface.
**Risk:** low if done as a pure move with the dispatcher table unchanged.
