# c9core cluster port to TrinityCore 3.3.5 — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two TrinityCore 3.3.5 worldservers on wow-node-01 and wow-node-02, built from one commit on branch `cluster`, hand off a stock 12340 client with talents and auras on the map, teleport and zone paths.

**Architecture:** Hunk transplant from c9core (AzerothCore fork) onto TC. The 20 new cluster files are copied and translated with a sed block plus a short list of manual edits; the ~25 hook sites are re-applied by hand at the equivalent TC function. Phase-2 bus handlers (chat, mail, group, LFG, BG, arena) are stubbed so the wire protocol stays stable. Cross-thread work from the NATS I/O thread reaches the world thread through a new `World::QueueCallback` queue, exactly as in AC.

**Tech Stack:** C++20, CMake ≥3.18, GCC ≥11.1 (Ubuntu 26.04 on wow-node-01), Boost ≥1.74, MariaDB client, OpenSSL 3, nats.c 3.8.0 (vendored static), NATS broker `c9-nats` already on wow-node-01:4222, TDB 335.26091.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-05-tc-cluster-port-phase1-design.md`. API map: `docs/superpowers/specs/2026-10-05-ac-to-tc-api-map.md`. Per-file hook analyses: `docs/superpowers/port/summaries/*.md`. AC diff hunks: `docs/superpowers/port/ac-hunks/*.diff`. AC new files: `docs/superpowers/port/ac-new/`.
- **No rebranding of TC.** Keep `TC_GAME_API`, `TC_LOG_*`, `Trinity::`, TC file layout, `"TC> "` CLI prefix. Never carry `C9_*`/`ACORE_*` renames.
- **Faithful port.** Behaviour as in c9core `main` f6d4a147f. Known gaps stay. Improvement ideas go to `docs/superpowers/improvements.md`, never into code.
- **Noise is never ported:** `GetRawValue()→GetCounter()` reverts, ToCloud9 removals, `AC_PLATFORM` renames, header/attribution edits, unrelated gameplay fixes. Each summary file classifies every hunk; port only `phase1`.
- **Phase 2 and 3 are out of scope:** no hooks in LFGMgr, BattleGroundHandler, GroupHandler, Group, MiscHandler, CalendarMgr, SocialMgr, Guild, Pet, ChatHandler, LFGHandler, `Player::Say/Yell/TextEmote`, the party-frame sync block (`m_clusterUnitUpdateTimer`), the cross-node group-invite drain in `World::Update`; no nodemgr, no clustermgr, no nats-server vendoring.
- Config keys keep c9core spelling, including case-sensitive `ClusterServer.NatsURL`.
- Edits happen on the laptop in `~/source/Trinitycore` (branch `cluster`), commits on the laptop. Builds happen only on wow-node-01 (`wow@192.0.2.10.20`) in `~/source/c9core-tc`, synced by rsync (Task 1). Build logs go to `~/source/c9core-tc/build/build.log` and are grepped, never pasted whole.
- Commit message trailer: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- TC and AC worldservers must never be on the NATS bus at the same time. AC services (`~/335`) on both VMs stay stopped during all TC work.
- Node MTU on routed VLANs stays 1500.

---

## File map (what gets created or changed)

| Path (TC tree) | Role | Task |
| --- | --- | --- |
| `contrib/cluster/vm-sync-build.sh` | rsync laptop tree → VM, run cmake+ninja/make, capture log | 1 |
| `dep/cnats/**` (copied), `dep/CMakeLists.txt`, `dep/PackageList.txt` | nats.c static lib | 2 |
| `src/server/shared/Cluster/{ClusterAuth.h,ClusterAuth.cpp,PskCrypt.h,PskCrypt.cpp,ClusterMgmtProtocol.h}` | HMAC sealing, PSK crypt, mgmt protocol | 3 |
| `tests/common/ClusterAuth.cpp` | Catch2 seal/open round-trip | 3 |
| `sql/updates/characters/3.3.5/2026_10_05_00_characters.sql`, `src/server/database/Database/Implementation/CharacterDatabase.{h,cpp}` | `owning_node_id`, node-scoped statements | 4 |
| `src/server/game/World/World.{h,cpp}` | callback queue, deactivation queue, `WUPDATE_CLUSTER` | 5 |
| `src/server/game/Server/WorldSession.{h,cpp}` | redirect state + accessors | 5 |
| `src/server/game/Maps/Map.h`, `src/server/game/Entities/Transport/Transport.{h,cpp}`, `src/server/game/Entities/Player/Player.h`, `src/server/game/Spells/SpellHistory.h` | ghost flag, `InitializeToTime`, snapshot accessors, cluster fields | 5 |
| `src/server/game/Server/{NatsBus,ClusterMgr,ClientRedirect,PlayerTransfer,PlayerStateSync,SharedPlayerCache,PacketTrace}.{h,cpp}`, `SharedPlayerState.h` | cluster core | 6 |
| `src/server/game/Server/WorldSocket.{h,cpp}` | redirect auth path | 7 |
| `src/server/game/Server/WorldSession.cpp` | redirect-out behaviour, auto-login | 7 |
| `src/server/game/Handlers/CharacterHandler.cpp` | login routing, arrival | 8 |
| `src/server/game/Entities/Player/Player.cpp`, `src/server/game/Entities/Object/Object.cpp` | dwell timer, teleport handoff, state sync, destroy suppression | 9 |
| `src/server/game/Maps/{MapManager.cpp,MapManager.h,Map.cpp,TransportMgr.cpp,TransportMgr.h}`, `src/server/game/Entities/Transport/Transport.cpp` | ghost map, transport sync | 10 |
| `src/server/worldserver/Main.cpp`, `src/server/worldserver/CommandLine/CliRunnable.cpp`, `src/server/worldserver/worldserver.conf.dist` | bootstrap, CLI, conf block | 11 |

---

### Task 1: Build host online and baseline TC build

**Files:**
- Create: `contrib/cluster/vm-sync-build.sh`
- Create: `contrib/cluster/README.md`

**Interfaces:**
- Produces: `contrib/cluster/vm-sync-build.sh [configure|build|install]` used by every later task's "build" step. Build log at `~/source/c9core-tc/build/build.log` on the VM.

- [ ] **Step 1: Start the two VMs**

```bash
ssh root@203.0.113.3 'qm start 170; qm start 171; sleep 45; qm status 170; qm status 171'
```
Expected: both `status: running`.

- [ ] **Step 2: Confirm SSH and that the AC realm is stopped on both nodes**

```bash
for h in 192.0.2.20 192.0.2.21; do ssh reno@$h 'hostname; systemctl is-active c9-nodemgr c9-authserver 2>/dev/null; pgrep -a worldserver || echo no-worldserver'; done
ssh wow@192.0.2.10.20 'systemctl is-active c9-nats'
```
Expected: no `worldserver` process on either node; `c9-nats` is `active` (the broker stays up, it is reused). If `c9-nodemgr`/`c9-authserver` are active, stop them: `sudo systemctl stop c9-nodemgr c9-authserver` on both nodes.

- [ ] **Step 3: Verify the toolchain on wow-node-01**

```bash
ssh wow@192.0.2.10.20 'gcc --version | head -1; cmake --version | head -1; dpkg -l libboost-dev libboost-filesystem-dev libboost-locale-dev libboost-program-options-dev libboost-regex-dev libboost-thread-dev libssl-dev libreadline-dev zlib1g-dev libbz2-dev libmariadb-dev libmariadb-dev-compat p7zip-full 2>/dev/null | grep -E "^(ii|un)" | awk "{print \$1, \$2}"'
```
Expected: GCC ≥ 11.1, CMake ≥ 3.18, every package `ii`. Install any `un` with `sudo apt-get install -y <pkg>`. Note: TC builds against the MariaDB client exactly like AC did; do not install `libmysqlclient-dev`.

- [ ] **Step 4: Write the sync-and-build script**

```bash
#!/usr/bin/env bash
# contrib/cluster/vm-sync-build.sh — rsync this tree to the build VM and build there.
# Usage: contrib/cluster/vm-sync-build.sh configure|build|install|all
set -euo pipefail
VM="${VM:-wow@192.0.2.10.20}"
REMOTE_SRC="${REMOTE_SRC:-/home/wow/source/c9core-tc}"
PREFIX="${PREFIX:-/home/wow/tc-335}"
JOBS="${JOBS:-8}"
LOCAL_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-all}"

sync() {
  rsync -az --delete \
    --exclude '.git/' --exclude 'build/' --exclude '*.o' \
    "$LOCAL_SRC/" "$VM:$REMOTE_SRC/"
}
configure() {
  ssh "$VM" "mkdir -p $REMOTE_SRC/build && cd $REMOTE_SRC/build && \
    cmake .. -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTOOLS=1 -DSERVERS=1 -DSCRIPTS=static -DWITH_WARNINGS=0 -DBUILD_TESTING=1 \
      > configure.log 2>&1; tail -3 configure.log"
}
build() {
  ssh "$VM" "cd $REMOTE_SRC/build && (make -j$JOBS > build.log 2>&1; echo EXIT=\$? >> build.log); \
    grep -cE ' error:|Error [0-9]+' build.log | sed 's/^/errors: /'; tail -1 build.log"
}
install() {
  ssh "$VM" "cd $REMOTE_SRC/build && make install > install.log 2>&1; tail -1 install.log"
}
case "$MODE" in
  configure) sync; configure ;;
  build)     sync; build ;;
  install)   install ;;
  all)       sync; configure; build; install ;;
  *) echo "usage: $0 configure|build|install|all" >&2; exit 2 ;;
esac
```

Make it executable: `chmod +x contrib/cluster/vm-sync-build.sh`.

- [ ] **Step 5: Write `contrib/cluster/README.md`**

```markdown
# Cluster port helpers

`vm-sync-build.sh` rsyncs this checkout to the build VM (`VM`, default `wow@192.0.2.10.20`)
into `~/source/c9core-tc`, configures with the standard phase-1 flags, builds with
`make -j8`, and installs to `~/tc-335`. Output is written to `build/configure.log`,
`build/build.log`, `build/install.log` on the VM; the script prints only the error count
and the last line. Inspect failures with:

    ssh wow@192.0.2.10.20 "grep -n -B2 -A6 ' error:' ~/source/c9core-tc/build/build.log | head -80"
```

- [ ] **Step 6: Baseline build of unmodified TC (proves the toolchain before any port work)**

```bash
contrib/cluster/vm-sync-build.sh all
```
Expected: `errors: 0`, `EXIT=0`, install line ends normally. Takes 30–60 min on 8 vCPU. If it fails, fix the toolchain, not the source; this is upstream TC at 092eb27b20.

- [ ] **Step 7: Run TC's own unit tests on the VM as a second baseline**

```bash
ssh wow@192.0.2.10.20 'cd ~/source/c9core-tc/build && ./tests/tests 2>&1 | tail -3'
```
Expected: `All tests passed`.

- [ ] **Step 8: Commit**

```bash
git add contrib/cluster
git commit -m "build(cluster): VM sync-and-build helper for the phase 1 port

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Dependency slab — vendor nats.c as `nats_static`

**Files:**
- Create: `dep/cnats/**` (copy of `/home/wow/src/c9core-pub/deps/cnats`, minus `test/` and `examples/`)
- Modify: `dep/CMakeLists.txt` (after line 34, the `endif()` of `if(SERVERS)`)
- Modify: `dep/PackageList.txt`
- Modify: `src/server/game/CMakeLists.txt:49-53`

**Interfaces:**
- Produces: CMake target `nats_static`, linked PRIVATE into `game`. `<nats/nats.h>` is only included from `NatsBus.cpp` (Task 6).

- [ ] **Step 1: Copy the vendored client**

```bash
rsync -a --exclude test/ --exclude examples/ --exclude '.travis.yml' \
  /home/wow/src/c9core-pub/deps/cnats/ dep/cnats/
ls dep/cnats && grep -m1 NATS_VERSION_STRING dep/cnats/src/version.h
```
Expected: `CMakeLists.txt LICENSE README.md src ...` and `"3.8.0"`.

- [ ] **Step 2: Wire it into `dep/CMakeLists.txt`**

Insert after the existing `if(SERVERS) ... endif()` block (line 34), before `if(SERVERS AND BUILD_EFSW)`:

```cmake
if(SERVERS)
  # NATS C client (cluster control-channel transport), vendored at dep/cnats (nats.c 3.8.0).
  # Static only, no TLS/streaming/examples; EXCLUDE_FROM_ALL keeps its install() rules out
  # of ${CMAKE_INSTALL_PREFIX}. BUILD_TESTING is saved/restored as a directory-scoped
  # variable so cnats' enable_testing() cannot switch off TrinityCore's own Catch2 suite.
  set(NATS_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_STATIC ON  CACHE BOOL "" FORCE)
  set(NATS_BUILD_STREAMING  OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_WITH_TLS   OFF CACHE BOOL "" FORCE)
  set(_tc_saved_build_testing "${BUILD_TESTING}")
  set(BUILD_TESTING OFF)
  add_subdirectory(cnats EXCLUDE_FROM_ALL)
  set(BUILD_TESTING "${_tc_saved_build_testing}")
  if(TARGET nats_static)
    # nats.c forces -std=c99 but uses C11 _Generic; the later flag wins.
    if(NOT MSVC)
      target_compile_options(nats_static PRIVATE -std=c11)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
      target_compile_options(nats_static PRIVATE -Wno-incompatible-pointer-types-discards-qualifiers)
    endif()
    set_target_properties(nats_static PROPERTIES FOLDER "dep")
  endif()
endif()
```

- [ ] **Step 3: Add the package entry to `dep/PackageList.txt`** (append, matching the file's existing format)

```
cnats (NATS C client, cluster control-channel transport)
  https://github.com/nats-io/nats.c
  Version: v3.8.0
```

- [ ] **Step 4: Link `nats_static` into `game`** — `src/server/game/CMakeLists.txt`:

```cmake
target_link_libraries(game
  PRIVATE
    trinity-core-interface
    nats_static
  PUBLIC
    game-interface)
```

- [ ] **Step 5: Configure and build on the VM**

```bash
contrib/cluster/vm-sync-build.sh configure && contrib/cluster/vm-sync-build.sh build
```
Expected: configure log contains no `CMake Error`; `errors: 0`. Confirm the lib was built: `ssh wow@192.0.2.10.20 'ls ~/source/c9core-tc/build/dep/cnats/src/libnats_static.a'`.

- [ ] **Step 6: Commit**

```bash
git add dep/cnats dep/CMakeLists.txt dep/PackageList.txt src/server/game/CMakeLists.txt
git commit -m "build(cluster): vendor nats.c 3.8.0 as nats_static and link it into game

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Shared cluster slab — ClusterAuth, PskCrypt, mgmt protocol, with a unit test

**Files:**
- Create: `src/server/shared/Cluster/ClusterAuth.h`, `ClusterAuth.cpp`, `PskCrypt.h`, `PskCrypt.cpp`, `ClusterMgmtProtocol.h` (from `docs/superpowers/port/ac-new/src/server/shared/Cluster/`)
- Create: `tests/common/ClusterAuth.cpp`
- Modify: `tests/CMakeLists.txt` only if it lists sources explicitly (check; TC's tests dir uses `CollectSourceFiles`, so probably nothing)

**Interfaces:**
- Produces: `ClusterAuth::Init(std::string const& key)`, `ClusterAuth::IsInitialised()`, `ClusterAuth::Seal(...)`, `ClusterAuth::Open(...)` (exact signatures in `ClusterAuth.h:363-383` of the copied file) and `class PskCrypt`. These are consumed by `NatsBus` (Task 6).

- [ ] **Step 1: Copy and apply the mechanical renames**

```bash
mkdir -p src/server/shared/Cluster
cp docs/superpowers/port/ac-new/src/server/shared/Cluster/* src/server/shared/Cluster/
sed -i -E 's/\bLOG_(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\(/TC_LOG_\1(/g' src/server/shared/Cluster/*.cpp src/server/shared/Cluster/*.h
grep -nE 'LOG_|Acore|sConfigMgr|AC_|C9_' src/server/shared/Cluster/* || echo clean
```
Expected: `clean` (the API map says these five files need nothing beyond the LOG rename).

- [ ] **Step 2: Export the symbols for shared-lib builds**

In `ClusterAuth.h`, prefix each of the four free functions in the `ClusterAuth` namespace (`Init`, `IsInitialised`, `Seal`, `Open`) with `TC_SHARED_API`, and in `PskCrypt.h` change `class PskCrypt` to `class TC_SHARED_API PskCrypt`. Add `#include "Define.h"` to both headers if not present (it is already included; verify with grep).

- [ ] **Step 3: Write the failing Catch2 test** — `tests/common/ClusterAuth.cpp`

Signatures (from `ClusterAuth.h:89-109`): `bool Init(std::string const& sharedKey)`, `bool IsInitialised()`, `std::vector<uint8> Seal(uint8 srcNodeId, uint8 msgType, uint8 const* payload, std::size_t payloadLen)`, `bool Open(uint8 const* frame, std::size_t frameLen, uint8& outSrcNodeId, uint8& outMsgType, std::vector<uint8>& outPayload)`.

```cpp
#include "tc_catch2.h"
#include "ClusterAuth.h"
#include <string>
#include <vector>

TEST_CASE("ClusterAuth seal/open round trip", "[ClusterAuth]")
{
    std::string const key(32, 'k');
    REQUIRE(ClusterAuth::Init(key));
    REQUIRE(ClusterAuth::IsInitialised());

    std::vector<uint8> const payload = { 1, 2, 3, 4, 5 };
    std::vector<uint8> frame = ClusterAuth::Seal(3, 0x29, payload.data(), payload.size());
    REQUIRE(frame.size() > payload.size());

    uint8 srcNode = 0, msgType = 0;
    std::vector<uint8> opened;
    REQUIRE(ClusterAuth::Open(frame.data(), frame.size(), srcNode, msgType, opened));
    REQUIRE(srcNode == 3);
    REQUIRE(msgType == 0x29);
    REQUIRE(opened == payload);

    SECTION("replay of the same frame is rejected")
    {
        std::vector<uint8> again;
        REQUIRE_FALSE(ClusterAuth::Open(frame.data(), frame.size(), srcNode, msgType, again));
    }

    SECTION("tampered frame is rejected")
    {
        std::vector<uint8> bad = ClusterAuth::Seal(3, 0x29, payload.data(), payload.size());
        bad.back() ^= 0xFF;
        std::vector<uint8> out;
        REQUIRE_FALSE(ClusterAuth::Open(bad.data(), bad.size(), srcNode, msgType, out));
    }
}

TEST_CASE("ClusterAuth refuses a short key", "[ClusterAuth]")
{
    REQUIRE_FALSE(ClusterAuth::Init("short"));
}
```
If the replay section fails because `Open` keys replay detection on something other than the whole frame (read the implementation), drop that section rather than weaken the assertion.

- [ ] **Step 4: Build and run the test**

The implementation was copied in Step 1, so this run should pass on the first try; the test is the regression gate for every later edit to these files (and proves `shared` links OpenSSL transitively). Run:

```bash
contrib/cluster/vm-sync-build.sh build && ssh wow@192.0.2.10.20 'cd ~/source/c9core-tc/build && ./tests/tests "[ClusterAuth]" 2>&1 | tail -3'
```
Expected: `errors: 0` and `All tests passed`. If `tests` links `shared` but `Cluster/` symbols are missing, confirm `tests/CMakeLists.txt` links `shared` (it links `game` which pulls `shared`).

- [ ] **Step 5: Commit**

```bash
git add src/server/shared/Cluster tests/common/ClusterAuth.cpp
git commit -m "feat(cluster): shared ClusterAuth/PskCrypt/mgmt protocol with seal/open test

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Database slab — `owning_node_id` column and node-scoped statements

**Files:**
- Create: `sql/updates/characters/3.3.5/2026_10_05_00_characters.sql`
- Modify: `src/server/database/Database/Implementation/CharacterDatabase.h` (enum, near line 286/314)
- Modify: `src/server/database/Database/Implementation/CharacterDatabase.cpp` (lines 360, 389, 402)

**Interfaces:**
- Produces: `CHAR_UPD_CHAR_ONLINE` now takes `(uint8 nodeId, uint32 guid)`; `CHAR_UPD_ACCOUNT_ONLINE` unchanged signature; new `CHAR_UPD_NODE_ONLINE_CLEANUP(uint8 nodeId)`; `CHAR_UPD_CHARACTER_POSITION` is `CONNECTION_BOTH` and zeroes `instance_id`. Consumers: CharacterHandler (Task 8), Player::TeleportTo (Task 9), NatsBus dead-node handler (Task 6), Main.cpp (Task 11).

- [ ] **Step 1: SQL update file**

```sql
-- c9core cluster port: track which node owns each online character.
-- Set to ClusterServer.NodeId on login, cleared to 0 on logout; used for node-scoped
-- crash recovery and by the dead-node handler.
ALTER TABLE `characters`
    ADD COLUMN `owning_node_id` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `online`,
    ADD INDEX `idx_owning_node_id` (`owning_node_id`);
```
(The `transguid` signedness fix from c9core is not needed: TC's `characters.transguid` is already `INT UNSIGNED`; verify with `grep -n transguid sql/base/characters_database.sql`. If it is signed, add `ALTER TABLE characters MODIFY COLUMN transguid INT UNSIGNED NOT NULL DEFAULT 0;`.)

- [ ] **Step 2: Enum** — `CharacterDatabase.h`, add after `CHAR_UPD_ACCOUNT_ONLINE,`:

```cpp
    CHAR_UPD_NODE_ONLINE_CLEANUP,
```

- [ ] **Step 3: Statements** — `CharacterDatabase.cpp`:

```cpp
PrepareStatement(CHAR_UPD_ACCOUNT_ONLINE, "UPDATE characters SET online = 0, owning_node_id = 0 WHERE account = ?", CONNECTION_ASYNC);
PrepareStatement(CHAR_UPD_NODE_ONLINE_CLEANUP, "UPDATE characters SET online = 0, owning_node_id = 0 WHERE owning_node_id = ?", CONNECTION_ASYNC);
PrepareStatement(CHAR_UPD_CHAR_ONLINE, "UPDATE characters SET online = 1, owning_node_id = ? WHERE guid = ?", CONNECTION_ASYNC);
PrepareStatement(CHAR_UPD_CHARACTER_POSITION, "UPDATE characters SET position_x = ?, position_y = ?, position_z = ?, orientation = ?, map = ?, zone = ?, trans_x = 0, trans_y = 0, trans_z = 0, transguid = 0, taxi_path = '', cinematic = 1, instance_id = 0 WHERE guid = ?", CONNECTION_BOTH);
```
Replace the existing three lines in place; add the cleanup line next to `CHAR_UPD_ACCOUNT_ONLINE`.

- [ ] **Step 4: Fix the one existing caller of `CHAR_UPD_CHAR_ONLINE`** — `src/server/game/Handlers/CharacterHandler.cpp:830-832` currently binds only the guid. Change to:

```cpp
CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_ONLINE);
stmt->setUInt8(0, 0); // owning node id; Task 8 replaces the 0 with sClusterMgr.GetNodeId()
stmt->setUInt32(1, pCurrChar->GetGUID().GetCounter());
CharacterDatabase.Execute(stmt);
```
Grep for any other caller: `grep -rn CHAR_UPD_CHAR_ONLINE src/` — there must be exactly one.

- [ ] **Step 5: Build**

```bash
contrib/cluster/vm-sync-build.sh build
```
Expected: `errors: 0`.

- [ ] **Step 6: Commit**

```bash
git add sql/updates/characters/3.3.5/2026_10_05_00_characters.sql src/server/database src/server/game/Handlers/CharacterHandler.cpp
git commit -m "feat(cluster): characters.owning_node_id and node-scoped online statements

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: TC extension points the core needs (no cluster code yet)

Everything the core and hooks call on TC classes that TC does not have. All additions are inert without the core, so this task compiles cleanly on its own.

**Files:**
- Modify: `src/server/game/World/World.h` (includes :33-36; `enum WorldTimers` :69-86; public API :751-752; private :851-852)
- Modify: `src/server/game/World/World.cpp` (`ProcessPendingCallbacks` after `ShutdownCancel`; `QueuePlayerDeactivation` after `LoadDBVersion`)
- Modify: `src/server/game/Server/WorldSession.h` (public API after `GetWarden()` :578; `IsLegitCharacterForAccount` :1301; private members :1316, :1332, :1343-1351)
- Modify: `src/server/game/Server/WorldSession.cpp:131` (ctor init)
- Modify: `src/server/game/Maps/Map.h` (accessor near :468, member near :880)
- Modify: `src/server/game/Entities/Transport/Transport.h:84,102`, `Transport.cpp` (after `MoveToNextWaypoint`, :582)
- Modify: `src/server/game/Entities/Player/Player.h` (:1393, :1565, :2428-2433)
- Modify: `src/server/game/Spells/SpellHistory.h` (near :141)

**Interfaces:**
- Produces (World): `void QueueCallback(std::function<void()>)`, `void ProcessPendingCallbacks()`, `void QueuePlayerDeactivation(uint64 guid)`, timer `WUPDATE_CLUSTER`, members `_callbackQueue`, `_deactivateQueueMutex`, `_playerDeactivateQueue`.
- Produces (WorldSession): `SetSessionKey/GetSessionKey`, `SetAuthSeed/GetAuthSeed`, `SetRedirectAutoLoginGuid/GetRedirectAutoLoginGuid`, `SetRedirectedOut/IsRedirectedOut`, `SetRedirectPending/IsRedirectPending`, `AddLegitCharacter(ObjectGuid)`, `GetSecureAddons() const` / `SetSecureAddons(std::vector<SecureAddonInfo>)`, `struct PendingTransportAttach` + `SetPendingTransportAttach/HasPendingTransportAttach/GetPendingTransportAttach/ClearPendingTransportAttach`, `TransferPetInfo _pendingPetTransfer` (held via `std::unique_ptr` to avoid including PlayerTransfer.h here; declare `struct TransferPetInfo;`).
- Produces (Map): `IsGhostMap()`, `SetGhostMap()`. (Transport): `InitializeToTime(uint32)`, `GetPathProgress()`, `SetPathProgress(uint32)`. (Player): `QuestStatusMap const& getQuestStatusMap() const`, `SkillStatusMap const& GetSkillStatusMap() const`, `void UpdateClusterZoneRouting(uint32)`, `void BroadcastClusterStateIfDirty(uint32)`, `void MarkClusterStateDirty(uint8)`, and the cluster timer fields. (SpellHistory): `CooldownStorageType const& GetCooldowns() const`.

- [ ] **Step 1: World.h**

Includes: add `#include <functional>`, `#include <mutex>`, `#include <vector>`.
`enum WorldTimers`: add `WUPDATE_CLUSTER,` immediately before `WUPDATE_COUNT`.
Public, next to `QueueCliCommand`:
```cpp
void ProcessPendingCallbacks();
void QueueCallback(std::function<void()> cb) { _callbackQueue.add(std::move(cb)); }
void QueuePlayerDeactivation(uint64 guid);
```
Private, after `LockedQueue<CliCommandHolder*> cliCmdQueue;`:
```cpp
// Closures posted from NATS I/O threads, drained on the world thread (cluster port)
LockedQueue<std::function<void()>> _callbackQueue;
std::mutex _deactivateQueueMutex;
std::vector<uint64> _playerDeactivateQueue;
```

- [ ] **Step 2: World.cpp** — after `World::ShutdownCancel()`:

```cpp
void World::ProcessPendingCallbacks()
{
    std::function<void()> cb;
    while (_callbackQueue.next(cb))
        cb();
}
```
After `World::LoadDBVersion()`:
```cpp
void World::QueuePlayerDeactivation(uint64 guid)
{
    std::lock_guard<std::mutex> lock(_deactivateQueueMutex);
    _playerDeactivateQueue.push_back(guid);
}
```
In `SetInitialWorldSettings()`, next to `m_timers[WUPDATE_WHO_LIST].SetInterval(...)`: `m_timers[WUPDATE_CLUSTER].SetInterval(5 * IN_MILLISECONDS);`. The drain sites are added in Task 11 (they need `sNatsBus`).

- [ ] **Step 3: WorldSession.h**

Forward decl at top (after includes): `struct TransferPetInfo;`.
Hoist `struct Addons` (private, :1343-1349) to the public section so `WorldSession::Addons` is nameable; keep `_addons` private.
Public, after `Warden const* GetWarden() const`:
```cpp
// ---- cluster port: redirect / handoff state ----
void SetSessionKey(SessionKey const& k) { _clusterSessionKey = k; }
SessionKey const& GetSessionKey() const { return _clusterSessionKey; }
void SetAuthSeed(std::array<uint8, 4> const& s) { _clusterAuthSeed = s; }
std::array<uint8, 4> const& GetAuthSeed() const { return _clusterAuthSeed; }
void SetRedirectAutoLoginGuid(uint64 g) { _redirectAutoLoginGuid = g; }
uint64 GetRedirectAutoLoginGuid() const { return _redirectAutoLoginGuid; }
void SetRedirectedOut() { _redirectedOut = true; }
bool IsRedirectedOut() const { return _redirectedOut; }
void SetRedirectPending() { m_redirectPending = true; }
bool IsRedirectPending() const { return m_redirectPending; }
void AddLegitCharacter(ObjectGuid guid) { _legitCharacters.insert(guid); }
std::vector<SecureAddonInfo> const& GetSecureAddons() const { return _addons.SecureAddons; }
void SetSecureAddons(std::vector<SecureAddonInfo> addons) { _addons.SecureAddons = std::move(addons); }

// Deferred transport reattach (AC WorldSession.h, same field names; "pending" == entry != 0)
struct PendingTransportAttach
{
    uint32 entry   = 0;
    uint32 mapId   = 0;
    float  offsetX = 0.f;
    float  offsetY = 0.f;
    float  offsetZ = 0.f;
    float  offsetO = 0.f;
};
bool HasPendingTransportAttach() const { return _pendingTransportAttach.entry != 0; }
PendingTransportAttach const& GetPendingTransportAttach() const { return _pendingTransportAttach; }
void SetPendingTransportAttach(PendingTransportAttach const& pa) { _pendingTransportAttach = pa; }
void ClearPendingTransportAttach() { _pendingTransportAttach = {}; }
// Deferred pet transfer: AC holds TransferPetInfo by value; held by pointer here because
// PlayerTransfer.h arrives in Task 6. Phase 1 only sets it (Task 8); Pet.cpp consumes it in phase 2.
TransferPetInfo const* GetPendingPetTransfer() const { return _pendingPetTransfer.get(); }
void SetPendingPetTransfer(std::unique_ptr<TransferPetInfo> p) { _pendingPetTransfer = std::move(p); }
void ClearPendingPetTransfer() { _pendingPetTransfer.reset(); }
```
Private members (after `std::string m_Address;`):
```cpp
SessionKey _clusterSessionKey{};
std::array<uint8, 4> _clusterAuthSeed{};
uint64 _redirectAutoLoginGuid = 0;
bool _redirectedOut = false;
bool m_redirectPending = false;
PendingTransportAttach _pendingTransportAttach{};
std::unique_ptr<TransferPetInfo> _pendingPetTransfer;
```
`std::unique_ptr<TransferPetInfo>` with an incomplete type needs `~WorldSession()` defined in the .cpp (it is, `WorldSession.cpp:162`).

- [ ] **Step 4: WorldSession.cpp** — nothing to add to the init list (members use default initialisers). Confirm the dtor is out-of-line (`WorldSession::~WorldSession()` at :162).

- [ ] **Step 5: Map.h**

Public near `GetPlayers()`:
```cpp
bool IsGhostMap() const { return _ghostMap; }
void SetGhostMap() { _ghostMap = true; }
```
Private near `_updateObjects`: `bool _ghostMap = false;`

- [ ] **Step 6: Transport.h / Transport.cpp**

Public after `uint32 GetTimer() const { ... }` (:84):
```cpp
uint32 GetPathProgress() const { return GetGOValue()->Transport.PathProgress; }
void SetPathProgress(uint32 v) { m_goValue.Transport.PathProgress = v; }
```
Public after `GetDebugInfo()` (:102): `void InitializeToTime(uint32 timer);`
Transport.cpp after `MoveToNextWaypoint()` (:582): port AC's body from `ac-hunks/src_server_game_Entities_Transport_Transport.cpp.diff` lines 128-180 with `MotionTransport::` → `Transport::` and `SetPathProgress(timer)` kept (now exists). The body walks `_transportInfo->keyFrames` to the frame owning `timer % pathTime`, sets `_currentFrame/_nextFrame`, `SetMoving(...)`, `_triggeredArrivalEvent = _triggeredDepartureEvent = true`, then `SetPathProgress(timer)`.

- [ ] **Step 7: Player.h**

Include: `#include "SharedPlayerState.h"` cannot be added yet (file arrives in Task 6). Instead forward-define the mask type here as `uint8` and keep the enum in `SharedPlayerState.h`; `MarkClusterStateDirty(uint8 fields)` takes the raw mask.
Public next to `getQuestStatusMap()`: `QuestStatusMap const& getQuestStatusMap() const { return m_QuestStatus; }`
Public near `GetSkillRankByPos`: `SkillStatusMap const& GetSkillStatusMap() const { return mSkillStatus; }`
Public after `void UpdateZone(uint32 newZone, uint32 newArea);`:
```cpp
void UpdateClusterZoneRouting(uint32 zoneId);
void BroadcastClusterStateIfDirty(uint32 diff);
void MarkClusterStateDirty(uint8 fields) { m_clusterDirtyFields |= fields; }
```
Protected after `uint32 m_zoneUpdateTimer;` — copy the field block verbatim from `ac-hunks/src_server_game_Entities_Player_Player.h.diff` lines 33-72 **minus** `m_clusterUnitUpdateTimer` (phase 2). Fields: `m_pendingZoneReroute`, `m_zoneTransferDwellZone`, `m_zoneTransferDwellTimer`, `m_zoneTransferCooldown{ZONE_TRANSFER_COOLDOWN_MS}`, `ZONE_TRANSFER_DWELL_MS=2000`, `ZONE_TRANSFER_COOLDOWN_MS=5000`, `m_transportReattachTimer`, `m_clusterFullRefreshTimer`, `CLUSTER_FULL_REFRESH_MS=120000`, `m_clusterStateBroadcastTimer`, `m_clusterDirtyFields`, `m_clusterLastHealth`, `m_clusterLastPower`, `m_clusterLastCombat`, `m_clusterLastDead`, `CLUSTER_STATE_BROADCAST_INTERVAL=100`.
The two new member functions get empty bodies in Player.cpp for now (`void Player::UpdateClusterZoneRouting(uint32) {}` etc.); Task 9 fills them.

- [ ] **Step 8: SpellHistory.h** — public near the size accessor (:141):

```cpp
CooldownStorageType const& GetCooldowns() const { return _spellCooldowns; }
```

- [ ] **Step 9: Build and run tests**

```bash
contrib/cluster/vm-sync-build.sh build && ssh wow@192.0.2.10.20 'cd ~/source/c9core-tc/build && ./tests/tests 2>&1 | tail -1'
```
Expected: `errors: 0`, `All tests passed`.

- [ ] **Step 10: Commit**

```bash
git add src/server/game/World src/server/game/Server/WorldSession.h src/server/game/Server/WorldSession.cpp src/server/game/Maps/Map.h src/server/game/Entities/Transport src/server/game/Entities/Player/Player.h src/server/game/Entities/Player/Player.cpp src/server/game/Spells/SpellHistory.h
git commit -m "feat(cluster): TC extension points for the cluster core (callback queue, session redirect state, ghost map, transport time, snapshot accessors)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Core slab — NatsBus, ClusterMgr, ClientRedirect, PlayerTransfer, PlayerStateSync, SharedPlayerCache, PacketTrace

**Files:**
- Create: `src/server/game/Server/{NatsBus,ClusterMgr,ClientRedirect,PlayerTransfer,PlayerStateSync,SharedPlayerCache,PacketTrace}.{h,cpp}`, `SharedPlayerState.h` (from `docs/superpowers/port/ac-new/src/server/game/Server/`)

**Interfaces:**
- Consumes: everything from Task 5; `ClusterAuth` (Task 3); `CHAR_UPD_NODE_ONLINE_CLEANUP` (Task 4); `nats_static` (Task 2).
- Produces: `sNatsBus`, `sClusterMgr`, `sSharedPlayerCache`, `ClientRedirect::{GenerateToken,RedirectClient,SuspendClient}`, `PlayerTransfer`/`TransferPetInfo`, `StateFieldMask`. Public method names are unchanged from AC (`NatsBus.h` lists them); hooks in Tasks 7-11 call them by those names.

- [ ] **Step 1: Copy and run the sed block**

```bash
cp docs/superpowers/port/ac-new/src/server/game/Server/* src/server/game/Server/
```
Then run the sed block from `docs/superpowers/specs/2026-10-05-ac-to-tc-api-map.md` §A with `D=src/server/game/Server` (edit the `D=` line; the block was written for the `ac-new` copy, the patterns are the same). Afterwards:
```bash
grep -nE '\bLOG_(INFO|ERROR|DEBUG|WARN|TRACE|FATAL)\(|Acore::|sWorldSessionMgr|GetOption<|SMSG_REDIRECT_CLIENT|MotionTransport|getPowerType|->getClass|AddonMgr.h|WorldSessionMgr.h' src/server/game/Server/{NatsBus,ClusterMgr,ClientRedirect,PlayerTransfer,PlayerStateSync,SharedPlayerCache,PacketTrace}.* src/server/game/Server/SharedPlayerState.h || echo sed-clean
```
Expected: `sed-clean`.

- [ ] **Step 2: Addon info retype (API map §B.18)**

In `ClusterMgr.h` add before `struct PendingRedirect`:
```cpp
struct ClusterAddonInfo
{
    std::string Name;
    uint8 Enabled = 0;
    uint32 CRC = 0;
    uint8 State = 0;
    bool UsePublicKeyOrCRC = false;
    ClusterAddonInfo() = default;
    ClusterAddonInfo(std::string n, uint8 e, uint32 c, uint8 s, bool k)
        : Name(std::move(n)), Enabled(e), CRC(c), State(s), UsePublicKeyOrCRC(k) { }
};
```
Then `sed -i 's/\bAddonInfo\b/ClusterAddonInfo/g' src/server/game/Server/ClusterMgr.h src/server/game/Server/NatsBus.h src/server/game/Server/NatsBus.cpp` and verify the `emplace_back(name, enabled, crc, state, usePK)` at the former `NatsBus.cpp:927` and the field reads near `:1123-1133` still compile against this struct.
Add two free helpers at the bottom of `ClusterMgr.h` (used by Tasks 7-9):
```cpp
inline std::list<ClusterAddonInfo> ClusterAddonsFromSession(std::vector<SecureAddonInfo> const& addons)
{
    std::list<ClusterAddonInfo> out;
    for (SecureAddonInfo const& a : addons)
        out.emplace_back(a.Name, uint8(a.Status != SecureAddonInfo::BANNED), 0u, uint8(a.Status), a.HasKey);
    return out;
}
inline std::vector<SecureAddonInfo> SessionAddonsFromCluster(std::list<ClusterAddonInfo> const& addons)
{
    std::vector<SecureAddonInfo> out;
    for (ClusterAddonInfo const& a : addons)
    {
        SecureAddonInfo s;
        s.Name = a.Name;
        s.Status = SecureAddonInfo::SecureAddonStatus(a.State);
        s.HasKey = a.UsePublicKeyOrCRC;
        out.push_back(std::move(s));
    }
    return out;
}
```
(`#include "WorldSession.h"` in ClusterMgr.h for `SecureAddonInfo`; if that creates a cycle, move the two helpers to `ClientRedirect.h`.)

- [ ] **Step 3: Manual translations in `PlayerTransfer.cpp` (API map §B.8–B.12)**

Apply each exactly as written in the API map: spell map by value with `specMask = 0xFF`; talent map loop over `MAX_TALENT_SPECS` with `specMask = 1 << g`, `talentId = 0`; skills via `GetSkillStatusMap()` + `GetSkillRankByPos/GetSkillMaxRankByPos/GetSkillTempBonusByPos/GetSkillPermBonusByPos`; quest status via the new const overload; cooldowns via `GetSpellHistory()->GetCooldowns()` converting `CooldownEnd - Clock::now()` to ms.

- [ ] **Step 4: Manual translations in `NatsBus.cpp`**

- §B.2: replace `DoForAllOnlinePlayers` with the `sWorld->GetAllSessions()` loop.
- §B.7: `trans->InitializeToTime(remoteProgress)` now exists (Task 5) — no change.
- §B.17: `CHAR_UPD_NODE_ONLINE_CLEANUP` now exists (Task 4) — no change.
- §B.19: wrap the `/proc/self` block in `#ifndef _WIN32` with zero-returning stubs.
- **Stub the phase-2 handlers.** For each of `HandleIncomingChat`, `HandleIncomingMailNotify`, `HandleIncomingArenaResult`, `HandleGroupUpdate`, `HandleGroupDisband`, `HandleLFGRelay`, `HandleLFGRelayResponse`, `HandleBgCreateInst`, `HandleBgReady`, `HandleBgQueueJoin`, `HandleBgQueueLeave`, `HandleBgInstCreated`, `HandleIncomingRelay` (keep only the `GROUP_INNER_REROUTE_TO_MAP` inner type if it is what the teleport path relays; check `Dispatch`), replace the body with:
  ```cpp
  TC_LOG_DEBUG("server.worldserver", "NatsBus: <name> ignored (phase 2 feature not ported)");
  ```
  Keep the message-type constants, the `Dispatch` switch, and the public `Send*` methods for these features but make the `Send*` bodies no-ops too (same debug log). This removes the §B.13–B.16 manual items (SocialMgr, LFGMgr, ArenaTeam, BattlegroundQueue) from phase 1. Leave `RestoreBgCoordIfNeeded` as a no-op.
- `QueuePlayerDeactivation` and `QueueCallback` now exist on `World` — no change.

- [ ] **Step 5: Export macros** — add `TC_GAME_API` to `class NatsBus`, `class ClusterMgr`, `class SharedPlayerCache`, `class PacketTrace` (or its namespace functions) and to `ClientRedirect`'s free functions, since `Main.cpp` (outside `game`) calls them.

- [ ] **Step 6: Build until the game library links**

```bash
contrib/cluster/vm-sync-build.sh build
ssh wow@192.0.2.10.20 "grep -n -A4 ' error:' ~/source/c9core-tc/build/build.log | head -120"
```
Iterate: every remaining error is an API delta. Fix it in the ported file, add a one-line entry to the API map table (§1–§15) describing the delta, rebuild. Expected final state: `errors: 0`.

- [ ] **Step 7: Run the unit tests again** (`./tests/tests`), expected `All tests passed`.

- [ ] **Step 8: Commit**

```bash
git add src/server/game/Server docs/superpowers/specs/2026-10-05-ac-to-tc-api-map.md
git commit -m "feat(cluster): port the cluster core (NatsBus, ClusterMgr, ClientRedirect, PlayerTransfer, state sync, PacketTrace); phase-2 handlers stubbed

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Hook slab A — WorldSocket redirect auth and WorldSession redirect-out behaviour

Reference: `summaries/src_server_game_Server_WorldSocket.cpp.md` (hunks 3, 6, 7, 8a/8b/8c, 13 + Cross-file notes), `WorldSocket.h.md` (hunks 2, 4, 5), `WorldSession.cpp.md` (hunks 2, 4, 5, 7, 8, 9, 10, 11, 16). AC source: `ac-hunks/src_server_game_Server_WorldSocket.cpp.diff`, `..._WorldSession.cpp.diff`.

**Files:**
- Modify: `src/server/game/Server/WorldSocket.h` (:65 fwd decl; :113 method decls; :119-128 members)
- Modify: `src/server/game/Server/WorldSocket.cpp` (`Update` :74; `ReadDataHandler` :305-325; `SendPacket` :417-420; new functions after :622; `HandleAuthSessionCallback` :610-618)
- Modify: `src/server/game/Server/WorldSession.cpp` (includes; dtor :184; `SendPacket` :208-211; `Update` :295; `LogoutPlayer` :484-603; `InitializeSessionCallback` :1186)

**Interfaces:**
- Consumes: `sClusterMgr.TakePendingRedirect(accountId, remoteIp)` → `PendingRedirect` with `playerGuid`, `addons`; `SessionAddonsFromCluster`; `WorldSession` accessors from Task 5; `PT_ENABLED/PT_OPCODE/PT_OPCODE_HEX` from PacketTrace.h.
- Produces: a redirected client's new socket becomes an authenticated session that auto-logs the character in.

- [ ] **Step 1: WorldSocket.h**

```cpp
struct AccountInfo;              // next to struct AuthSession;
...
// private, next to HandleAuthSessionCallback:
void HandleRedirectionAuthProof(WorldPacket& recvPacket);
void HandleRedirectionAuthProofCallback(PreparedQueryResult result);
void TryCompleteRedirectAuth();
// private members, next to _authCrypt:
std::string _redirectAccountName;
std::unique_ptr<AccountInfo> _redirectAccount;
bool _redirectAwaitingToken = false;
uint32 _redirectWaitStartMs = 0;
bool _isRedirectConn = false;
```
Do **not** add `_encryptionSeeds` or `_sessionKey` (TC already has `_dosChallenge`; the socket-side key copy is unused).

- [ ] **Step 2: WorldSocket.cpp — includes and the three small hooks**

Includes: `#include "ClusterMgr.h"`, `#include "PacketTrace.h"`, `#include "IpAddress.h"`.
`Update()` first lines (hunk 3):
```cpp
if (_redirectAwaitingToken)
    TryCompleteRedirectAuth();
```
`ReadDataHandler()` (hunk 6): after the `sPacketLog->LogPacket` block, the inbound trace exactly as AC lines 137-153 of the diff (macros from PacketTrace.h).
`ReadDataHandler()` switch (hunk 7): between `case CMSG_PING` and `case CMSG_AUTH_SESSION`:
```cpp
case CMSG_SUSPEND_COMMS_ACK:
    packet.rfinish();
    return ReadDataHandlerResult::Ok;
case CMSG_AUTH_CONTINUED_SESSION:
{
    if (_authed)
    {
        TC_LOG_ERROR("network", "WorldSocket::ReadDataHandler: received duplicate CMSG_AUTH_CONTINUED_SESSION from {}", GetRemoteIpAddress().to_string());
        return ReadDataHandlerResult::Error;
    }
    try
    {
        HandleRedirectionAuthProof(packet);
        return ReadDataHandlerResult::WaitingForQuery;
    }
    catch (ByteBufferException const&) { }
    TC_LOG_ERROR("network", "WorldSocket::ReadDataHandler: malformed CMSG_AUTH_CONTINUED_SESSION from {}", GetRemoteIpAddress().to_string());
    return ReadDataHandlerResult::Error;
}
```
`SendPacket()` (hunk 8a): outbound trace after `sPacketLog->LogPacket`.
`HandleAuthSessionCallback()` (hunk 13): right after `_worldSession = new WorldSession(...)`: `_worldSession->SetSessionKey(account.SessionKey); _worldSession->SetAuthSeed(_serverChallenge);` (`_serverChallenge` is `std::array<uint8,4>`; adapt if TC stores it as `uint32`).

- [ ] **Step 3: WorldSocket.cpp — the redirect auth trio (hunk 8b/8c), TC-adapted**

Port `HandleRedirectionAuthProof` and `HandleRedirectionAuthProofCallback` from diff lines 201-241 with `SetData`→typed setters, `_queryProcessor.AddCallback`→`QueueQuery`. Port `TryCompleteRedirectAuth` from diff lines 243-351 with these exact substitutions:
```cpp
// crypt init: TC's 3-arg overload is AC's InitRedirect
_authCrypt.Init(account.SessionKey,
    std::span<uint8 const, 16>(_dosChallenge.data(), 16),
    std::span<uint8 const, 16>(_dosChallenge.data() + 16, 16));
// resume comms on the NEW socket (AC: SMSG_FORCE_SEND_QUEUED_PACKETS)
WorldPacket resume(SMSG_RESUME_COMMS, 0);
SendPacket(resume);
// session ctor: TC signature
_worldSession = new WorldSession(account.Id, std::move(_redirectAccountName),
    std::static_pointer_cast<WorldSocket>(shared_from_this()), account.Security,
    account.Expansion, account.MuteTime, account.TimezoneOffset, account.Locale,
    account.Recruiter, account.IsRectuiter);
_worldSession->SetSessionKey(account.SessionKey);
_worldSession->SetAuthSeed(_serverChallenge);
_worldSession->SetSecureAddons(SessionAddonsFromCluster(redirect->addons));
_worldSession->AddLegitCharacter(ObjectGuid(redirect->playerGuid));
_worldSession->SetRedirectAutoLoginGuid(redirect->playerGuid);
_authed = true;
_isRedirectConn = true;
// RBAC must load before AddSession (World::AddSession_ checks RBAC_PERM_SKIP_QUEUE)
QueueQuery(_worldSession->LoadPermissionsAsync().WithPreparedCallback(
    [this](PreparedQueryResult result) { LoadSessionPermissionsCallback(std::move(result)); }));
AsyncRead(Trinity::Net::InvokeReadHandlerCallback<WorldSocket>{ .Socket = this });
```
Drop `ValidateAccountFlags`, `account.Flags`, `account.TotalTime`. Skip Warden (as AC does). Keep AC's `REDIRECT_TOKEN_WAIT_MS` park-and-retry and `AUTH_FAILED`/`AUTH_UNKNOWN_ACCOUNT` responses. TC's `AccountInfo` (`WorldSocket.cpp:245`) members used above are exactly: `Id`, `SessionKey`, `Security`, `Expansion`, `MuteTime` (int64), `TimezoneOffset` (Minutes), `Locale`, `Recruiter`, `IsRectuiter` (sic), plus `IsBanned`/`IsLockedToIP`/`LastIP`/`LockCountry` for the same ban and IP-lock checks TC's normal path does at `:490-560`; apply those checks on the redirect path too, as AC does.

- [ ] **Step 4: WorldSession.cpp hooks**

Includes: `NatsBus.h`, `ClusterMgr.h`, `SharedPlayerCache.h`.
Dtor (hunk 4): wrap `LoginDatabase.PExecute("UPDATE account SET online = 0 ...")` with `if (!_redirectedOut)`.
`SendPacket` (hunk 5), after `if (!m_Socket) return;`:
```cpp
if (_redirectedOut)
    return; // the client is switching nodes; anything we send now breaks the redirect
if (_redirectAutoLoginGuid && _player && !_player->IsInWorld())
{
    switch (packet->GetOpcode())
    {
        case SMSG_SPELL_START: case SMSG_SPELL_GO:
        case SMSG_PLAY_SPELL_VISUAL: case SMSG_PLAY_SPELL_IMPACT:
            return; // passive-talent casts during load would animate on the old-node character
        default: break;
    }
}
```
`Update` (hunk 7): before the recv loop, drain and delete queued packets when `_redirectedOut`; add `&& !_redirectedOut` to the loop condition.
`LogoutPlayer` (hunks 8, 9, 10, 11): at the top, the redirect-out fast path:
```cpp
if (_redirectedOut && _player)
{
    TC_LOG_INFO("server.worldserver", "LogoutPlayer: {} redirected out, tearing down without save", _player->GetName());
    _player->CleanupsBeforeDelete();
    if (Map* map = _player->FindMap())
        map->RemovePlayerFromMap(_player, true);
    SetPlayer(nullptr);
    m_playerLogout = false;
    m_playerSave = false;
    m_playerRecentlyLogout = true;
    LogoutRequest(0);
    return;
}
bool crossNodeReroute = false;
if (_player && _player->IsBeingTeleportedFar())
{
    uint32 destMap = _player->GetTeleportDest().GetMapId();
    if (sNatsBus.IsConnected() && !sClusterMgr.IsMapLocal(destMap))
    {
        crossNodeReroute = true;
        _player->SetSemaphoreTeleportFar(false);
    }
}
if (!crossNodeReroute)
    while (_player && _player->IsBeingTeleportedFar())
        HandleMoveWorldportAck();
```
Then the second `while (IsBeingTeleportedFar()) HandleMoveWorldportAck();` gets `if (!crossNodeReroute)`, `if (save)` becomes `if (save && !crossNodeReroute)`, `sNatsBus.AnnounceOffline(_player->GetGUID().GetRawValue())` goes right before `sSocialMgr->SendFriendStatus(... FRIEND_OFFLINE ...)` guarded by `sNatsBus.IsConnected()`, and before `_player->CleanupsBeforeDelete()`:
```cpp
if (sClusterMgr.IsEnabled())
{
    sSharedPlayerCache.Remove(_player->GetGUID().GetRawValue());
    sClusterMgr.SetPlayerOwner(_player->GetGUID().GetRawValue(), 0);
}
```
Match the exact field/flag resets to TC's own logout tail (read `WorldSession.cpp:600-625`) rather than AC's.
`InitializeSessionCallback` (hunk 16), after `SendTutorialsData();`:
```cpp
if (_redirectAutoLoginGuid)
{
    TC_LOG_INFO("server.worldserver", "Redirect arrival: auto-login guid {:#x} for account {}", _redirectAutoLoginGuid, GetAccountId());
    WorldPacket data(CMSG_PLAYER_LOGIN, 8);
    data << uint64(_redirectAutoLoginGuid);
    HandlePlayerLoginOpcode(data);
}
```

- [ ] **Step 5: Build** — `contrib/cluster/vm-sync-build.sh build`, expected `errors: 0`.

- [ ] **Step 6: Commit**

```bash
git add src/server/game/Server/WorldSocket.h src/server/game/Server/WorldSocket.cpp src/server/game/Server/WorldSession.cpp
git commit -m "feat(cluster): redirect auth on WorldSocket and redirected-out session semantics

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Hook slab B — CharacterHandler login routing and redirect arrival

Reference: `summaries/src_server_game_Handlers_CharacterHandler.cpp.md` hunks 6a, 6b, 6c, 7, 8, 9, 10 and the Cross-file notes. AC source: `ac-hunks/src_server_game_Handlers_CharacterHandler.cpp.diff` lines 238-668.

**Files:**
- Modify: `src/server/game/Handlers/CharacterHandler.cpp` (`HandlePlayerLogin` :720-999)

**Interfaces:**
- Consumes: `sClusterMgr.{IsEnabled,PeekPendingTransferMapId,IsMapLocal,GetNodeId,GetNodeForMap,GetRedirectAddressForNode,TakePendingTransfer}`, `sNatsBus.{SendPlayerTransferForRedirect,PublishRedirectToken,BroadcastPlayerTransferFull,BroadcastPlayerStateFull,AnnounceOnline,IsConnected}`, `ClientRedirect::{GenerateToken,RedirectClient,SuspendClient}`, `sSharedPlayerCache.{Get,UpdateFromPlayer}`, `ClusterAddonsFromSession(GetSecureAddons())`, session accessors.

- [ ] **Step 1: 6a — wrong-node redirect**, inserted between the LoadFromDB-fail block (:735) and `GetMotionMaster()->Initialize()` (:738). Port diff lines 242-346 with: `LOG_*`→`TC_LOG_*`, `GetAddonsList()`→`ClusterAddonsFromSession(GetSecureAddons())`, `SaveToDB(false,false)`→`SaveToDB(false)`. Keep the "already a redirect arrival → kick, never double-redirect" branch and the `SetPlayer(nullptr); pCurrChar->RemoveAllAuras(); delete pCurrChar; m_playerLoading = false; return;` teardown order exactly (the RemoveAllAuras-after-SetPlayer order is the October 2 fix).

- [ ] **Step 2: 6b + 6c — apply the pending transfer, broadcast, shared-cache activate**, inserted after `GetMotionMaster()->Initialize()` (:738). Port diff lines 348-544 with `MotionTransport`→`Transport`, `AddPassenger(pCurrChar, false)`→`AddPassenger(pCurrChar)`, `_pendingPetTransfer = transfer->pet` → `SetPendingPetTransfer(std::make_unique<TransferPetInfo>(transfer->pet))`, `SetPendingTransportAttach({...})` with the Task 5 struct (same field names as AC). `hadNatsTransfer` stays in scope to Step 4.

- [ ] **Step 3: 7 — MOTD only for cold logins**: wrap TC's MOTD block (:753-757) in `if (GetRedirectAutoLoginGuid() == 0)`.

- [ ] **Step 4: 8 — teleport-far semaphore for channel joins, online flag with node id, post-transfer save** (around :826-838):
```cpp
if (hadNatsTransfer)
    pCurrChar->SetSemaphoreTeleportFar(true);
pCurrChar->SendInitialPacketsAfterAddToMap();
if (hadNatsTransfer)
    pCurrChar->SetSemaphoreTeleportFar(false);

CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_ONLINE);
stmt->setUInt8(0, sClusterMgr.GetNodeId());
stmt->setUInt32(1, pCurrChar->GetGUID().GetCounter());
CharacterDatabase.Execute(stmt);
// ... existing LOGIN_UPD_ACCOUNT_ONLINE ...
if (hadNatsTransfer)
    pCurrChar->SaveToDB(false);
```

- [ ] **Step 5: 9 — `AnnounceOnline`** after `sSocialMgr->SendFriendStatus(pCurrChar, FRIEND_ONLINE, ...)` (:851): `if (sNatsBus.IsConnected()) sNatsBus.AnnounceOnline(pCurrChar);`

- [ ] **Step 6: 10 — clear the auto-login guid last**, after `sScriptMgr->OnPlayerLogin(pCurrChar, firstLogin);` (:997): `SetRedirectAutoLoginGuid(0);`

- [ ] **Step 7: Build** — expected `errors: 0`.

- [ ] **Step 8: Commit**

```bash
git add src/server/game/Handlers/CharacterHandler.cpp
git commit -m "feat(cluster): login routing, redirect arrival and transfer apply in HandlePlayerLogin

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Hook slab C — Player (teleport handoff, dwell timer, state sync) and Object

Reference: `summaries/src_server_game_Entities_Player_Player.cpp.md` hunks 3, 4, 5, 7, 8, 9, 19, 21; `PlayerUpdates.cpp.md` hunks 2, 4, 6, 7, 8, 9, 10 + Cross-file dwell-timer notes; `PlayerStorage.cpp.md` hunk 3; `Object.cpp.md` hunk 7. AC source: the matching `.diff` files.

**Files:**
- Modify: `src/server/game/Entities/Player/Player.cpp` (`Update` :920-1264; `TeleportTo` :1539-1777; `GiveLevel` :2534; `LearnSpell` :3416; `ResetTalents` :3800; `ResurrectPlayer` :4454; `KillPlayer` :4489; `UpdateArea`/`UpdateZone` :6778-6905; `EquipItem` :11918; `SendInitialPacketsAfterAddToMap` :22491; `LearnTalent` :24934)
- Modify: `src/server/game/Entities/Object/Object.cpp` (`DestroyForNearbyPlayers` :3447-3476)

**Interfaces:**
- Consumes: Task 5 fields and declarations; `sClusterMgr.{IsEnabled,IsZoneLocal,IsMapLocal,GetNodeForZone,GetNodeForMap,GetRedirectAddressForNode,GetGroupRemoteMembers}`, `sNatsBus.{IsConnected,SendPlayerTransferSeamless,SendPlayerTransferForRedirect,PublishRedirectToken,BroadcastPlayerStateFull,BroadcastPlayerStateDelta,BroadcastPlayerTransferFull,ClaimPlayer,RelayToNode}`, `ClientRedirect`, `sSharedPlayerCache.UpdateFromPlayer`, `STATE_FIELD_*`.

- [ ] **Step 1: Includes** in Player.cpp: `ClusterMgr.h`, `NatsBus.h`, `ClientRedirect.h`, `SharedPlayerCache.h`, `SharedPlayerState.h`, `<random>`.

- [ ] **Step 2: `Player::Update`** (PlayerUpdates hunks 2, 4, 6):
- Line 921, before `if (!IsInWorld())`: `if (m_pendingZoneReroute) return;`
- After the `IsInWorld` check: the deferred transport reattach block (diff 20-92) with `MotionTransport`→`Transport`, `AddPassenger(this)`.
- Zone tick else-branch (:1135-1138): `UpdateClusterZoneRouting(newzone);` after the `UpdateArea` call.
- Before the closing brace (:1264), in this order: dwell timer (diff 210-315), cooldown decrement, cold refresh, dirty detect, `BroadcastClusterStateIfDirty(p_time)`. **Skip** the party-frame sync block (hunk 5, phase 2). In the dwell-timer FIRE path keep: `SaveToDB(trans, false)`; `GetSession()->AddTransactionCallback(CharacterDatabase.AsyncCommitTransaction(trans)).AfterComplete(...)` publishing `SendPlayerTransferSeamless` then `PublishRedirectToken(..., ClusterAddonsFromSession(GetSession()->GetSecureAddons()))`; then `BroadcastPlayerStateFull`, `ClaimPlayer`, `ClientRedirect::RedirectClient` (no SuspendClient on the zone path), `GetSession()->SetRedirectedOut()`, `m_pendingZoneReroute = true`, cooldown, `return`.

- [ ] **Step 3: `UpdateClusterZoneRouting` and `BroadcastClusterStateIfDirty`** — replace the Task 5 empty bodies with diff 360-401 and 426-522 (`getPowerType`→`GetPowerType`, `LOG_*`→`TC_LOG_*`). Place both between `UpdateArea` and `UpdateZone`.

- [ ] **Step 4: `UpdateZone`**: `UpdateClusterZoneRouting(newZone);` after `m_zoneUpdateTimer = ZONE_UPDATE_INTERVAL;` (:6821); `MarkClusterStateDirty(STATE_FIELD_POSITION);` as the last statement before the closing brace.

- [ ] **Step 5: `TeleportTo` cross-node branch** (Player.cpp hunk 3), inside the far branch between `SendDirectMessage(transferPending.Write())` and `oldmap->RemovePlayerFromMap(this, false)`:
1. Hoist `m_teleport_dest = WorldLocation(mapid, x, y, z, orientation); m_teleport_options = options; SetFallInformation(0, GetPositionZ());` above the map removal (delete the later duplicates at :1756-1758).
2. Port diff lines 25-247 with: `SaveToDB(trans, false)`; `CHAR_UPD_CHARACTER_POSITION` binds `setFloat(0..3)`, `setUInt16(4, mapid)`, `setUInt32(5, 0)`, `setUInt32(6, GetGUID().GetCounter())`; `SetSemaphoreTeleportFar(true)`; addons via `ClusterAddonsFromSession`; `SMSG_NEW_WORLD` via `WorldPackets::Movement::NewWorld` with `MapID = mapid`, `Pos = m_teleport_dest.GetPosition()`; then `ClientRedirect::RedirectClient`, `ClientRedirect::SuspendClient`, `GetSession()->SetRedirectedOut()`, the `GROUP_INNER_REROUTE_TO_MAP` relay loop (keep: the group member reroute is phase 1 since it is the teleport path), transport detach without nulling `m_transport`, `PurgeAndApplyPendingMovementChanges(false)`, `return true`.

- [ ] **Step 6: Full-snapshot broadcasts** — add once as a private helper near the top of Player.cpp:
```cpp
static void ClusterBroadcastFull(Player* player)
{
    if (sClusterMgr.IsEnabled() && sNatsBus.IsConnected())
        sNatsBus.BroadcastPlayerTransferFull(player);
}
```
and call `ClusterBroadcastFull(this);` at: end of `GiveLevel` (after `OnPlayerLevelChanged`), end of `LearnSpell` guarded by `if (!dependent)`, before `return true` in `ResetTalents`, end of `ResurrectPlayer`, end of `KillPlayer` (after `UpdateObjectVisibility()`), before the final `return pItem;` in `EquipItem`, before the final `return true;` in `LearnTalent`.

- [ ] **Step 7: `SendInitialPacketsAfterAddToMap`**: `if (!GetSession()->GetRedirectAutoLoginGuid()) CastSpell(this, 836, true); // LOGINEFFECT`.

- [ ] **Step 8: `WorldObject::DestroyForNearbyPlayers`** (Object.cpp hunk 7), after the `HaveAtClient` check (:3463-3464):
```cpp
if (player->GetSession() && player->GetSession()->IsRedirectPending())
{
    player->m_clientGUIDs.erase(GetGUID());
    continue;
}
```

- [ ] **Step 9: Build** — expected `errors: 0`.

- [ ] **Step 10: Commit**

```bash
git add src/server/game/Entities/Player/Player.cpp src/server/game/Entities/Object/Object.cpp
git commit -m "feat(cluster): cross-node teleport, zone dwell timer, state sync and redirect-safe object destroy on Player

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: Hook slab D — ghost maps and transport peer sync

Reference: `summaries/src_server_game_Maps_MapMgr.cpp.md` hunk 3; `Map.cpp.md` hunks 3, 5; `TransportMgr.cpp.md` hunks 2, 3; `TransportMgr.h.md`; `Transport.cpp.md` hunk 7.

**Files:**
- Modify: `src/server/game/Maps/MapManager.cpp` (`CreateBaseMap` :70-99), `MapManager.h` (:44-58 inline helpers)
- Modify: `src/server/game/Maps/Map.cpp` (`EnsureGridLoaded` :535; `RemoveFromMap(Transport*)` :1058-1060)
- Modify: `src/server/game/Maps/TransportMgr.cpp` (`CreateTransport` :390-426; `SpawnContinentTransports` :434-436), `TransportMgr.h` (:162)
- Modify: `src/server/game/Entities/Transport/Transport.cpp` (`UpdatePassengerPositions` :699)

**Interfaces:**
- Consumes: `sClusterMgr.{IsEnabled,IsMapLocal,GetNodeId}`, `sNatsBus.{IsConnected,QueryTransportSync}`, `Transport::InitializeToTime`, `Map::SetGhostMap/IsGhostMap`.

- [ ] **Step 1: `MapManager::CreateBaseMap`** — after `ASSERT(entry);`:
```cpp
if (sClusterMgr.IsEnabled() && !sClusterMgr.IsMapLocal(id))
{
    if (entry->Instanceable())
    {
        TC_LOG_DEBUG("maps.cluster", "CreateBaseMap: instanceable map {} not owned by node {}, not creating", id, sClusterMgr.GetNodeId());
        return nullptr;
    }
    // Continent owned by another node: ghost map so transports can route through it; grids never load.
    map = new Map(id, i_gridCleanUpDelay, 0, REGULAR_DIFFICULTY);
    map->SetGhostMap();
    TC_LOG_DEBUG("maps.cluster", "CreateBaseMap: ghost map {} on node {}", id, sClusterMgr.GetNodeId());
    Trinity::unique_trackable_ptr<Map>& ptr = i_maps[id];
    ptr.reset(map);
    map->SetWeakPtr(ptr);
    sScriptMgr->OnCreateMap(map);
    return map;
}
if (sConfigMgr->GetBoolDefault("InstanceServer.Enable", false) && !entry->Instanceable())
    return nullptr;
```
Move the trailing `ASSERT(map);` so it applies only to the normal branch. In `MapManager.h` guard the three inline helpers: `Map* m = CreateBaseMap(mapid); if (!m) return 0;` (and the `GetZoneAndAreaId` variant sets both outputs to 0).

- [ ] **Step 2: `Map::EnsureGridLoaded`** — after `EnsureGridCreated(...)` at :535: `if (_ghostMap) return false;`

- [ ] **Step 3: `Map::RemoveFromMap(Transport*)`** — change the send condition to `if (itr->GetSource()->GetTransport() != obj && !itr->GetSource()->IsBeingTeleportedFar())`.

- [ ] **Step 4: `TransportMgr`** — `TransportMgr.h`: `#include <unordered_map>`, private `std::unordered_map<uint32, uint32> _spawnSyncData;`. `TransportMgr.cpp`: `#include "NatsBus.h"`, `#include <chrono>`; in `SpawnContinentTransports` after the empty-templates return:
```cpp
if (sNatsBus.IsConnected())
{
    _spawnSyncData = sNatsBus.QueryTransportSync();
    TC_LOG_INFO("server.loading", "Transport sync: {} peer entries", _spawnSyncData.size());
}
```
In `CreateTransport`, replace the "spawn at first waypoint" position block (:390-399) with AC's timer computation and keyframe walk (diff 22-141) using `Node->ContinentID`, `Node->Loc.X/Y/Z`, and call `trans->InitializeToTime(timer);` after the zone-script line and before `HashMapHolder<Transport>::Insert(trans)`.

- [ ] **Step 5: `Transport::UpdatePassengerPositions`** — replace `if (passenger->GetMap() != GetMap()) continue;` with
```cpp
Map* passengerMap = passenger->FindMap();
if (!passengerMap || passengerMap != GetMap())
    continue;
```

- [ ] **Step 6: Build** — expected `errors: 0`.

- [ ] **Step 7: Commit**

```bash
git add src/server/game/Maps src/server/game/Entities/Transport/Transport.cpp
git commit -m "feat(cluster): ghost maps for non-local continents and transport position peer sync

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 11: Hook slab E — World tick, worldserver bootstrap, CLI, conf block

Reference: `summaries/src_server_game_World_World.cpp.md` hunks 5, 6, 8, 9, 10, 11a, 12; `Main.cpp.md` hunks 3, 7, 14; `CliRunnable.cpp.md` hunks 1, 3, 5, 6; `worldserver.conf.dist.md`.

**Files:**
- Modify: `src/server/game/World/World.cpp` (`LoadConfigSettings` :1593; `SetInitialWorldSettings` :1627-1640, :2230, :2273; `Update` :2381, :2467, :2610)
- Modify: `src/server/worldserver/Main.cpp` (:129, :315-322, :698-708)
- Modify: `src/server/worldserver/CommandLine/CliRunnable.cpp` (:32-40, :120-127, :154-162, :177-184)
- Modify: `src/server/worldserver/worldserver.conf.dist` (section index after :40; append after :4175; comment at :363)

- [ ] **Step 1: World.cpp**
Includes: `ClusterMgr.h`, `NatsBus.h`, `ObjectAccessor.h`.
`LoadConfigSettings`, inside the existing `if (reload)` before `sScriptMgr->OnConfigLoad(reload)`: `sClusterMgr.RefreshPacketTraceConfig(); sNatsBus.RefreshConfigCache();`
`SetInitialWorldSettings`: wrap the `ExistMapAndVMap` check in `if (!sConfigMgr->GetBoolDefault("InstanceServer.Enable", false)) { ... }`; wrap `sTransportMgr->SpawnContinentTransports();` the same way; after the final `TC_METRIC_EVENT("events", "World initialized", ...)`: `if (sNatsBus.IsConnected()) sNatsBus.SetWorldReady();`
`Update`, after the who-list timer block:
```cpp
if (m_timers[WUPDATE_CLUSTER].Passed())
{
    m_timers[WUPDATE_CLUSTER].Reset();
    sNatsBus.Update();
    sClusterMgr.PurgeStaleEntries(getMSTime());
}
```
After `UpdateSessions(diff);` (hunk 11a only, diff lines 173-199): drain `_playerDeactivateQueue` under the mutex, and for each guid: `Player* p = ObjectAccessor::FindPlayer(ObjectGuid(guid)); if (!p) continue; p->RemoveAllAuras(); if (WorldSession* s = p->GetSession()) s->SetPlayer(nullptr); if (Map* m = p->FindMap()) m->RemovePlayerFromMap(p, true);` with the log lines from AC. **Skip** the group-invite drain (hunk 11b, phase 2).
Before "Process cli commands": `{ TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Process pending callbacks")); ProcessPendingCallbacks(); }`

- [ ] **Step 2: Main.cpp**
Includes `ClusterMgr.h`, `NatsBus.h`. After `signal(SIGABRT, ...)`:
```cpp
#if TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
    signal(SIGPIPE, SIG_IGN); // cnats writes to a raw socket
#endif
```
Between `sScriptMgrHandle` and `// Initialize the World` (diff lines 73-99 minus the modules line):
```cpp
if (sConfigMgr->GetIntDefault("ClusterServer.NodeId", 0) > 0)
{
    sClusterMgr.LoadLocalMaps();
    uint16 gamePort = uint16(sConfigMgr->GetIntDefault("ClusterServer.GamePort", sConfigMgr->GetIntDefault("WorldServerPort", 8085)));
    std::string gameAddress = sConfigMgr->GetStringDefault("ClusterServer.GameAddress", "127.0.0.1");
    uint8 serverType = (sConfigMgr->GetBoolDefault("ClusterServer.InstanceServer", false) ||
                        sConfigMgr->GetBoolDefault("InstanceServer.Enable", false)) ? 1 : 0;
    sNatsBus.Initialize(sConfigMgr->GetStringDefault("ClusterServer.NatsURL", "nats://127.0.0.1:4222"), serverType, gamePort, gameAddress);
}
```
(`NatsBus::Initialize(std::string const& natsUrl, uint8 serverType, uint16 gamePort, std::string const& gameAddress)`, `NatsBus.h:82`; the serverType expression is AC's verbatim.)
`ClearOnlineAccounts()`: replace the characters line with
```cpp
uint8 nodeId = uint8(sConfigMgr->GetIntDefault("ClusterServer.NodeId", 0));
if (nodeId > 0)
    CharacterDatabase.DirectPExecute("UPDATE characters SET online = 0, owning_node_id = 0 WHERE owning_node_id = {}", nodeId);
else
    CharacterDatabase.DirectExecute("UPDATE characters SET online = 0 WHERE online <> 0");
```
Leave the account and battleground lines as TC has them; add the two as a note in `docs/superpowers/improvements.md` ("ClearOnlineAccounts account/BG resets are realm-scoped, not node-scoped").

- [ ] **Step 3: CliRunnable.cpp** — add `<iostream>`, `<unistd.h>`; `bool const stdinIsTty = ::isatty(STDIN_FILENO) == 1;` before the readline setup; gate the setup on it; replace the readline call with AC's `if (stdinIsTty) { readline...; if (!command_str) { World::StopNow(SHUTDOWN_EXIT_CODE); break; } } else { std::string line; if (!std::getline(std::cin, line)) return; ... }` shape from diff lines 99-131; `add_history` only when tty; the `feof(stdin)` branch becomes Windows-only. Keep `"TC> "`.

- [ ] **Step 4: worldserver.conf.dist** — add `#    INSTANCE SERVER MODE` and `#    CLUSTER SERVER` to the section index; append the two blocks from `ac-hunks/src_server_apps_worldserver_worldserver.conf.dist.diff` lines 86-418 at the end, replacing "AzerothCore"/"C9Core" wording with "TrinityCore" only inside the new text. Add AC's one-sentence note under `PlayerSaveInterval` without changing TC's 90000 default.

- [ ] **Step 5: Build and install**

```bash
contrib/cluster/vm-sync-build.sh build && contrib/cluster/vm-sync-build.sh install
ssh wow@192.0.2.10.20 'ls -la ~/tc-335/bin/ && ~/tc-335/bin/worldserver --version'
```
Expected: `errors: 0`; binaries `worldserver authserver mapextractor vmap4extractor vmap4assembler mmaps_generator`; version prints the `cluster` branch hash.

- [ ] **Step 6: Commit**

```bash
git add src/server/game/World/World.cpp src/server/worldserver
git commit -m "feat(cluster): bus bootstrap and tick, node-scoped online reset, non-tty CLI, ClusterServer conf block

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 12: Fresh-context hook review against AC (gate before any runtime test)

**Files:** none modified unless findings require it.

- [ ] **Step 1: Dispatch a reviewer with no port context.** Prompt: "Compare each phase-1 hook in `git diff 092eb27b20..HEAD -- src/server/game src/server/worldserver` against the AC source in `docs/superpowers/port/ac-hunks/*.diff` using `docs/superpowers/port/summaries/*.md` as the index. Hunt specifically for: (1) any `SendPacket` path that can reach a session after `SetRedirectedOut()`; (2) `HandleRemotePlayerOnline` / deactivation kicking a redirected-out session; (3) a 0-HP or dead player passing the dwell-timer gate or arriving alive; (4) the teleport path missing `SetRedirectedOut()`; (5) cast/visual packets sent to an arrival before `IsInWorld()`; (6) MOTD resent on arrival; (7) any `phase2` or `noise` hunk that was ported; (8) any `GetRawValue`↔`GetCounter` mix-up in prepared-statement binds. Report file:line and the AC line it diverges from."

- [ ] **Step 2: Fix every confirmed finding, rebuild, commit** with message `fix(cluster): review findings on the hook slab`.

---

### Task 13: Realm bring-up — databases, client data, confs, two nodes on the bus

**Files:**
- Create (on VMs, not in git): `~/tc-335/etc/worldserver.conf`, `~/tc-335/etc/authserver.conf` on node 1; `~/tc-335/etc/worldserver.conf` on node 2.

- [ ] **Step 1: Databases on mariadb-01** (run from node 1; the DBnet path is already MTU 1500):

```bash
ssh wow@192.0.2.10.20 'mysql -h 192.0.2.110 -u root -p -e "
CREATE DATABASE IF NOT EXISTS tc_auth DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS tc_characters DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS tc_world DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS trinity@\"192.0.2.%\" IDENTIFIED BY \"<new password, stored in credentials.md>\";
GRANT ALL ON tc_auth.* TO trinity@\"192.0.2.%\"; GRANT ALL ON tc_characters.* TO trinity@\"192.0.2.%\"; GRANT ALL ON tc_world.* TO trinity@\"192.0.2.%\"; FLUSH PRIVILEGES;"'
```
Record the password in the usual credentials file, never in the repo.

- [ ] **Step 2: TDB world import**

```bash
ssh wow@192.0.2.10.20 'cd ~/tc-335/bin && curl -L -o TDB_full_world_335.26091_2026_09_09.7z https://github.com/TrinityCore/TrinityCore/releases/download/TDB335.26091/TDB_full_world_335.26091_2026_09_09.7z && 7z x TDB_full_world_335.26091_2026_09_09.7z && ls -la TDB_full_world_335.26091_2026_09_09.sql'
```
Leave the `.sql` in `bin/`; TC's updater (`Updates.AutoSetup = 1`) imports it on first start and then applies `sql/updates`. Auth and characters are created from `sql/base/*.sql` the same way.

- [ ] **Step 3: Client data** (dbc, maps, vmaps; mmaps skipped in phase 1)

```bash
ssh wow@192.0.2.10.20 'sudo mkdir -p /mnt/wow335 && sudo mount -t cifs //192.0.2.10/wow-335 /mnt/wow335 -o ro,guest,vers=3.0 2>/dev/null || mount | grep wow335
mkdir -p ~/tc-335/data && cd /mnt/wow335 && ~/tc-335/bin/mapextractor -o ~/tc-335/data -e 7 -f 0 > ~/tc-335/extract-maps.log 2>&1; tail -2 ~/tc-335/extract-maps.log
cd ~/tc-335/data && ~/tc-335/bin/vmap4extractor -d /mnt/wow335/Data > ~/tc-335/extract-vmaps.log 2>&1 && mkdir -p vmaps && ~/tc-335/bin/vmap4assembler Buildings vmaps >> ~/tc-335/extract-vmaps.log 2>&1; tail -2 ~/tc-335/extract-vmaps.log; rm -rf Buildings
du -sh ~/tc-335/data/*'
```
Adjust the share mount options to match how the AC extraction mounted it (check `~/335` notes on node 1). Expected: `dbc/`, `maps/`, `vmaps/` populated. Then `rsync -a ~/tc-335/ wow@192.0.2.10.21:~/tc-335/` from node 1 (bin, etc, data).

- [ ] **Step 4: Confs** — copy `~/tc-335/etc/worldserver.conf.dist` to `worldserver.conf` on each node and set:

| key | node 1 (192.0.2.20) | node 2 (192.0.2.21) |
| --- | --- | --- |
| `DataDir` | `"/home/wow/tc-335/data"` | same |
| `LoginDatabaseInfo` | `"192.0.2.110;3306;trinity;<pw>;tc_auth"` | same |
| `WorldDatabaseInfo` | `"192.0.2.110;3306;trinity;<pw>;tc_world"` | same |
| `CharacterDatabaseInfo` | `"192.0.2.110;3306;trinity;<pw>;tc_characters"` | same |
| `Updates.EnableDatabases` | `7` | `0` |
| `mmap.enablePathFinding` | `0` | `0` |
| `ClusterServer.NodeId` | `1` | `2` |
| `ClusterServer.NatsURL` | `"nats://192.0.2.20:4222"` | same |
| `ClusterServer.AuthKey` | value from `~/335/etc/worldserver.conf` on node 1 | same |
| `ClusterServer.GameAddress` | `"192.0.2.20"` | `"192.0.2.21"` |
| `ClusterServer.Maps` | `"0,530"` | `"1,571"` |
| `ClusterServer.Zones` | `"1637"` | `""` |
| `Logger.network` | level `3` | `3` |
| `ClusterServer.PacketTrace.Enable` / `RedirectDebug` | `0` / `0` (turn on per test window only) | same |

Edit keys in place and verify each with `grep -nE '^<key>' worldserver.conf` showing exactly one line. `authserver.conf` on node 1: `LoginDatabaseInfo` to `tc_auth`. Realmlist: after the first authserver start, `UPDATE tc_auth.realmlist SET address='192.0.2.20', port=8085 WHERE id=1;` (clients get the redirect address from `ClusterServer.RedirectAddress`/`GameAddress`, not the realmlist, after login).

- [ ] **Step 5: First start, node 1 only, interactive** (`Updates` runs here):

```bash
ssh wow@192.0.2.10.20 'cd ~/tc-335/bin && ./authserver > ../logs-auth.txt 2>&1 & sleep 3; cd ~/tc-335/bin && ./worldserver 2>&1 | tee ../logs-node1-first.txt'
```
Expected in the log: TDB import, `>> Applied N queries` including `2026_10_05_00_characters.sql`, `NatsBus: connected`, `World initialized`. Create a GM account from the console: `account create test test` then `account set gmlevel test 3 -1`.

- [ ] **Step 6: Start node 2, confirm the bus and routing table**

```bash
ssh wow@192.0.2.10.21 'cd ~/tc-335/bin && nohup ./worldserver > ../logs-node2.txt 2>&1 & sleep 20; grep -nE "NatsBus|ClusterMgr" ../logs-node2.txt | tail -20'
ssh wow@192.0.2.10.20 'grep -nE "node 2|announce" ~/tc-335/logs-node1-first.txt | tail -10'
```
Expected: each node logs the other's announce with `maps=` matching the table above; `.cluster` console command (if ported with ClusterMgr) or the announce log confirms node 1 = maps 0,530 zone 1637 and node 2 = maps 1,571.

---

### Task 14: Handoff tests (the phase-1 success predicate)

**Files:**
- Create: `docs/superpowers/port/phase1-test-log.md`

Every test: PacketTrace and RedirectDebug ON for the window (`.reload config` after editing), both node logs captured with a marker line (`.server info` output or a `date` echo appended to each log), then OFF again. The character used has at least one talent and an active long-duration aura (buff) before each handoff. Record for each test: node logs path, line numbers of `SMSG_CONNECT_TO` send on the source and `redirect auth` success on the destination, and the client observation.

- [ ] **Test 1: Cold login on the correct node.** Character in Elwynn (map 0). Client realmlist → node 1. Expected: login completes on node 1, `CHAR_UPD_CHAR_ONLINE` with `owning_node_id = 1`, no redirect.
- [ ] **Test 2: Cold login on the wrong node.** Character in Durotar (map 1) logging in via node 1. Expected: node 1 sends `SMSG_CONNECT_TO` to 192.0.2.21, node 2 accepts the token, auto-login, one loading screen, no MOTD on arrival, no login sparkle.
- [ ] **Test 3: Zone path.** Walk from Durotar into Orgrimmar (zone 1637, node 1) and back out. Expected: dwell timer arms at 2 s, fires once, transfer published from the save commit callback, arrival on node 1 with same HP/auras/talents; walk-out returns to node 2 the same way; no `kicked` lines on either node.
- [ ] **Test 4: Teleport path.** Hearthstone from Kalimdor (node 2) to a Stormwind inn (map 0, node 1). Expected: `SMSG_NEW_WORLD` + `SMSG_CONNECT_TO` + `SMSG_SUSPEND_COMMS` on node 2, `SetRedirectedOut` logged, arrival on node 1, no leftover session for the character on node 2 (`.account onlinelist` or `grep` the logout line).
- [ ] **Test 5: Map path.** Deeprun Tram (map 369, owned by whichever node; make sure map 369 is in one node's `ClusterServer.Maps` or let the default routing cover it) or a boat from Menethil (map 0) to Theramore (map 1). Expected: same observations as test 4, plus the transport position matches on both nodes within one waypoint (compare `transport sync` log lines).
- [ ] **Test 6: Death on a zone border.** Die just inside Orgrimmar's zone while owned by node 1, release. Expected: dwell timer does not fire while dead (`!IsAlive()` re-arm), release and resurrect work, after resurrection the timer behaves normally.
- [ ] **Test 7: Node 2 restart while node 1 is up.** Kill node 2, wait > `NodeDeadThreshold`, restart. Expected: node 1 logs the dead node, drops maps 1/571 from routing, then re-adds on the new announce; a login for a Kalimdor character during the gap is refused cleanly (no crash), and succeeds after.

- [ ] **Audit checklist, run against every test's logs** (spec §9): no `SendPacket` after redirected-out (grep for the debug line added in Task 7 if you add one; otherwise confirm zero packets on the source after `SMSG_CONNECT_TO` in the PacketTrace); no stale-session kick of the redirected session; no 0-HP arrival alive; teleport path shows `redirected out`; no `SMSG_SPELL_START/GO` before `IsInWorld` on arrival; no MOTD on arrival.

- [ ] **Write `docs/superpowers/port/phase1-test-log.md`** with one section per test: date, commit hash, character, log file paths, the key line numbers, pass/fail. Commit: `docs(cluster): phase 1 handoff test log`.

---

### Task 15: Close-out

- [ ] Set PacketTrace/RedirectDebug to 0 in both confs; confirm with grep.
- [ ] Update `docs/superpowers/improvements.md` with anything surfaced during bring-up (at minimum the `ClearOnlineAccounts` realm-scope note from Task 11 and any AC-side bug found).
- [ ] Update the memory file `project_c9core_tc_port.md` (laptop) with the final commit hash, the VM state, and what phase 2 starts from.
- [ ] Report: for each of the seven tests, the log pointer and the result. Phase 1 is done only if all seven pass and the audit checklist is clean.
