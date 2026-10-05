# AC (c9core) → TrinityCore 3.3.5 API map for the 20 cluster files

Date: 2026-10-05
Scope: every external identifier used by the files in `docs/superpowers/port/ac-new/`
(src/server/game/Server/{NatsBus,ClusterMgr,ClientRedirect,PlayerTransfer,PlayerStateSync,SharedPlayerCache,PacketTrace}.{h,cpp},
SharedPlayerState.h, src/server/shared/Cluster/{ClusterAuth,PskCrypt}.{h,cpp}, ClusterMgmtProtocol.h).
Every TC claim below was grepped in `/home/wow/source/Trinitycore/src` (paths given relative to that). Every AC claim was
grepped in `/home/wow/src/c9core-pub/src`. `ac-new/` line numbers are from the copied files.

Legend for the "apply" column: **sed** = covered by the sed block in §A; **manual** = listed in §B; **none** = no change.

Files that need NO changes beyond the sed block: `ClusterAuth.{h,cpp}`, `PskCrypt.{h,cpp}`, `ClusterMgmtProtocol.h`,
`SharedPlayerState.h`, `PlayerStateSync.{h,cpp}`, `SharedPlayerCache.{h,cpp}` (they only use `Define.h` int typedefs,
OpenSSL, the STL, and `LOG_INFO`). TC's `Define.h` (`common/Define.h`) provides `uint8/16/32/64`, `int32`, `uint64`.

---

## 1. Macros, export symbols, namespaces

| AC identifier | TC identifier | apply | notes |
|---|---|---|---|
| `LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL(filter, fmt, ...)` | `TC_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL(filter, fmt, ...)` | sed | `common/Logging/Log.h:153-169`. TC's body is `sLog->OutMessage(filter, level, Trinity::FormatString<Args...> fmt, Args&&...)` = `fmt::format_string` (`common/Utilities/StringFormat.h:28`). `{}` / `{:016X}` / `{:#x}` / `{:.1f}` placeholders are identical. Filter strings (`"server.worldserver"`, `"cluster.packettrace"`) are free-form in TC too — `cluster.packettrace` needs a `Logger.cluster.packettrace=` entry in worldserver.conf or it inherits root. |
| `C9_GAME_API` / `AC_GAME_API` / `C9_SHARED_API` | `TC_GAME_API` / `TC_SHARED_API` | none | **Not used** in any of the 20 files (grep confirmed). TC defines both: `common/Define.h:106-114`. Note TC's `src/server/shared` library exports with `TC_SHARED_API` (e.g. `server/shared/Packets/ByteBuffer.h:31`); if `ClusterAuth`/`PskCrypt` land in `src/server/shared/Cluster` and are called from `game`, their free functions/class need `TC_SHARED_API` added (manual, §B.1). |
| `ASSERT`, `ABORT`, `ACORE_*`, `C9CORE_*` (other than header guard) | — | none | Not used. Only `C9CORE_PACKETTRACE_H` include guard (`PacketTrace.h:289,290,388`) — cosmetic rename to `TRINITY_PACKETTRACE_H` via sed. |
| `namespace Acore::PacketTrace` / `Acore::PacketTrace::X` | `namespace Trinity::PacketTrace` / `Trinity::PacketTrace::X` | sed | `PacketTrace.h:342,352,354,357`, `PacketTrace.cpp:23`. TC's util namespace is `Trinity` (`common/Utilities/StringFormat.h`). |
| `fmt::format(...)` (direct) | same | none | `ClientRedirect.cpp:121,126,131,143-149`. Works: TC `Log.h:24` includes `StringFormat.h` which includes `<fmt/core.h>`. House style would be `Trinity::StringFormat(...)`; optional sed provided. |
| `sNatsBus`, `sClusterMgr`, `sSharedPlayerCache` | same (defined by the new files) | none | |

## 2. Include paths

| AC include | TC include | apply | notes |
|---|---|---|---|
| `"Entities/Player/Player.h"` | `"Player.h"` | sed | NatsBus.cpp:42. TC uses flat includes (every dir is on the include path). |
| `"Entities/Transport/Transport.h"` | `"Transport.h"` | sed | NatsBus.cpp:43 |
| `"Globals/ObjectAccessor.h"` | `"ObjectAccessor.h"` | sed | NatsBus.cpp:44 |
| `"Groups/Group.h"` | `"Group.h"` | sed | NatsBus.cpp:45 |
| `"WorldSessionMgr.h"` | *(delete line)* | sed | NatsBus.cpp:50. No such header in TC; session API lives in `World.h` (already included at :48). |
| `"AddonMgr.h"` | *(delete; see §9)* | sed | ClusterMgr.h:21, NatsBus.h:43. TC's `server/game/Addons/AddonMgr.h` exists but has no `AddonInfo` (it has `SavedAddon`). |
| `"AuthDefines.h"` | same | none | NatsBus.h:21. TC: `common/Cryptography/Authentication/AuthDefines.h` → `using SessionKey = std::array<uint8, SESSION_KEY_LENGTH>` (:25). AC's is identical type. |
| `"Define.h"`, `"Log.h"`, `"Config.h"`, `"Timer.h"`, `"GameTime.h"`, `"Opcodes.h"`, `"WorldPacket.h"`, `"WorldSession.h"`, `"World.h"`, `"Player.h"`, `"Pet.h"`, `"Item.h"`, `"SpellAuras.h"`, `"SpellInfo.h"`, `"Transport.h"`, `"ObjectGuid.h"`, `"Position.h"`, `"DBCStores.h"`, `"DatabaseEnv.h"`, `"LFGMgr.h"`, `"SocialMgr.h"`, `"ArenaTeam.h"`, `"ArenaTeamMgr.h"`, `"Battleground.h"`, `"BattlegroundMgr.h"`, `"BattlegroundQueue.h"` | same | none | All exist in TC (verified with `find`): `common/{Define,Logging/Log,Configuration/Config,Time/Timer}.h`, `server/game/{Time/GameTime,Server/Protocol/Opcodes,Server/WorldPacket,Server/WorldSession,World/World,Entities/Player/Player,Entities/Pet/Pet,Entities/Item/Item,Spells/Auras/SpellAuras,Spells/SpellInfo,Entities/Transport/Transport,Entities/Object/ObjectGuid,Entities/Object/Position,DataStores/DBCStores,DungeonFinding/LFGMgr,Entities/Player/SocialMgr,Battlegrounds/*}.h`, `server/database/Database/DatabaseEnv.h`. |
| *(missing)* `<list>` | add `#include <list>` | sed | ClusterMgr.h and NatsBus.h use `std::list<...>` and only got it transitively from AC's AddonMgr.h. |
| *(missing)* `<cctype>` | add | sed | NatsBus.cpp:2106 uses `::tolower`. |
| `<nats.h>` | same | none | cnats; CMake find/link is outside these files. |
| *(missing)* `"ClusterMgr.h"` in `NatsBus.h` | add `#include "ClusterMgr.h"` | manual (Task 6) | `NatsBus.h` declares `std::list<ClusterAddonInfo>` (§14); AC got `AddonInfo` from `AddonMgr.h`. No cycle (ClusterMgr.h does not include NatsBus.h). |
| *(missing)* `"WorldSession.h"` in `ClusterMgr.h` | add | manual (Task 6) | Needed for `SecureAddonInfo` in the `ClusterAddonsFromSession`/`SessionAddonsFromCluster` helpers (§14). No cycle: WorldSession.h includes no cluster header. |
| *(missing)* `"SpellHistory.h"` in `PlayerTransfer.cpp` | add | manual (Task 6) | §B.12 cooldown loop uses `SpellHistory::Clock` / `GetCooldowns()`; `Player.h`/`Unit.h` only forward-declare `SpellHistory`. |

## 3. Config — `sConfigMgr->GetOption<T>(key, def)` → typed getters

TC (`common/Configuration/Config.h:46-49`): `GetStringDefault(name, std::string def)`, `GetBoolDefault(name, bool)`,
`GetIntDefault(name, int)`, `GetFloatDefault(name, float)`. No template getter is public (`GetValueDefault<T>` is private).
Every call site, by T:

| T | call sites (file:line → key, default) | TC call | apply |
|---|---|---|---|
| `bool` | ClusterMgr.cpp:231 `ClusterServer.PacketTrace.Enable`,false; :237 `ClusterServer.RedirectDebug`,false; :271 `ClusterServer.InstanceServer`,false; NatsBus.cpp:757 `ClusterServer.AllowRemoteConsole`,false | `GetBoolDefault(key, def)` | sed |
| `std::string` | ClusterMgr.cpp:257 `ClusterServer.GameAddress`,"127.0.0.1"; :260 `ClusterServer.RedirectAddress`,""; :278 `ClusterServer.Maps`,""; :293 `ClusterServer.Zones`,""; :327 `ClusterServer.Zones`,""; NatsBus.cpp:80 `ClusterServer.AuthKey`,"" | `GetStringDefault(key, def)` | sed |
| `int32` | NatsBus.cpp:68 `ClusterServer.NodeId`,0; :91 `ClusterServer.TransportSyncInterval`,60; :98 `ClusterServer.BgCoordinatorNode`,1; :1160 `ClusterServer.MgmtStatusInterval`,5; :1161 `ClusterServer.MgmtPlayersInterval`,3; :1162 `ClusterServer.LFGMasterNode`,1; :1620 `ClusterServer.BgCoordinatorNode`,1 | `GetIntDefault(key, def)` (returns `int`; existing `static_cast<uint8/uint32>` wrappers stay) | sed |
| `uint32` | NatsBus.cpp:1158 `ClusterServer.HeartbeatInterval`,300; :1159 `ClusterServer.NodeDeadTimeout`,3000 | `GetIntDefault(key, def)` → implicit `int→uint32` on assignment to `uint32` member (legal; add `static_cast<uint32>` if you want it explicit) | sed |
| `uint16` | ClusterMgr.cpp:258 `ClusterServer.GamePort`, default = :259 nested `GetOption<uint16>("WorldServerPort", 8085)`; :261 `ClusterServer.RedirectPort`,0 | `GetIntDefault(key, def)`; nested call becomes `GetIntDefault("WorldServerPort", 8085)` (TC reads the same key: `World.cpp:752`). `int→uint16` narrowing on assignment to `_gamePort`/`_redirectPort`. | sed |
| `uint8` | ClusterMgr.cpp:262 `ClusterServer.NodeId`,1 | `GetIntDefault(key, def)` → `int→uint8` narrowing on assignment to `_nodeId` | sed |

Note: ClusterMgr.cpp:262 default is 1 and NatsBus.cpp:68 default is 0 for the same key — pre-existing inconsistency, not a port issue.

## 4. Logging / time

| AC | TC | apply | notes |
|---|---|---|---|
| `getMSTime()` (`Timer.h`) | `getMSTime()` | none | `common/Time/Timer.h:33 inline uint32 getMSTime()`. 28 uses. |
| `GameTime::GetGameTime().count()` | `GameTime::GetGameTime()` (returns `time_t`) | sed | PlayerTransfer.cpp:171. `server/game/Time/GameTime.h:32 TC_GAME_API time_t GetGameTime();` — AC returns `Seconds`. |
| `GameTime::GetGameTimeMS().count()` | `GameTime::GetGameTimeMS()` (returns `uint32`) | sed | PlayerTransfer.cpp:274, NatsBus.cpp:3124. `GameTime.h:35 TC_GAME_API uint32 GetGameTimeMS();` — AC returns `Milliseconds`. |

## 5. World / session manager / cross-thread marshalling

| AC | TC | apply | notes |
|---|---|---|---|
| `sWorldSessionMgr->GetActiveSessionCount()` | `sWorld->GetActiveSessionCount()` | sed | NatsBus.cpp:1632. `World/World.h:584`. |
| `sWorldSessionMgr->GetPlayerCount()` | `sWorld->GetPlayerCount()` | sed | NatsBus.cpp:1756. `World.h:590`. |
| `sWorldSessionMgr->GetPlayerAmountLimit()` | `sWorld->GetPlayerAmountLimit()` | sed | NatsBus.cpp:1757. `World.h:615`. |
| `sWorldSessionMgr->DoForAllOnlinePlayers(std::function<void(Player*)>)` | **NO TC EQUIVALENT** — iterate `sWorld->GetAllSessions()` (`World.h:582`, public, returns `SessionMap const&` = `std::unordered_map<uint32, WorldSession*>`, `World.h:554`): `for (auto const& [id, sess] : sWorld->GetAllSessions()) if (Player* p = sess->GetPlayer()) f(p);` | manual §B.2 | NatsBus.cpp:1841. `m_sessions` itself is private (`World.h:815`) but the const accessor is public. Not mutex-protected in TC — world-thread only (SendMgmtPlayers is called from `Update()`, which is world-thread, so OK). Alternative with the same thread constraint: `ObjectAccessor::GetPlayers()` under `HashMapHolder<Player>::GetLock()` (already used at NatsBus.cpp:2830-2831). |
| `sWorld->QueueCallback(std::function<void()>)` | **NO TC EQUIVALENT** | manual §B.3 | 15 sites: NatsBus.cpp:530,586,994,1022,2228,2276,2304,2450,2466,2488,2751,2823,2907,3016,3094. AC impl (`c9core World.h:237,321`): `LockedQueue<std::function<void()>> _callbackQueue; void QueueCallback(std::function<void()> cb) { _callbackQueue.add(std::move(cb)); }` drained in `World::Update`. TC already has the identical pattern for CLI: `World.h:752 QueueCliCommand` / `:852 LockedQueue<CliCommandHolder*> cliCmdQueue` / `ProcessCliCommands()`, and `LockedQueue.h` is included (`World.h:28`). Port = add the queue + accessor + a drain in `World::Update` right after `ProcessCliCommands()`. |
| `sWorld->QueuePlayerDeactivation(uint64 guid)` | **NO TC EQUIVALENT** (c9core `World.h:260`) | manual §B.4 | NatsBus.cpp:875. Requires the c9core "deactivate local player when another node claims him" path; out of scope of these 20 files but must exist or the call must be stubbed. |
| `sWorld->QueueCliCommand(CliCommandHolder*)` | same | none | NatsBus.cpp:1477. `World.h:752`. |
| `CliCommandHolder(void* arg, char const* cmd, Print, CommandFinished)` with `Print = void(*)(void*, std::string_view)`, `CommandFinished = void(*)(void*, bool)` | same | none | NatsBus.cpp:1451-1479. `World.h:536-546` identical. |
| `sWorld->FindSession(uint32)` / `AddSession(WorldSession*)` | same | none | Not called by these files; listed as known-good (`World.h:576-577`). |

## 6. WorldSession / WorldSocket

| AC | TC | apply | notes |
|---|---|---|---|
| `session->GetPlayer()`, `GetAccountId()`, `SendPacket(WorldPacket const*)` | same | none | `Server/WorldSession.h:567,565,541`. |
| `session->KickPlayer(std::string const& reason)` | same | none | NatsBus.cpp:1004. `WorldSession.h:599`. |
| `session->GetRemoteAddress()` | same | none | Not called here but needed by the caller that fills `PendingRedirect::clientIp`; `WorldSession.h:572`. |
| `session->GetSessionKey()` → `SessionKey const&` | **NO TC EQUIVALENT** | manual §B.5 | ClientRedirect.cpp:72. TC's `WorldSession` never stores the key: `WorldSocket.cpp:482 _authCrypt.Init(account.SessionKey)` and `:618 _worldSession->InitWarden(account.SessionKey, account.OS)` are the only consumers. c9core added `SessionKey _clusterSessionKey` + getter (`c9core WorldSession.h:528`). Port = add member + `SetSessionKey()` called from `WorldSocket::HandleAuthSessionCallback` next to :618, plus the getter. |
| `session->SetRedirectPending()` | **NO TC EQUIVALENT** (c9core `WorldSession.h:456`, `m_redirectPending`) | manual §B.6 | ClientRedirect.cpp:199. Suppresses `SMSG_DESTROY_OBJECT` after redirect. |
| `session->IsRedirectedOut()` | **NO TC EQUIVALENT** (c9core `WorldSession.h:545`, `_redirectedOut`) | manual §B.6 | NatsBus.cpp:1003. "Skip full logout for a session whose player was redirected away." |
| `WorldSession` ctor (for reference, not called here) | `WorldSession(uint32 id, std::string&& name, std::shared_ptr<WorldSocket> sock, AccountTypes sec, uint8 expansion, time_t mute_time, Minutes timezoneOffset, LocaleConstant locale, uint32 recruiter, bool isARecruiter)` | — | `WorldSession.h:525-526`. The destination-node "reconnect after SMSG_CONNECT_TO" code (outside these files) must construct this. |
| Player save/load (reference) | `Player::SaveToDB(bool create=false)` / `SaveToDB(CharacterDatabaseTransaction, bool)` (`Player.h:1364-1365`); `Player::LoadFromDB(ObjectGuid, CharacterDatabaseQueryHolder const&)` (`:1351`); `WorldSession::HandlePlayerLogin(LoginQueryHolder const&)` (`WorldSession.h:748`) | — | NatsBus.h comment refers to `HandlePlayerLoginFromDB` (c9core) — TC's equivalent hook is `HandlePlayerLogin`. |

## 7. Opcodes / packets

| AC | TC | apply | notes |
|---|---|---|---|
| `SMSG_REDIRECT_CLIENT` (0x50D) | `SMSG_CONNECT_TO` (0x50D) | sed | 14 uses (ClientRedirect.cpp:186, PacketTrace.cpp:76, comments). `Opcodes.h:1322`. |
| `CMSG_REDIRECTION_FAILED` (0x50E) | `CMSG_CONNECT_TO_FAILED` (0x50E) | sed | PacketTrace.cpp:81. `Opcodes.h:1323`. Values verified equal on both sides. |
| `CMSG_REDIRECTION_AUTH_PROOF` (0x512) | `CMSG_AUTH_CONTINUED_SESSION` (0x512) | sed | PacketTrace.cpp:80. `Opcodes.h:1327`. |
| `SMSG_SUSPEND_COMMS` (0x50F), `CMSG_SUSPEND_COMMS_ACK` (0x510), `SMSG_RESUME_COMMS` (0x511) | same | none | `Opcodes.h:1324-1326`. TC registers `CMSG_SUSPEND_COMMS_ACK` as `STATUS_NEVER / Handle_NULL` (`Opcodes.cpp:1427`) — the ack will be dropped unless a handler is added (outside these files). |
| `SMSG_NEW_WORLD`, `SMSG_LOGIN_VERIFY_WORLD`, `SMSG_AUTH_RESPONSE`, `CMSG_PLAYER_LOGIN`, `SMSG_GROUP_INVITE`, `SMSG_MESSAGECHAT`, `SMSG_BATTLEFIELD_STATUS` | same | none | `Opcodes.h:91,595,523,90,140,179,753`. |
| `GetOpcodeNameForLogging(static_cast<OpcodeClient>(x))` | `GetOpcodeNameForLogging(static_cast<Opcodes>(x))` | sed | PacketTrace.cpp:31. TC: `std::string GetOpcodeNameForLogging(Opcodes opcode)` (`Opcodes.h:1430`); single `enum Opcodes : uint16` (`:28`), no `OpcodeClient`/`OpcodeServer` split. Doc comments in PacketTrace.h:334 mention OpcodeServer/OpcodeClient — cosmetic. |
| `WorldPacket(opcode, size)`, `operator<<`, `append(ptr,len)`, `size()`, `contents()`, `GetOpcode()` | same | none | `WorldPacket.h:34 WorldPacket(uint16 opcode, size_t res = 200)`; `Opcodes : uint16` converts implicitly. `GetOpcode()` returns `uint16`. |

## 8. ObjectAccessor / HashMapHolder / ObjectGuid

| AC | TC | apply | notes |
|---|---|---|---|
| `ObjectAccessor::FindPlayer(ObjectGuid)` | `ObjectAccessor::FindPlayer(ObjectGuid const&)` | none | NatsBus.cpp:872,2230,2278. `Globals/ObjectAccessor.h:76`. |
| `ObjectAccessor::FindConnectedPlayer(ObjectGuid)` | same | none | NatsBus.cpp:1002,2310,2753,2927,3129. `ObjectAccessor.h:81`. |
| `ObjectAccessor::GetPlayers()` → `HashMapHolder<Player>::MapType const&` | same | none | NatsBus.cpp:2831. `ObjectAccessor.h:85`. |
| `HashMapHolder<Player>::GetLock()` → `std::shared_mutex*` | same | none | NatsBus.cpp:2830. `ObjectAccessor.h:55`. |
| `HashMapHolder<MotionTransport>::{GetContainer,GetLock,Find}` | `HashMapHolder<Transport>::{GetContainer,GetLock,Find}` | sed | NatsBus.cpp:1295-1296,1381-1382,1422. TC instantiates exactly `HashMapHolder<Player>` and `HashMapHolder<Transport>` (`ObjectAccessor.cpp:81-82`); transports are inserted by `TransportMgr.cpp:426 HashMapHolder<Transport>::Insert(trans)`. `GetContainer()` returns `std::unordered_map<ObjectGuid, T*>&` (`ObjectAccessor.h:45,53`). |
| `.GetRawValue()`, `.GetCounter()`, `ObjectGuid::Empty` | same | none | `ObjectGuid.h:148 GetRawValue`, `:140 Empty`. |
| `ObjectGuid(uint64 raw)` | `GuidFromRaw(raw)` (new inline helper in `ClusterMgr.h`: default-construct + `SetRawValue`) | manual (Task 6) | TC's `explicit ObjectGuid(uint64)` is **private** (`ObjectGuid.h:254`, only reachable via `Create<>`). NatsBus.cpp:875,1005,2219 (Task 6 line numbers). Hook tasks that port AC `ObjectGuid(uint64)` calls should use `GuidFromRaw` too. |
| `ObjectGuid(HighGuid::Mo_Transport, low)` | `ObjectGuid::Create<HighGuid::Mo_Transport>(low)` | manual (Task 6) | No `(HighGuid, LowType)` ctor in TC; `Create<>` is the public factory (`ObjectGuid.h:224`; TC uses it at `Transport.cpp:60`). NatsBus.cpp:1424. |

## 9. Transport

TC has one class `Transport : public GameObject, public TransportBase` (`Entities/Transport/Transport.h:27`); AC's `MotionTransport`/`StaticTransport` split does not exist.

| AC | TC | apply | notes |
|---|---|---|---|
| `MotionTransport` (type name) | `Transport` | sed | NatsBus.cpp:1295,1296,1381,1382,1422; comments in NatsBus.h. |
| `trans->GetPathProgress()` (AC `Transport.h:43` = `GetGOValue()->Transport.PathProgress`) | `trans->GetTimer()` | sed | NatsBus.cpp:1307,1394,1430. TC `Transport.h:84 uint32 GetTimer() const { return GetGOValue()->Transport.PathProgress; }` — same field. |
| `trans->GetPeriod()` (AC `Transport.h:81` = `GetUInt32Value(GAMEOBJECT_LEVEL)`) | `trans->GetTransportPeriod()` | sed | NatsBus.cpp:1426. TC `Transport.h:82 uint32 GetTransportPeriod() const override { return GetUInt32Value(GAMEOBJECT_LEVEL); }` — same field. |
| `trans->InitializeToTime(uint32)` (AC `Transport.h:90`) | **NO TC EQUIVALENT** | manual §B.7 | NatsBus.cpp:1441. TC writes `m_goValue.Transport.PathProgress` only inside `Transport::Update` (`Transport.cpp:134,160-162`); no public setter. Needs a new `Transport::SetTransportTime(uint32)` that sets `m_goValue.Transport.PathProgress` and re-resolves `_currentFrame`/`_nextFrame` (mirror the logic in `Transport::Update`). |
| `player->GetTransport()` → `Transport*` | same | none | PlayerTransfer.cpp:122. TC `Object.h:566 Transport* GetTransport() const`. |
| `player->GetTransOffsetX/Y/Z/O()` | same | none | `Object.h:567ff`. |
| `transport->GetEntry()` | same | none | GameObject. |

## 10. Player / Unit / Pet / Item / Aura / Spell

| AC | TC | apply | notes |
|---|---|---|---|
| `player->getPowerType()` | `player->GetPowerType()` | sed | PlayerTransfer.cpp:115, NatsBus.cpp:2641. `Unit.h:945`. |
| `player->getClass()` / `getRace()` | `GetClass()` / `GetRace()` | sed | NatsBus.cpp:1853-1854, 2111-2112. `Unit.h:909,906`. |
| `GetLevel()`, `GetHealth()`, `GetMaxHealth()`, `GetPower(Powers)`, `GetMaxPower(Powers)` | same | none | `Unit.h:903,927,928,948,950`. TC `GetPower`/`GetMaxPower` return `uint32` (AC same); `GetLevel` returns `uint8`. |
| `GetGUID()`, `GetName()`, `GetMapId()`, `GetZoneId()`, `GetAreaId()`, `GetPositionX/Y/Z()`, `GetOrientation()`, `IsInWorld()`, `GetSession()` | same | none | `Object.h:375-376,384`; `Player.h:1698`. |
| `player->HasPlayerFlag(PLAYER_FLAGS_GHOST)` | `player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)` | sed | NatsBus.cpp:2634. No `HasPlayerFlag` in TC `Player.h`; pattern per `Player.h:947` / `Player.cpp:799`. `PLAYER_FLAGS_GHOST` = `Player.h:350`. |
| `IsPvP()`, `IsAlive()`, `isAFK()`, `isDND()` | same | none | `Unit.h:1002,1247`; `Player.h:947-948` (lower-case `isAFK/isDND` retained in TC). |
| `GetTeamId()` → `TeamId` | same | none | `Player.h:1812`. `TEAM_ALLIANCE` = `server/shared/SharedDefines.h:846`. |
| `GetAuraUpdateMaskForRaid()`, `MAX_AURAS_GROUP_UPDATE` | same | none | `Player.h:2159`; `Spells/Auras/SpellAuraDefines.h:29` (=64). |
| `GetVisibleAura(i)` → `AuraApplication*` | same | none | NatsBus.cpp:2655. `Unit.h:1588 AuraApplication* GetVisibleAura(uint8 slot) const`. |
| `GetAppliedAuras()`, `AuraApplication::GetBase()`, `Aura::{IsPassive,GetSpellInfo,GetId,GetDuration,GetMaxDuration,GetStackAmount,GetCasterGUID}` | same | none | `Unit.h:1351-1352`; `SpellAuras.h:75,203,140,141,173,168,189,144`. |
| `SpellInfo::HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY)` | `HasAttribute(SPELL_ATTR0_HIDDEN_CLIENTSIDE)` | sed | PlayerTransfer.cpp:158. `SharedDefines.h:427` (0x80, same bit). |
| `Powers`, `POWER_MANA`, `POWER_HAPPINESS` | same | none | `SharedDefines.h:301-308`. |
| `GetPet()`, `pet->GetEntry/GetDisplayId/GetLevel/GetHealth/GetPower/GetName()` | same | none | `Player.h:1043`; `Unit.h:1598`. |
| `pet->GetReactState()` | same | none | `Creature.h:129` (Pet → Guardian → … → Creature). |
| `player->GetActiveSpec()` | `player->GetActiveTalentGroup()` | sed | PlayerTransfer.cpp:186. `Player.h:1474 uint8 GetActiveTalentGroup() const`. |
| `const_cast<Player*>(player)->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)` | `player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)` (const) | sed | PlayerTransfer.cpp:191. `Player.h:1079 Item* GetItemByPos(uint8 bag, uint8 slot) const`. `INVENTORY_SLOT_BAG_0`=255 (`Player.h:549`), `EQUIPMENT_SLOT_END`=19 (`:573`). |
| `item->GetEntry()`, `GetItemRandomPropertyId()`, `GetEnchantmentId(PERM/TEMP/SOCK_ENCHANTMENT_SLOT)` | same | none | `Item.h:142,150`. |
| `player->GetSpellMap()` → `PlayerSpellMap const&` | same name; **value type differs**: TC `std::unordered_map<uint32, PlayerSpell>` (by value, `Player.h:180`) vs AC `uint32→PlayerSpell*` | manual §B.8 | PlayerTransfer.cpp:205-213. Loop var `spell` is a `PlayerSpell const&`, not a pointer. |
| `PlayerSpell::State` / `::Active` / `::specMask` | `PlayerSpell::state` / `::active` / **NO `specMask`** | manual §B.8 | `Player.h:157-163`: `{ PlayerSpellState state; bool active:1; bool dependent:1; bool disabled:1; }`. TC 3.3.5 has no per-spec spell mask (dual spec only affects talents/action bars). `PLAYERSPELL_REMOVED` exists (`Player.h:153`). |
| `player->GetTalentMap()` (AC: no-arg, `uint32→PlayerTalent*` with `talentID`, `specMask`, `State`) | `player->GetTalentMap(uint8 group)` → `PlayerTalentMap const*` = `std::unordered_map<uint32 /*spellId*/, PlayerSpellState>` | manual §B.9 | PlayerTransfer.cpp:217-226. `Player.h:179,1504-1505`. TC has no `PlayerTalent` struct: key is the talent *spell id*, value is the state; talent id is not stored (look up via `sTalentStore`/`GetTalentSpellPos` if needed). Must loop `for (uint8 g = 0; g < MAX_TALENT_SPECS; ++g)` and set `specMask = 1 << g`. |
| `MAX_TALENT_SPECS` (§B.9 text) | `MAX_TALENT_GROUPS` (=2) | manual (Task 6) | TC has no `MAX_TALENT_SPECS`; `SharedDefines.h:716 #define MAX_TALENT_GROUPS 2` sizes `PlayerTalentInfo::GroupInfo[]` (`Player.h:882`). §B.9's loop bound is corrected to this. |
| `GetSkillTempBonusByPos(pos)` return type | `int16` (others `uint16`) | manual (Task 6) | `Player.h:1797`. `TransferSkillInfo::bonusTemp` is `uint16` → explicit `uint16(...)` cast in PlayerTransfer.cpp (wire bits unchanged). |
| `player->GetActionButtons()` → `ActionButtonList const&`, `ab.uState`, `ab.GetAction()`, `ab.GetType()`, `ACTIONBUTTON_DELETED` | same | none | `Player.h:1506,216-229,240,188`. Note TC's list is the **active** spec's buttons only. |
| `player->GetSkillStatusMap()` (AC `Player.h:1808`) | **NO TC EQUIVALENT** — `mSkillStatus` is `protected` (`Player.h:2244,2378`) | manual §B.10 | PlayerTransfer.cpp:241-253. Add `SkillStatusMap const& GetSkillStatusMap() const { return mSkillStatus; }` to TC `Player` (typedef exists: `Player.h:533`), or make the snapshotter a friend. `SkillStatusData{uint8 pos; SkillUpdateState uState;}` and `SKILL_DELETED` match (`Player.h:524-531,521`). |
| `GetUInt32Value(PLAYER_SKILL_VALUE_INDEX(f)) & 0xFFFF` etc. | **NO TC EQUIVALENT macros** → `GetSkillRankByPos(pos)`, `GetSkillMaxRankByPos(pos)`, `GetSkillTempBonusByPos(pos)`, `GetSkillPermBonusByPos(pos)` | manual §B.10 | PlayerTransfer.cpp:248-251. `Player.h:1788,1790,1792,1794` (uint16-field accessors over `PLAYER_SKILL_INFO_1_1 + pos*3`). |
| `player->getQuestStatusMap()` → `QuestStatusMap&`, `QuestStatusData::{Status,Explored,Timer,CreatureOrGOCount[4],ItemCount[6],PlayerCount}`, `QUEST_STATUS_NONE` | same | none | `Player.h:1393,486`; `Quests/QuestDef.h:425-433`. Non-const accessor only — `SnapshotPlayerFull(Player const*)` needs `const_cast` or a const overload (manual §B.11). |
| `player->GetSpellCooldownMap()` (AC `Player.h:1805`, `SpellCooldown{end,category,itemid}`) | **NO TC EQUIVALENT** — cooldowns live in `SpellHistory` (`player->GetSpellHistory()`); storage `_spellCooldowns` is private (`SpellHistory.h:145,158`), entries are `CooldownEntry{uint32 SpellId; Clock::time_point CooldownEnd; uint32 ItemId; uint32 CategoryId; Clock::time_point CategoryEnd; bool OnHold;}` (`:53-61`) | manual §B.12 | PlayerTransfer.cpp:274-285. Add `CooldownStorageType const& GetCooldowns() const { return _spellCooldowns; }` to `SpellHistory` (only size accessor exists today: `:141`). Convert `CooldownEnd - Clock::now()` to ms. |
| `player->getRewardedQuests()` → `RewardedQuestSet const&` | same | none | `Player.h:1392,487`. |
| `player->SetGroupUpdateFlag(GROUP_UPDATE_FULL)` | same | none | `Player.h:2158`; `Group.h:122`. |
| `player->SendDirectMessage(WorldPacket const*)`, `SendNewMail()`, `TeleportTo(map,x,y,z,o)` | same | none | `Player.h:1943,1405,914`. |
| `MEMBER_STATUS_{ONLINE,PVP,DEAD,GHOST,AFK,DND}` | same | none | `Group.h:61-69`. |
| `WorldLocation::{GetMapId,GetPositionX/Y/Z,GetOrientation}` | same | none | `Position.h:167,193`. |

## 11. Group / Social / LFG / Arena / Battleground

| AC | TC | apply | notes |
|---|---|---|---|
| `sSocialMgr->NotifyRemoteFriendOnline(ObjectGuid const&, uint32 areaId, uint8 level, uint8 classId)` / `NotifyRemoteFriendOffline(ObjectGuid const&)` | **NO TC EQUIVALENT** (c9core `SocialMgr.h:142-143`) | manual §B.13 | NatsBus.cpp:1007,1024. TC only has `SendFriendStatus(Player* player, FriendsResult, ObjectGuid const& friendGuid, bool broadcast)` (`SocialMgr.h:148`) which needs the *friend's* `Player*`. Port = add the two methods: iterate `sWorld->GetAllSessions()`, for each local player whose `PlayerSocial` has `remoteGuid` as friend, build `SMSG_FRIEND_STATUS` with `FRIEND_ONLINE/OFFLINE` and status fields from the args. |
| `sSocialMgr->NotifyRemoteFriendOnline/Offline` call sites in `HandleRemotePlayerOnline/Offline` (phase 1) | `TC_LOG_DEBUG(... "ignored (phase 2 feature not ported)")` | stub (Task 6) | These two handlers stay live (ghost-session kick, ClusterMgr/cache bookkeeping); only the friend-notify calls are stubbed, so §B.13 is deferred to phase 2 along with the other social/LFG/arena/BG handlers. |
| `lfg::LfgDungeonSet` | same | none | `DungeonFinding/LFG.h:27 namespace lfg`, `:102 typedef std::set<uint32> LfgDungeonSet`. |
| `sLFGMgr->JoinLfgByData(ObjectGuid, uint8 roles, LfgDungeonSet const&, uint8 teamId)` | **NO TC EQUIVALENT** (c9core `LFGMgr.h:552`). TC: `JoinLfg(Player* player, uint8 roles, LfgDungeonSet& dungeons, std::string const& comment)` (`LFGMgr.h:407`) — needs a live `Player*`, which does not exist on the master node for a remote player | manual §B.14 | NatsBus.cpp:2452. Needs a guid/team-based join entry point added to TC `LFGMgr` (port c9core's). |
| `sLFGMgr->LeaveLfg(ObjectGuid)` | `LeaveLfg(ObjectGuid guid, bool disconnected = false)` | none | NatsBus.cpp:2469. `LFGMgr.h:409`. |
| `sLFGMgr->LeaveAllLfgQueues(ObjectGuid, bool allowgroup, ObjectGuid groupguid)` | **NO TC EQUIVALENT** (c9core `LFGMgr.h:556`; TC has single-queue LFG) | manual §B.14 | NatsBus.cpp:2470. Drop the call (TC `LeaveLfg` already removes from the only queue). |
| `sLFGMgr->UpdateProposal(uint32, ObjectGuid, bool)` | same | none | NatsBus.cpp:2490. `LFGMgr.h:399`. |
| `sArenaTeamMgr->GetArenaTeamById(uint32)` | same | none | `ArenaTeamMgr.h:35`. |
| `ArenaTeamStats{Rating,WeekGames,WeekWins,SeasonGames,SeasonWins,Rank}` | same | none | `ArenaTeam.h:104-112`. |
| `team->SetArenaTeamStats(ArenaTeamStats&)` | **NO TC EQUIVALENT** (c9core `ArenaTeam.h:156`); TC `Stats` is `protected` (`:190,204`), only `GetStats() const` (`:135`) | manual §B.15 | NatsBus.cpp:2921. Add `void SetStats(ArenaTeamStats const& s) { Stats = s; }` to TC `ArenaTeam` (public). |
| `team->GetMembers()` → `MemberList const&` | **NO TC EQUIVALENT** — TC exposes `m_membersBegin()` / `m_membersEnd()` (`ArenaTeam.h:147-148`, non-const iterators) | manual §B.15 | NatsBus.cpp:2924-2925. Replace with `for (auto itr = team->m_membersBegin(); itr != team->m_membersEnd(); ++itr)`. `ArenaTeam::MemberList` = `std::list<ArenaTeamMember>` (`:126`), `ArenaTeamMember::Guid` (`:90`). |
| `team->SendStats(WorldSession*)` | same | none | `ArenaTeam.h:169`. |
| `BattlegroundTypeId`, `BattlegroundBracketId`, `PvPDifficultyEntry` | same | none | `SharedDefines.h:3592`; `DBCStructure.h`. |
| `sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId)` | same | none | NatsBus.cpp:3020,3102. `BattlegroundMgr.h:111`. |
| `sBattlegroundMgr->CreateNewBattleground(BattlegroundTypeId, PvPDifficultyEntry const*, uint8 arenaType, bool isRated)` | same | none | NatsBus.cpp:3036. `BattlegroundMgr.h:112`. |
| `sBattlegroundMgr->AddBattleground(Battleground*)` | same (public) | none | NatsBus.cpp:3043. `BattlegroundMgr.h:114` (public section starts :86). |
| `BattlegroundMgr::BGQueueTypeId(bgType, arenaType)` (2 args) | `BattlegroundMgr::BGQueueTypeId(BattlegroundTypeId bgTypeId, uint8 bracketId, uint8 arenaType)` (3 args) | manual §B.16 | NatsBus.cpp:3098. `BattlegroundMgr.h:140`. TC's `BattlegroundQueueTypeId` is a **struct** `{uint16 BattlemasterListId; uint8 BracketId; uint8 TeamSize;}` (`SharedDefines.h:3755-3779`), so the bracket is part of the queue identity; pass the wire `bracketId`. |
| `BATTLEGROUND_QUEUE_NONE` | same (`constexpr BattlegroundQueueTypeId BATTLEGROUND_QUEUE_NONE = {0,0,0}`) | none | NatsBus.cpp:3099. `SharedDefines.h:3779`. `==` works if the struct has `operator==` (TC uses it in `Player::GetBattlegroundQueueIndex`); verify at compile. |
| `sBattlegroundMgr->GetBattlegroundQueue(BattlegroundQueueTypeId)` → `BattlegroundQueue&` | same | none | `BattlegroundMgr.h:126`. |
| `bgQueue.GetPlayerGroupInfoData(ObjectGuid, GroupQueueInfo*)` | same | none | NatsBus.cpp:3140. `BattlegroundQueue.h:87`. |
| `player->SetInviteForBattlegroundQueueType(BattlegroundQueueTypeId, uint32 instanceId)`, `GetBattlegroundQueueIndex(BattlegroundQueueTypeId)`, `PLAYER_MAX_BATTLEGROUND_QUEUES` | same | none | `Player.h:1980,1972,2266`. |
| `BGQueueRemoveEvent(ObjectGuid, uint32 instanceId, BattlegroundTypeId, BattlegroundQueueTypeId, uint32 removeTime)` (5 args) | `BGQueueRemoveEvent(ObjectGuid pl_guid, uint32 bgInstanceGUID, BattlegroundQueueTypeId bgQueueTypeId, uint32 removeTime)` (4 args — no `BattlegroundTypeId`) | manual §B.16 | NatsBus.cpp:3172-3173. `BattlegroundQueue.h:171`. |
| `bgQueue.AddEvent(BasicEvent*, uint64)` (c9core `BattlegroundQueue.h:95`) | **NO TC EQUIVALENT** — `m_events` is private (`BattlegroundQueue.h:128,138`); TC itself does `m_events.AddEvent(removeEvent, m_events.CalculateTime(Milliseconds(INVITE_ACCEPT_WAIT_TIME)))` (`BattlegroundQueue.cpp:461`) | manual §B.16 | NatsBus.cpp:3174. Add a public `void AddEvent(BasicEvent* e, Milliseconds delay) { m_events.AddEvent(e, m_events.CalculateTime(delay)); }` to TC `BattlegroundQueue`. |
| `Battleground::{GetInstanceID,GetClientInstanceID,GetMapId,GetMinLevel,GetMaxLevel}`, `STATUS_WAIT_JOIN` | same | none | `Battleground.h:277,279,364,286,287,186`. |
| `SMSG_BATTLEFIELD_STATUS` manual layout (NatsBus.cpp:3154-3167) | same wire format (3.3.5) | none | TC builds it in `BattlegroundMgr::BuildBattlegroundStatusPacket`; hand-rolled layout here is client-defined and unchanged. |
| `SMSG_GROUP_INVITE` manual layout (NatsBus.cpp:2236-2242) | same | none | |

## 12. DBC stores

| AC | TC | apply | notes |
|---|---|---|---|
| `sMapStore.LookupEntry(id)` → `MapEntry const*` | same | none | ClusterMgr.cpp:339, NatsBus.cpp:2281. `DBCStores.h:158`; `DBCStore.h:68`. |
| `MapEntry::Instanceable()` | same | none | `DBCStructure.h:1091`. |
| `mapEntry->MapID` | `mapEntry->ID` | sed | ClusterMgr.cpp:652. `DBCStructure.h:1065`. |
| `for (auto const* e : sMapStore)` | same | none | ClusterMgr.cpp:650. `DBCStore.h:88-89 begin()/end()` (non-const — `sMapStore` is a non-const global), `DBCStorageIterator.h:45 T const* operator*()`. |
| `sAreaTableStore.LookupEntry(zoneId)` | same | none | ClusterMgr.cpp:408. `DBCStores.h:92`. |
| `area->mapid` | `area->ContinentID` | sed | ClusterMgr.cpp:411. `DBCStructure.h:179`. |
| `GetBattlegroundBracketById(uint32 mapid, BattlegroundBracketId)` | same | none | NatsBus.cpp:3027. `DBCStores.h:71`. |

## 13. Database

| AC | TC | apply | notes |
|---|---|---|---|
| `CharacterDatabasePreparedStatement*` | same | none | NatsBus.cpp:1564. TC: `CharacterDatabaseConnection::Statements` / `PreparedStatement<CharacterDatabaseConnection>`; the alias `CharacterDatabasePreparedStatement` is defined in `DatabaseEnvFwd.h` (TC uses it throughout `Player.cpp`). |
| `CharacterDatabase.GetPreparedStatement(enum)`, `CharacterDatabase.Execute(stmt)` | same | none | `DatabaseEnv.h:37`. |
| `stmt->SetData(0, deadNodeId)` | `stmt->setUInt8(0, deadNodeId)` | sed | NatsBus.cpp:1565. `PreparedStatement.h:71 void setUInt8(uint8 index, uint8 value)`. (AC's generic `SetData<T>` → TC typed setters: `setUInt8/16/32/64`, `setInt*`, `setFloat`, `setString`, `setBool`, `setNull`.) |
| `CHAR_UPD_NODE_ONLINE_CLEANUP` | **NO TC EQUIVALENT** | manual §B.17 | NatsBus.cpp:1564. c9core: `CharacterDatabase.cpp:386 PrepareStatement(CHAR_UPD_NODE_ONLINE_CLEANUP, "UPDATE characters SET online = 0, owning_node_id = 0 WHERE owning_node_id = ?", CONNECTION_ASYNC)`. Requires (a) the enum value in `server/database/Database/Implementation/CharacterDatabase.h` (`CharacterDatabaseStatements`), (b) the `PrepareStatement` line in `CharacterDatabase.cpp`, (c) a characters-DB SQL update adding `characters.owning_node_id TINYINT UNSIGNED NOT NULL DEFAULT 0` (+ index). Nearest existing TC statement: `CHAR_UPD_ACCOUNT_ONLINE` = `UPDATE characters SET online = 0 WHERE account = ?` (`CharacterDatabase.cpp:360`). |

## 14. Addon info

| AC | TC | apply | notes |
|---|---|---|---|
| `AddonInfo` (`c9core AddonMgr.h:26`: `AddonInfo(std::string name, uint8 enabled, uint32 crc, uint8 state, bool crcOrPubKey)`; fields `Name, Enabled, CRC, State, UsePublicKeyOrCRC`) | **NO TC EQUIVALENT**. TC `WorldSession.h:507-519`: `struct SecureAddonInfo { enum SecureAddonStatus : uint8 { BANNED=0, SECURE_VISIBLE=1, SECURE_HIDDEN=2 }; std::string Name; SecureAddonStatus Status = BANNED; bool HasKey = false; }`, held in private `WorldSession::_addons.SecureAddons` (`std::vector<SecureAddonInfo>`, `:1343-1348`), filled by `ReadAddonsInfo(ByteBuffer&)` and consumed by `SendAddonsInfo()` (`:535-536`). `AddonMgr.h` only has `SavedAddon{Name, CRC}` / `BannedAddon`. | manual §B.18 | ClusterMgr.h:280 `std::list<AddonInfo> addons`; NatsBus.h:282 param; NatsBus.cpp:927 `emplace_back(name, enabled, crc, state, usePK)`, :1125-1133 field reads. Recommended: keep the cluster's own wire struct (rename to `ClusterAddonInfo` in `ClusterMgr.h` with the same 5 fields), and add to TC `WorldSession` a getter `std::vector<SecureAddonInfo> const& GetSecureAddons() const` + a setter `SetSecureAddons(std::vector<SecureAddonInfo>)` used on the destination node (so `SendAddonsInfo()` works after a redirect without a fresh `CMSG_AUTH_SESSION` addon blob). Map `Enabled`→(Status != BANNED), `State`→Status, `UsePublicKeyOrCRC`→HasKey, `CRC`→carry (TC does not store it per session; 0 is acceptable since `SendAddonsInfo` only checks `STANDARD_ADDON_CRC` via `SavedAddon`). |

## 15. Misc

| AC | TC | apply | notes |
|---|---|---|---|
| `std::transform(..., ::tolower)` | same | none | NatsBus.cpp:2106; add `<cctype>`/`<algorithm>` (NatsBus.cpp lacks `<algorithm>` include — `std::min`, `std::find_if` also used; TC headers pull it in transitively but add it). |
| `/proc/self/{status,stat}`, `::getpid()`, `<unistd.h>` | same (Linux-only) | none | NatsBus.cpp:1680-1748. Already non-portable in AC; TC builds on Windows will fail — wrap in `#ifndef _WIN32` (manual §B.19, optional). |

---

## A. Mechanical renames — sed block

Safe to run over the copied files in `docs/superpowers/port/ac-new/`. Each rule touches only the exact AC identifier.
Run from the TC tree root. GNU sed.

```bash
set -euo pipefail
D=/home/wow/source/Trinitycore/docs/superpowers/port/ac-new
FILES=$(find "$D" -type f \( -name '*.cpp' -o -name '*.h' \))

# --- logging -----------------------------------------------------------------
sed -i -E 's/\bLOG_(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\(/TC_LOG_\1(/g' $FILES

# --- namespaces / guards -----------------------------------------------------
sed -i -E 's/\bAcore::PacketTrace\b/Trinity::PacketTrace/g' $FILES
sed -i -E 's/\bC9CORE_PACKETTRACE_H\b/TRINITY_PACKETTRACE_H/g' $FILES

# --- include paths -----------------------------------------------------------
sed -i -E 's|#include "Entities/Player/Player.h"|#include "Player.h"|; s|#include "Entities/Transport/Transport.h"|#include "Transport.h"|; s|#include "Globals/ObjectAccessor.h"|#include "ObjectAccessor.h"|; s|#include "Groups/Group.h"|#include "Group.h"|' $FILES
sed -i -E '/#include "WorldSessionMgr.h"/d' $FILES
sed -i -E 's|#include "AddonMgr.h"|#include <list>|' $FILES          # ClusterMgr.h, NatsBus.h: keep <list>, drop AC AddonMgr
sed -i -E 's|^#include <cstring>$|#include <cstring>\n#include <cctype>\n#include <algorithm>|' "$D/src/server/game/Server/NatsBus.cpp"

# --- config ------------------------------------------------------------------
sed -i -E 's/sConfigMgr->GetOption<bool>\(/sConfigMgr->GetBoolDefault(/g' $FILES
sed -i -E 's/sConfigMgr->GetOption<std::string>\(/sConfigMgr->GetStringDefault(/g' $FILES
sed -i -E 's/sConfigMgr->GetOption<(int32|uint32|uint16|uint8)>\(/sConfigMgr->GetIntDefault(/g' $FILES

# --- time --------------------------------------------------------------------
sed -i -E 's/GameTime::GetGameTime\(\)\.count\(\)/GameTime::GetGameTime()/g; s/GameTime::GetGameTimeMS\(\)\.count\(\)/GameTime::GetGameTimeMS()/g' $FILES

# --- world / sessions --------------------------------------------------------
sed -i -E 's/sWorldSessionMgr->GetActiveSessionCount\(\)/sWorld->GetActiveSessionCount()/g; s/sWorldSessionMgr->GetPlayerCount\(\)/sWorld->GetPlayerCount()/g; s/sWorldSessionMgr->GetPlayerAmountLimit\(\)/sWorld->GetPlayerAmountLimit()/g' $FILES

# --- opcodes -----------------------------------------------------------------
sed -i -E 's/\bSMSG_REDIRECT_CLIENT\b/SMSG_CONNECT_TO/g; s/\bCMSG_REDIRECTION_FAILED\b/CMSG_CONNECT_TO_FAILED/g; s/\bCMSG_REDIRECTION_AUTH_PROOF\b/CMSG_AUTH_CONTINUED_SESSION/g' $FILES
sed -i -E 's/static_cast<OpcodeClient>\(opcode\)/static_cast<Opcodes>(opcode)/' "$D/src/server/game/Server/PacketTrace.cpp"

# --- transport ---------------------------------------------------------------
sed -i -E 's/\bHashMapHolder<MotionTransport>/HashMapHolder<Transport>/g; s/\bMotionTransport\b/Transport/g' $FILES
sed -i -E 's/->GetPathProgress\(\)/->GetTimer()/g; s/\btrans->GetPeriod\(\)/trans->GetTransportPeriod()/g' "$D/src/server/game/Server/NatsBus.cpp"

# --- player / unit -----------------------------------------------------------
sed -i -E 's/->getPowerType\(\)/->GetPowerType()/g; s/->getClass\(\)/->GetClass()/g; s/->getRace\(\)/->GetRace()/g' $FILES
sed -i -E 's/player->HasPlayerFlag\(PLAYER_FLAGS_GHOST\)/player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)/' "$D/src/server/game/Server/NatsBus.cpp"
sed -i -E 's/\bSPELL_ATTR0_DO_NOT_DISPLAY\b/SPELL_ATTR0_HIDDEN_CLIENTSIDE/g' $FILES
sed -i -E 's/player->GetActiveSpec\(\)/player->GetActiveTalentGroup()/; s/const_cast<Player\*>\(player\)->GetItemByPos\(/player->GetItemByPos(/' "$D/src/server/game/Server/PlayerTransfer.cpp"

# --- DBC ---------------------------------------------------------------------
sed -i -E 's/mapEntry->MapID\b/mapEntry->ID/; s/area->mapid\b/area->ContinentID/' "$D/src/server/game/Server/ClusterMgr.cpp"

# --- DB ----------------------------------------------------------------------
sed -i -E 's/stmt->SetData\(0, deadNodeId\)/stmt->setUInt8(0, deadNodeId)/' "$D/src/server/game/Server/NatsBus.cpp"

# --- optional house-style ----------------------------------------------------
# sed -i -E 's/\bfmt::format\(/Trinity::StringFormat(/g' "$D/src/server/game/Server/ClientRedirect.cpp"
```

After the sed block, the remaining compile errors are exactly the items in §B.

## B. Manual translations (implementer required)

1. **Export macro on shared-lib symbols** — `ClusterAuth.h:363-383` (free functions `Init/IsInitialised/Seal/Open`) and `PskCrypt.h:117` (`class PskCrypt`). If built into `src/server/shared` (a shared lib on some configs), prefix with `TC_SHARED_API` (`common/Define.h:106`). If instead compiled into `game`, use `TC_GAME_API` or nothing (static). Decide placement first.
2. **`DoForAllOnlinePlayers` → session-map loop** — `NatsBus.cpp:1841-1881`. Replace with `for (auto const& [id, sess] : sWorld->GetAllSessions()) { Player* player = sess->GetPlayer(); if (!player || !player->IsInWorld()) continue; ... }`. World-thread only (it is: called from `Update()`). Remove the "thread-safe internally via session map mutex" comment at :1830.
3. **`sWorld->QueueCallback(fn)`** — 15 sites in NatsBus.cpp (:530,586,994,1022,2228,2276,2304,2450,2466,2488,2751,2823,2907,3016,3094). Add to TC `World` (`World/World.h` next to :752/:852): `void QueueCallback(std::function<void()> cb) { _callbackQueue.add(std::move(cb)); }` + `LockedQueue<std::function<void()>> _callbackQueue;` + drain loop in `World::Update()` immediately after `ProcessCliCommands()`: `std::function<void()> cb; while (_callbackQueue.next(cb)) cb();`. Add `#include <functional>`. Keep the call sites unchanged.
4. **`sWorld->QueuePlayerDeactivation(uint64)`** — `NatsBus.cpp:875`. c9core `World.h:260`. Either port the c9core deactivation path or replace with `ghost->GetSession()->KickPlayer("claimed by node N")` as an interim (semantics differ: deactivation skips save/logout broadcast).
5. **`session->GetSessionKey()`** — `ClientRedirect.cpp:72`. Add to TC `WorldSession`: private `SessionKey _sessionKey{};`, public `void SetSessionKey(SessionKey const& k) { _sessionKey = k; }` / `SessionKey const& GetSessionKey() const { return _sessionKey; }`; call `_worldSession->SetSessionKey(account.SessionKey)` in `WorldSocket::HandleAuthSessionCallback` right after the `new WorldSession(...)` at `WorldSocket.cpp:610-613`. (`SessionKey` type from `AuthDefines.h:25`, already included by `WorldSession.h` via `InitWarden`.)
6. **`SetRedirectPending()` / `IsRedirectedOut()`** — `ClientRedirect.cpp:199`, `NatsBus.cpp:1003`. Add both flag pairs to TC `WorldSession` (c9core `WorldSession.h:455-456,544-545,1327,1352`): `bool m_redirectPending=false; bool _redirectedOut=false;` with `SetRedirectPending()/IsRedirectPending()` and `SetRedirectedOut()/IsRedirectedOut()`. Their *consumers* (suppress `SMSG_DESTROY_OBJECT`, skip full `LogoutPlayer`) live in `WorldSession.cpp`/`Player.cpp` and are outside these 20 files — port those or the flags are inert.
7. **`trans->InitializeToTime(remoteProgress)`** — `NatsBus.cpp:1441`. TC `Transport` has no setter for `m_goValue.Transport.PathProgress`. Add `void Transport::SetTransportTime(uint32 progress)` that assigns `m_goValue.Transport.PathProgress = progress`, then recomputes `_currentFrame`/`_nextFrame` the way `Transport::Update` (`Transport.cpp:134-162`) does for `timer = progress % GetTransportPeriod()`. Port c9core's `MotionTransport::InitializeToTime` body (c9core `Transport.h:90`).
8. **`GetSpellMap()` value type + `specMask`** — `PlayerTransfer.cpp:205-213`. TC map is `uint32 → PlayerSpell` by value: `for (auto const& [spellId, spell] : player->GetSpellMap()) { if (spell.state == PLAYERSPELL_REMOVED) continue; si.active = spell.active; si.specMask = 0xFF /* TC: spells are spec-independent */; }`. Keep the wire field (v2 format) but document it as always 0xFF on TC.
9. **`GetTalentMap()`** — `PlayerTransfer.cpp:217-226`. TC: `for (uint8 g = 0; g < MAX_TALENT_SPECS; ++g) for (auto const& [spellId, state] : *player->GetTalentMap(g)) { if (state == PLAYERSPELL_REMOVED) continue; ti.spellId = spellId; ti.specMask = 1 << g; ti.talentId = 0 /* or resolve via sTalentStore */; }`. **Correction (Task 6):** TC's constant is `MAX_TALENT_GROUPS` (`SharedDefines.h:716`, sizes `PlayerTalentInfo::GroupInfo[]`); there is no `MAX_TALENT_SPECS`.
10. **`GetSkillStatusMap()` + `PLAYER_SKILL_*_INDEX`** — `PlayerTransfer.cpp:241-253`. Add `SkillStatusMap const& GetSkillStatusMap() const { return mSkillStatus; }` to TC `Player` public section (member is protected: `Player.h:2244,2378`). Replace the four `GetUInt32Value(...)` reads with `player->GetSkillRankByPos(pos)`, `GetSkillMaxRankByPos(pos)`, `GetSkillTempBonusByPos(pos)`, `GetSkillPermBonusByPos(pos)` (`Player.h:1788-1794`; `pos = status.pos`).
11. **`getQuestStatusMap()` is non-const in TC** — `PlayerTransfer.cpp:256`. `SnapshotPlayerFull` takes `Player const*`; either `const_cast<Player*>(player)->getQuestStatusMap()` or add `QuestStatusMap const& getQuestStatusMap() const` overload to TC `Player` (`Player.h:1393`).
12. **`GetSpellCooldownMap()` → `SpellHistory`** — `PlayerTransfer.cpp:274-285`. Add to TC `SpellHistory` (public, `SpellHistory.h` near :141): `CooldownStorageType const& GetCooldowns() const { return _spellCooldowns; }`. Then: `auto now = SpellHistory::Clock::now(); for (auto const& [spellId, cd] : player->GetSpellHistory()->GetCooldowns()) { if (cd.CooldownEnd <= now) continue; ci.spellId = spellId; ci.endTimeMs = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(cd.CooldownEnd - now).count()); ci.categoryId = uint16(cd.CategoryId); ci.itemId = cd.ItemId; }`. Drop the `GameTime::GetGameTimeMS()`-based `now` at :274. (`CooldownEntry` layout: `SpellHistory.h:53-61`.)
13. **`sSocialMgr->NotifyRemoteFriendOnline/Offline`** — `NatsBus.cpp:1007,1024`. Add both to TC `SocialMgr` (`Entities/Player/SocialMgr.h`): signature per c9core `SocialMgr.h:142-143`. Implementation: iterate `sWorld->GetAllSessions()`; for each local `Player* p` with `p->GetSocial()->HasFriend(remoteGuid)`, build `SMSG_FRIEND_STATUS` (`FRIEND_ONLINE`/`FRIEND_OFFLINE`, guid, status, area, level, class) and `p->SendDirectMessage`. TC's `SendFriendStatus(Player*, FriendsResult, ObjectGuid const&, bool)` (`:148`) cannot be reused as-is because it dereferences the friend's `Player*` for area/level/class.
14. **LFG relay on master node** — `NatsBus.cpp:2452,2470`. (a) TC `JoinLfg` requires `Player*` (`LFGMgr.h:407`); add a `JoinLfgByData(ObjectGuid, uint8 roles, LfgDungeonSet const&, uint8 teamId)` overload to TC `LFGMgr` (port c9core `LFGMgr.h:552` — it bypasses the `Player*`-based checks and seeds `LfgPlayerData` from the args). (b) Delete the `LeaveAllLfgQueues` call at :2470 — TC has one queue; `LeaveLfg` (:2469) suffices.
15. **ArenaTeam stats/members** — `NatsBus.cpp:2921,2924-2925`. Add `void SetStats(ArenaTeamStats const& s) { Stats = s; }` to TC `ArenaTeam` (public; `Stats` is protected at `ArenaTeam.h:204`). Replace `team->SetArenaTeamStats(newStats)` with `team->SetStats(newStats)`. Replace the `GetMembers()` loop with `for (auto itr = team->m_membersBegin(); itr != team->m_membersEnd(); ++itr)` (`ArenaTeam.h:147-148`).
16. **BG queue API shape** — `NatsBus.cpp:3098,3172-3174`. (a) `BattlegroundMgr::BGQueueTypeId(bgType, 0)` → `BattlegroundMgr::BGQueueTypeId(bgType, bracketId, 0)` — the wire payload for `MSG_CLUSTER_BG_READY` (`HandleBgReady`, :3058) does **not** carry `bracketId`; add it to the payload (producer `HandleBgInstCreated` :3322-3337 has `match.bracketId` available) or derive from `GetBattlegroundBracketByLevel(mapId, player->GetLevel())->GetBracketId()`. (b) `BGQueueRemoveEvent(guid, instanceId, bgType, bgQueueTypeId, removeTime)` → drop the 3rd arg: `BGQueueRemoveEvent(player->GetGUID(), instanceId, bgQueueTypeId, removeTime)` (`BattlegroundQueue.h:171`). (c) `bgQueue.AddEvent(removeEvent, 60000u)` → add to TC `BattlegroundQueue` (public): `void AddEvent(BasicEvent* e, Milliseconds delay) { m_events.AddEvent(e, m_events.CalculateTime(delay)); }` and call `bgQueue.AddEvent(removeEvent, Milliseconds(60000))`. `removeTime` at :3124 must stay in `GameTime::GetGameTimeMS()` units — that is what `BGQueueRemoveEvent::Execute` compares against in TC.
17. **`CHAR_UPD_NODE_ONLINE_CLEANUP`** — `NatsBus.cpp:1564`. Add enum member to `CharacterDatabaseStatements` (`server/database/Database/Implementation/CharacterDatabase.h`, near `CHAR_UPD_ACCOUNT_ONLINE` :286), `PrepareStatement(CHAR_UPD_NODE_ONLINE_CLEANUP, "UPDATE characters SET online = 0, owning_node_id = 0 WHERE owning_node_id = ?", CONNECTION_ASYNC)` in `CharacterDatabase.cpp`, and a `sql/updates/characters/` migration adding `owning_node_id TINYINT UNSIGNED NOT NULL DEFAULT 0` (the column is written by the login/transfer path outside these files).
18. **`AddonInfo`** — `ClusterMgr.h:280`, `NatsBus.h:282`, `NatsBus.cpp:927,1105,1123-1133`. Define in `ClusterMgr.h`: `struct ClusterAddonInfo { std::string Name; uint8 Enabled; uint32 CRC; uint8 State; bool UsePublicKeyOrCRC; ClusterAddonInfo(std::string n, uint8 e, uint32 c, uint8 s, bool k) ...; };` and `s/\bAddonInfo\b/ClusterAddonInfo/` in those three files. On the TC side add `WorldSession::GetSecureAddons() const` / `SetSecureAddons(std::vector<SecureAddonInfo>)` (`WorldSession.h:1343-1348` is private) so the source node can fill the list and the destination can seed `_addons.SecureAddons` before `SendAddonsInfo()`. Mapping: `Enabled = Status != BANNED`, `State = Status`, `UsePublicKeyOrCRC = HasKey`, `CRC = 0`.
19. **(optional) Windows guard** — `NatsBus.cpp:1680-1748` (`/proc/self/*`, `::getpid`, `<unistd.h>` at :24). Wrap in `#ifndef _WIN32` with zero-returning stubs; TC CI builds MSVC.
20. **Build system** — not a source edit but required: add `nats` to `src/server/game/CMakeLists.txt` link list (or a `dep/cnats` find-module), add `src/server/shared/Cluster/` to the `shared` target's sources, and `src/server/game/Server/{NatsBus,ClusterMgr,ClientRedirect,PlayerTransfer,PlayerStateSync,SharedPlayerCache,PacketTrace}.cpp` to `game` (TC globs `Server/*` via `CollectSourceFiles`, so dropping the files in the dir is enough for `game`; `shared/Cluster` likewise).

## C. Prepared statements and config keys referenced

### CharacterDatabase prepared statements
| enum | in TC? | SQL (c9core) |
|---|---|---|
| `CHAR_UPD_NODE_ONLINE_CLEANUP` | **missing** | `UPDATE characters SET online = 0, owning_node_id = 0 WHERE owning_node_id = ?` (needs `characters.owning_node_id` column) |

No other `CHAR_*` / `LOGIN_*` / `WORLD_*` statements are referenced by the 20 files.

### Config keys (worldserver.conf)
| key | type (AC) | default | read at |
|---|---|---|---|
| `ClusterServer.NodeId` | int32 / uint8 | 0 (NatsBus) / 1 (ClusterMgr) | NatsBus.cpp:68, ClusterMgr.cpp:262 |
| `ClusterServer.AuthKey` | string | "" (≥32 bytes required, fail-closed) | NatsBus.cpp:80 |
| `ClusterServer.TransportSyncInterval` | int32 (s) | 60 | NatsBus.cpp:91 |
| `ClusterServer.BgCoordinatorNode` | int32 | 1 | NatsBus.cpp:98, 1620 |
| `ClusterServer.AllowRemoteConsole` | bool | false | NatsBus.cpp:757 |
| `ClusterServer.HeartbeatInterval` | uint32 (ms) | 300 | NatsBus.cpp:1158 |
| `ClusterServer.NodeDeadTimeout` | uint32 (ms) | 3000 | NatsBus.cpp:1159 |
| `ClusterServer.MgmtStatusInterval` | int32 (s) | 5 | NatsBus.cpp:1160 |
| `ClusterServer.MgmtPlayersInterval` | int32 (s) | 3 | NatsBus.cpp:1161 |
| `ClusterServer.LFGMasterNode` | int32 | 1 | NatsBus.cpp:1162 |
| `ClusterServer.PacketTrace.Enable` | bool | false | ClusterMgr.cpp:231 |
| `ClusterServer.RedirectDebug` | bool | false | ClusterMgr.cpp:237 |
| `ClusterServer.GameAddress` | string | "127.0.0.1" | ClusterMgr.cpp:257 |
| `ClusterServer.GamePort` | uint16 | `WorldServerPort` (8085) | ClusterMgr.cpp:258-259 |
| `WorldServerPort` | uint16 | 8085 | ClusterMgr.cpp:259 (TC also reads it: `World.cpp:752`) |
| `ClusterServer.RedirectAddress` | string | "" | ClusterMgr.cpp:260 |
| `ClusterServer.RedirectPort` | uint16 | 0 | ClusterMgr.cpp:261 |
| `ClusterServer.InstanceServer` | bool | false | ClusterMgr.cpp:271 |
| `ClusterServer.Maps` | string (csv / "-1" / "") | "" | ClusterMgr.cpp:278 |
| `ClusterServer.Zones` | string (csv) | "" | ClusterMgr.cpp:293, 327 |

Logger names used: `server.worldserver` (all files), `cluster.packettrace` (PacketTrace.h). Keys referenced only in comments (not read by these files): `ProxyServer.Enable`, `InstanceServer.Enable`.

### NATS subjects (for the conf/ops side, unchanged by the port)
`cluster.node.{N}`, `cluster.broadcast`, `cluster.announce`, `cluster.transport.query`, `cluster.proxy`, `cluster.mgmt.status`, `cluster.mgmt.players`, `cluster.nodemgr.{N}`, `cluster.mgmt.nodemgr`.
