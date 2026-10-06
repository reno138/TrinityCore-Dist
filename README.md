# TrinityCore-Dist: a distributed World of Warcraft 3.3.5a server

TrinityCore-Dist is [TrinityCore 3.3.5](https://github.com/TrinityCore/TrinityCore)
with the distributed-server code from [c9core](https://github.com/reno138/c9core)
ported onto it. One realm runs across several machines. Each `worldserver`
process owns a set of maps or individual zones; when a player crosses into
territory owned by another process, the server hands the player off and tells
the client to reconnect there. The client sees an ordinary loading screen.

There is no proxy in the data path. Clients connect directly to the
worldserver that owns their location. The handoff uses the 3.3.5a client's own
`SMSG_CONNECT_TO` mechanism, so stock 3.3.5a (build 12340) clients work
unmodified. All processes share one set of databases, and coordinate over a
[NATS](https://nats.io/) message bus.

> **Not related to ToCloud9.** This project and c9core are independent of
> [ToCloud9](https://github.com/walkline/ToCloud9); none of its code is here.

> **Status: experimental, phase 1.** Map, teleport and zone handoffs work on a
> three-node lab. The cross-node features of c9core that are not in phase 1
> are listed under [What is not ported yet](#what-is-not-ported-yet). This has
> not run a public realm.

---

## How it works

```
                        authserver :3724
                              │
   WoW client ──────────────► node 1  :8085   maps 0, 530   (Eastern Kingdoms, Outland)
        │   SMSG_CONNECT_TO
        └─(reconnects)──────► node 2  :8085   maps 1, 571   (Kalimdor, Northrend)
                              node 3  :8085   zone 1637     (Orgrimmar)

   all nodes ◄──── NATS bus (HMAC-authenticated) ────► all nodes
   all nodes ◄──── one shared MySQL/MariaDB (auth, characters, world)
```

**A handoff, step by step:**

1. A player teleports, takes a boat or zeppelin across a map boundary, or
   stands for two seconds in a zone owned by another node.
2. The source node saves the character in one transaction. **Only from that
   transaction's commit callback** does it publish the transfer snapshot and a
   one-time redirect token to the destination node, so the destination can
   never load a stale row.
3. The source sends the client `SMSG_CONNECT_TO` with the destination address;
   the client reconnects there.
4. The destination waits for the token (up to 3 s), checks it, creates the
   session and loads the character from the database. The player arrives with
   no login screen, no login effect, and no second message of the day.

Every node announces which maps and zones it owns. All nodes keep the same
routing table, heartbeat each other, and drop a node that goes quiet until it
announces itself again; its maps are claimed by the lowest surviving node.

## Components

| Program | Role |
|---|---|
| `worldserver` | The TrinityCore game server with the distributed-server code. One per node. |
| `authserver` | Standard TrinityCore login server. One per realm. |
| `nats-server` | Message broker. **Not vendored here**: install the stock [nats-server](https://github.com/nats-io/nats-server) binary on one machine. |

The `nodemgr` supervisor and the `clustermgr` operator console from c9core are
not ported yet (phase 3).

---

## Setting it up

### 1. Requirements

- **Linux x86_64.** Developed on Ubuntu 26.04 with GCC 15. Everything
  TrinityCore 3.3.5 needs ([upstream requirements](https://trinitycore.info/en/install/requirements)):
  CMake ≥ 3.18, Boost ≥ 1.74, OpenSSL, readline, zlib, bzip2, and the
  **MariaDB or MySQL client library**.
- **A MySQL or MariaDB server** that every node can reach.
- **A stock `nats-server`** on one machine the nodes can reach.
- **A WoW 3.3.5a (12340) client.** You extract the map data from it yourself.

### 2. Build once and copy to the other nodes

```bash
git clone https://github.com/reno138/TrinityCore-Dist.git && cd TrinityCore-Dist
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DCMAKE_INSTALL_PREFIX=$HOME/tc-335 \
         -DTOOLS=1 -DSERVERS=1 -DSCRIPTS=static
make -j"$(nproc)" && make install
```

`dep/cnats` (the NATS C client) builds as part of the tree; nothing is fetched.
The binaries are static, so copy `~/tc-335/bin` to the other nodes as-is (same
OS release). When replacing a running binary, copy it to a new name and `mv`
it into place.

Extract `dbc`, `maps`, `vmaps` and `mmaps` from the client with the tools in
`~/tc-335/bin`, exactly as for TrinityCore (`mapextractor`, `vmap4extractor`
then `vmap4assembler`, `mmaps_generator`). Every node needs the same set.

`contrib/cluster/vm-sync-build.sh` is the helper used during development to
sync a checkout to a build host and build there; adapt or ignore it.

### 3. Databases

Create the `auth`, `characters` and `world` databases once (see the
[TrinityCore guide](https://trinitycore.info/en/install/Database-Installation));
import the TDB 335 full world dump. Every node points at the **same** three.
This fork adds one column, `characters.owning_node_id`, through the normal
`sql/updates` mechanism.

Let only **one** node apply database updates. On every other node set:

```ini
Updates.EnableDatabases = 0
```

### 4. The message bus

Start `nats-server` on one machine, listening on a private network.

Generate **one** shared key for the whole realm:

```bash
openssl rand -hex 32
```

Every message on the bus is authenticated with this key using HMAC-SHA256,
with a 30-second replay window. The bus refuses to start without a key of at
least 32 bytes. The HMAC proves who sent a message but doesn't encrypt it, so
keep port 4222 on a private network.

### 5. Configure each worldserver

In each node's `worldserver.conf`, the settings are under **CLUSTER SERVER**:

```ini
ClusterServer.NodeId      = 2                       # unique per node, 1-255; 0 = standalone TrinityCore
ClusterServer.AuthKey     = "<the shared key>"
ClusterServer.NatsURL     = "nats://192.0.2.20:4222"   # note the capital URL
ClusterServer.GameAddress = "192.0.2.21"            # this node's LAN IP
ClusterServer.GamePort    = 8085                    # = WorldServerPort
ClusterServer.Maps        = "1,571"                 # maps this node owns
```

Plus the usual TrinityCore settings: the three `*DatabaseInfo` strings,
`DataDir` (use an absolute path), and `WorldServerPort`.

**Splitting the world:**

| Setting | Use |
|---|---|
| `ClusterServer.Maps = "0,530"` | This node owns those continents. |
| `ClusterServer.Maps = ""` | Owns everything. Single node or development. |
| `ClusterServer.InstanceServer = 1` | Owns every instanceable map (dungeons, raids, BGs, arenas). Leave `Maps` empty. |
| `ClusterServer.Zones = "1637"` | A **zone node**: takes individual zones off a continent. A node that announces zones never claims a map, so put zones on their own node, and **also list those zones' maps in `Maps`** so it loads the terrain (see [Known limitations](#known-limitations)). |

**Grids.** A cluster node ignores `GridUnload`, `BaseMapLoadAllGrids` and
`InstanceMapLoadAllGrids` and always preloads every grid of its maps and never
unloads them. A cross-node arrival drops a player straight into a grid with no
approach, which on-demand loading does not handle well. Budget 2 to 3 GB of
RAM per continent.

**Players on the internet.** By default the client is redirected to
`GameAddress`, a LAN IP. Players outside your network need a reachable address:

```ini
ClusterServer.RedirectAddress = "203.0.113.10"   # public IP, or NAT address
ClusterServer.RedirectPort    = 8085             # external port, if it differs
```

Give each node its own external port and forward each one to that node.

### 6. Start order

NATS, then the node that applies database updates (it creates and updates the
schemas on first start), then authserver, then the other nodes. Point the realm
address in `auth.realmlist` at node 1. Nodes learn each other's routes from the
announces; a node started later learns them at the peers' next periodic
announce.

### Ports

| Port | Who needs it |
|---|---|
| 3724/tcp | authserver, for clients |
| 8085/tcp (per node) | worldservers, for clients |
| 4222/tcp | NATS. **Nodes only, never the internet.** |

---

## Things that will bite you

- **Config keys are case-sensitive, and unknown keys are ignored silently.**
  `ClusterServer.NatsURL` has a capital URL. A misspelled key gives no warning
  and the built-in default wins.
- **`ClusterServer.NodeId = 0` is a stock TrinityCore server.** The shipped
  `.conf.dist` sets 1, which makes a freshly installed worldserver a cluster
  node that dials the bus.
- **`HeartbeatInterval` and `NodeDeadTimeout` are milliseconds.** The conf
  text in c9core said seconds; the code never did.
- **A node that announces zones claims no maps.** With two nodes you cannot
  have a zone node; use three.
- **The client sends no `CMSG_SET_ACTIVE_MOVER` after a zone handoff.** TC
  needs it to accept movement; this fork sets the active mover on arrival.
  Keep that in mind when merging upstream movement changes.
- **Same-account sessions.** A node keeps a redirected-out session until the
  client drops the old connection. Another node claiming the character
  deactivates that stale copy; the order of that teardown matters to TC's
  `~Unit` assertions (see `World::Update`).

## Known limitations

- **Phase 1 only** (see below): no cross-node chat, group, LFG, battleground,
  arena, social, calendar or guild features yet. The bus message types for
  them exist and are ignored.
- **Zone nodes load whole continents.** A zone node must own its zones' maps
  for terrain, so two nodes run overlapping spawns on the same map and both
  write respawn data. The proper fix, zone-scoped spawning with a single
  ownership predicate, is on the register.
- **The destination never verifies the client's 20-byte redirect proof**;
  the one-time token plus the source-observed client IP is the credential.
- **Ghosts do not hand off**; a dead player is never transferred until
  resurrected.
- **The redirect is sent as soon as the save is dispatched**, and the
  destination waits up to 3 s for the token. Measured on the lab: commit
  4 to 89 ms, destination wait 0 to 59 ms. Moving the redirect into the commit
  callback is the first item on the register.
- **Only one client per account** is meaningful during a handoff.

## What is not ported yet

From c9core, in planned order:

- **Phase 2:** chat relay, mail notify, cross-node groups and party frames,
  LFG with one master node, battleground queue coordination, arena results,
  friend status, calendar, guild.
- **Phase 3:** `nodemgr` (per-node supervisor) and `clustermgr` (terminal and
  web console).

The full list of open items, with the AzerothCore-side bugs this port found
and the deviations from c9core, is in `docs/superpowers/improvements.md`. The
design spec, the implementation plan and the handoff test log are next to it.

## Relation to c9core and TrinityCore

c9core is the same design on AzerothCore. This port keeps the wire protocol,
the config keys and the behaviour, and follows TrinityCore's own login
behaviour where the two cores differ. TrinityCore's code, layout, macros and
logging are unchanged; the cluster code lives in `src/server/game/Server/`
(`NatsBus`, `ClusterMgr`, `ClientRedirect`, `PlayerTransfer`,
`PlayerStateSync`, `SharedPlayerCache`, `PacketTrace`) and
`src/server/shared/Cluster/`, plus hooks in the login, socket, session,
player, map, transport and world code.

Upstream TrinityCore history is carried in full, with one mechanical change:
GitHub rejects packs containing a handful of malformed author/committer
idents from 2008-era upstream commits, so those idents were rewritten (stray
`<` and `>` stripped). The commit SHAs here therefore differ from
`TrinityCore/TrinityCore`, and a newer `3.3.5` cannot be merged into this
history directly. New upstream work is merged into the private tracking clone
and rebased onto this `main`.

## License

GPL-2.0, like TrinityCore. See `COPYING`.
