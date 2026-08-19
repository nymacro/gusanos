#include "net_worm.h"

#include "dedicated.h"

#include "util/vec.h"
#include "util/angle.h"
#include "util/log.h"
#include "game.h"
#include "weapon.h"
#include "weapon_type.h"
#include "base_worm.h"
#ifndef DEDSERV
#include "base_animator.h"
#endif
#include "base_object.h"
#include "base_player.h"
#include "player_options.h"
#include "ninjarope.h"
#include "network.h"
#include "vector_replicator.h"
#include "posspd_replicator.h"
#include "encoding.h"
#include "gconsole.h"
#include "util/game_rng.h"

#include <math.h>
#include <vector>
#include <memory>
#include <SDL3/SDL_timer.h>
#include "network_compat.h"

using namespace std;

ZCom_ClassID NetWorm::classID = ZCom_Invalid_ID;

NetWorm::NetWorm(bool isAuthority) : BaseWorm() {
	health = 100;
	timeSinceLastUpdate = 1;

	m_playerID = INVALID_NODE_ID;
	m_node = std::make_unique<ZCom_Node>();
	if (!m_node) {
		allegro_message("ERROR: Unable to create worm node.");
	}

	m_node->beginReplicationSetup(9);

	static ZCom_ReplicatorSetup posSetup(ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
										 ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH, Position, -1, 1000);

	m_node->addReplicator(std::make_unique<PosSpdReplicator>(&posSetup, &pos, &spd, game.level.vectorEncoding,
															 game.level.diffVectorEncoding),
						  true);

	static ZCom_ReplicatorSetup nrSetup(ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);

	m_node->addReplicator(
		std::make_unique<VectorReplicator>(&nrSetup, &m_ninjaRope->getPosReference(), game.level.vectorEncoding), true);

	m_node->addReplicationInt((zS32 *)&m_ninjaRope->getLengthReference(), 32, false, ZCOM_REPFLAG_MOSTRECENT,
							  ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);

	m_node->addReplicationInt((zS32 *)&health, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	static ZCom_ReplicatorSetup angleSetup(ZCOM_REPFLAG_MOSTRECENT,
										   ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);

	m_node->addReplicator(std::make_unique<AngleReplicator>(&angleSetup, &aimAngle), true);

	m_node->addReplicationInt((zS32 *)&m_dir, 8, true, ZCOM_REPFLAG_MOSTRECENT,
							  ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);

	m_node->addReplicationBool(&m_ninjaRope->active, ZCOM_REPFLAG_MOSTRECENT,
							   ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);
	m_node->addReplicationBool(&m_ninjaRope->attached, ZCOM_REPFLAG_MOSTRECENT,
							   ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH);

	m_node->setInterceptID(PlayerID);

	m_node->addReplicationInt((zS32 *)&m_playerID, 32, false, ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
							  ZCOM_REPRULE_AUTH_2_ALL, INVALID_NODE_ID);

	m_node->endReplicationSetup();

	m_interceptor = std::make_unique<NetWormInterceptor>(this);
	m_node->setReplicationInterceptor(m_interceptor.get());

	m_isAuthority = isAuthority;
	if (isAuthority) {
		m_node->setEventNotification(true, false); // Enables the eEvent_Init.
		if (!m_node->registerNodeDynamic(classID, network.getZControl()))
			allegro_message("ERROR: Unable to register worm authority node.");
	} else {
		if (!m_node->registerRequestedNode(classID, network.getZControl()))
			allegro_message("ERROR: Unable to register worm requested node.");
	}
	m_node->applyForZoidLevel(1);
}

NetWorm::~NetWorm() = default;

void NetWorm::pushPosSnapshot(const Vec &posSnapshot, uint64_t tick) {
	// Teleport detection: a jump larger than the snap threshold between
	// consecutive snapshots is a spawn/relocate/server correction, not
	// movement. Clear the buffer so we snap to the new position instead of
	// interpolating across the gap.
	if (m_haveLastBuffered) {
		Vec d(m_lastBufferedPos, posSnapshot); // posSnapshot - last
		if (d.length() > RENDER_SNAP_THRESHOLD) {
			m_posSnapshotCount = 0;
			m_posSnapshotHead = 0;
		}
	}
	m_lastBufferedPos = posSnapshot;
	m_haveLastBuffered = true;

	m_posSnapshots[m_posSnapshotHead].pos = posSnapshot;
	m_posSnapshots[m_posSnapshotHead].tick = tick;
	m_posSnapshotHead = (m_posSnapshotHead + 1) % INTERP_BUFFER_SIZE;
	if (m_posSnapshotCount < INTERP_BUFFER_SIZE)
		++m_posSnapshotCount;
}

Vec NetWorm::interpolateRenderPos(uint64_t renderTime) const {
	if (m_posSnapshotCount == 0)
		return pos; // no snapshots: fall back to current (snapped) pos
	if (m_posSnapshotCount == 1) {
		size_t i = (m_posSnapshotHead + INTERP_BUFFER_SIZE - 1) % INTERP_BUFFER_SIZE;
		return m_posSnapshots[i].pos;
	}

	// Oldest valid entry (ring is ordered oldest->newest by insertion).
	size_t start = (m_posSnapshotHead + INTERP_BUFFER_SIZE - m_posSnapshotCount) % INTERP_BUFFER_SIZE;
	size_t newest = (m_posSnapshotHead + INTERP_BUFFER_SIZE - 1) % INTERP_BUFFER_SIZE;

	// Find the two adjacent snapshots bracketing renderTime.
	const PosSnapshot *s0 = nullptr; // last snapshot with tick <= renderTime
	const PosSnapshot *s1 = nullptr; // first snapshot with tick >  renderTime
	for (size_t k = 0; k < m_posSnapshotCount; ++k) {
		size_t i = (start + k) % INTERP_BUFFER_SIZE;
		if (m_posSnapshots[i].tick <= renderTime) {
			s0 = &m_posSnapshots[i];
		} else {
			s1 = &m_posSnapshots[i];
			break;
		}
	}

	if (!s0)
		return m_posSnapshots[start].pos; // renderTime before oldest: hold
	if (!s1)
		return m_posSnapshots[newest].pos; // renderTime after newest: hold (no extrapolation)

	uint64_t span = s1->tick - s0->tick;
	if (span == 0)
		return s1->pos;
	double alpha = static_cast<double>(renderTime - s0->tick) / static_cast<double>(span);
	Vec d(s0->pos, s1->pos); // s1.pos - s0.pos (BaseVec two-arg ctor)
	return s0->pos + d * alpha;
}

void NetWorm::addEvent(ZCom_BitStream *data, NetWorm::NetEvents event) {
#ifdef COMPACT_EVENTS
	Encoding::encode(*data, event, NetWorm::EVENT_COUNT);
#else
	data->addInt(static_cast<int>(event), 8);
#endif
}

void NetWorm::think() {
	// Authority gate: for a worm we don't own (server-side remote-client
	// worm, or a proxy's view of another player's worm) we must NOT run
	// BaseWorm::think() physics. The owner runs physics and pushes pos/spd
	// via the OWNER_2_AUTH replicators; the server just relays to proxies
	// (AUTH_2_PROXY). Running physics here integrates from throttled,
	// ~RTT/2-stale input, producing a pos that diverges from the owner's
	// truth; the next replication snap back (PosSpdReplicator::unpackData)
	// makes the server's pos oscillate every tick, and that oscillation is
	// relayed to proxies as rubber-banding (very visible redirecting a
	// ninja-rope swing). Proxies have the same problem in reverse: local
	// physics fights the replicated snap (the proxy half of the rubber-band).
	//
	// Server still runs the authoritative bookkeeping in BaseWorm::think()
	// that doesn't move the worm: death detection (-> die() broadcasts Die)
	// and the auto-respawn timer (-> respawn() broadcasts Respawn), via the
	// virtual overrides. Pure-physics branches are skipped; weapon ticks
	// still run (runWeaponThink below) so firing replicates — the owner
	// can't spawn the authority-only projectile itself.
	//
	// Proxy keeps weapon-think (predicted fire / SHOOT-repro spawns the
	// deterministic cosmetic burst) and the render ticks (walk animation +
	// firecone) BaseWorm::think() would have advanced; `animate` is
	// re-derived from networked move flags (processMoveAndDig is skipped).
	// Death/respawn are event-driven (Die/Respawn events); the rope is gated
	// in NinjaRope::think() (no-op on proxy) with its state replicated and
	// the owner's rope pull baked into the replicated spd. Gated by
	// NET_PROXY_NOPHYS (default on); =0 reverts a proxy to full
	// BaseWorm::think() (legacy dead-reckoning).
	if (m_isAuthority && !isLocalAuthority()) {
		if (m_isActive) {
			if (health <= 0)
				die();
			// The owner client owns this worm's physics (pos/spd/aim are
			// replicated to us), so we must NOT run BaseWorm::think() or we
			// reintroduce the rubber-band. But the owner cannot spawn the
			// authoritative projectile itself (particles are authority-only),
			// so we still tick weapons here: the networked FIRE action has
			// set primaryShooting on this authority worm, and
			// runWeaponThink() consumes it, fires, and broadcasts SHOOT.
			runWeaponThink();
		} else {
			if (m_timeSinceDeath > game.options.maxRespawnTime && game.options.maxRespawnTime >= 0)
				respawn();
			++m_timeSinceDeath;
		}
	} else if (!m_isAuthority && !isLocalAuthority()) {
		// Proxy worm: no local physics (pos/spd/renderPos are
		// replication/buffer-driven); keep only weapon-think + render ticks.
		if (m_isActive) {
			runWeaponThink();
			if (!g_dedicated) {
				// Re-derive walk-animate from the networked move flags, mirroring
				// processMoveAndDig: animate only when moving in one direction
				// (both or neither => idle/dig frame).
				float leftInt = movingLeft ? m_movingLeftIntensity : 0.0f;
				float rightInt = movingRight ? m_movingRightIntensity : 0.0f;
				animate = (leftInt > 0.0f) != (rightInt > 0.0f);
				if (animate)
					m_animator->tick();
				else
					m_animator->reset();
				if (m_currentFirecone) {
					if (m_fireconeTime == 0)
						m_currentFirecone = NULL;
					--m_fireconeTime;
					m_fireconeAnimator->tick();
				}
			}
		} else {
			if (m_timeSinceDeath > game.options.maxRespawnTime && game.options.maxRespawnTime >= 0)
				respawn();
			++m_timeSinceDeath;
		}
	} else {
		BaseWorm::think();
	}
	if (!g_dedicated) {
		if (!m_isAuthority && !isLocalAuthority() && network.netInterpEnabled && m_posSnapshotCount > 0) {
			// Proxy rendering a remote worm: interpolate renderPos between
			// buffered timestamped snapshots at a delayed render time
			// (receive-timeline interpolation). This decouples rendering from
			// the per-tick pos snap / local-physics integration fight that
			// caused the proxy rubber-band.
			uint64_t now = SDL_GetTicks();
			uint64_t delay = static_cast<uint64_t>(network.netInterpDelayMs);
			uint64_t renderTime = (now > delay) ? (now - delay) : 0;
			renderPos = interpolateRenderPos(renderTime);
		} else {
			// Owner / server-local / interpolation disabled: legacy exponential
			// easing toward the (authoritative) pos, snapping on large jumps
			// (spawn, server correction, teleport).
			Vec delta(renderPos, pos); // == pos - renderPos (see BaseVec two-arg ctor)
			double dist = delta.length();
			if (dist > RENDER_SNAP_THRESHOLD) {
				renderPos = pos;
			} else {
				double fact = 1.0 / (1.0 + dist / 4.0);
				renderPos = renderPos * (1.0 - fact) + pos * fact;
			}
		}
	}

	++timeSinceLastUpdate;

	if (!m_node)
		return;

	while (m_node->checkEventWaiting()) {
		DLOG("NetWorm::think processing event, isAuthority=" << m_isAuthority);
		eZCom_Event type;
		eZCom_NodeRole remote_role;
		ZCom_ConnID conn_id;

		auto data = m_node->getNextEvent(&type, &remote_role, &conn_id);
		switch (type) {
			case eZCom_EventUser:
				if (data) {
#ifdef COMPACT_EVENTS
					NetEvents event = (NetEvents)Encoding::decode(*data, EVENT_COUNT);
#else
					NetEvents event = (NetEvents)data->getInt(8);
#endif
					switch (event) {
						case PosCorrection: {
							pos = game.level.vectorEncoding.decode<Vec>(*data);
							spd = game.level.vectorEncoding.decode<Vec>(*data);
						} break;
						case Respawn: {
							DLOG("NetWorm::think Respawn received, calling BaseWorm::respawn");
							Vec newpos = game.level.vectorEncoding.decode<Vec>(*data);
							BaseWorm::respawn(newpos);
							health = 100;
						} break;
						case Dig: {
							Vec digPos = game.level.vectorEncoding.decode<Vec>(*data);
							Angle digAngle = Angle((int)data->getInt(Angle::prec));
							uint32_t seq = data->getInt(32);
							uint32_t seed = data->getInt(32);
							uint32_t shippedChecksum = data->getInt(32);
							// Reproduce the authority's dig burst from the shipped seed
							// (never recomputed from local node state).
							GameRng rg;
							rg.seed(seed);
							GameplayRngScope scope(rg);
							BaseWorm::dig(digPos, digAngle);
							burstChecksumMismatch("DIG", fireSeedNodeID(), seq, shippedChecksum, rg.checksum());
							reconcileFireActionSeq(seq + 1);
						} break;
						case Die: {
							m_lastHurt = game.findPlayerWithID(data->getInt(32));
							int wcount = (int)game.weaponList.size();
							m_lastHurtWeapon = Encoding::decode(*data, wcount + 1) - 1;
							if (const char *s = data->getStringStatic())
								m_lastHurtName = s;
							else
								m_lastHurtName.clear();
							uint32_t seq = data->getInt(32);
							Vec deathPos = game.level.vectorEncoding.decode<Vec>(*data);
							Vec deathSpd = game.level.vectorEncoding.decode<Vec>(*data);
							uint32_t seed = data->getInt(32);
							uint32_t shippedChecksum = data->getInt(32);
							// Reproduce the authority's death burst from the shipped seed
							// and burst-tick state (the local proxy pos/spd lag by RTT).
							GameRng rg;
							rg.seed(seed);
							{
								BurstStateScope burstState(*this);
								burstState.applyPos(deathPos);
								burstState.applySpd(deathSpd);
								GameplayRngScope scope(rg);
								BaseWorm::die();
							}
							burstChecksumMismatch("DIE", fireSeedNodeID(), seq, shippedChecksum, rg.checksum());
							reconcileFireActionSeq(seq + 1);
						} break;
						case ChangeWeapon: {
							size_t weapIndex = Encoding::decode(*data, m_weapons.size());
							changeWeaponTo(weapIndex);
						} break;
						case WeaponMessage: {
							size_t weapIndex = Encoding::decode(*data, m_weapons.size());
							if (weapIndex < m_weapons.size() && m_weapons[weapIndex])
								m_weapons[weapIndex]->recieveMessage(data.get());
						} break;
						case SetWeapon: {
							size_t index = Encoding::decode(*data, game.options.maxWeapons);
							if (data->getBool()) {
								size_t weaponIndex = Encoding::decode(*data, game.weaponList.size());
								if (weaponIndex < game.weaponList.size())
									BaseWorm::setWeapon(index, game.weaponList[weaponIndex]);
								else
									BaseWorm::setWeapon(index, 0);
							} else {
								BaseWorm::setWeapon(index, 0);
							}
						} break;
						case ClearWeapons: {
							DLOG("Clearing weapons");
							BaseWorm::clearWeapons();
						} break;
						case SYNC: {
							m_isActive = data->getBool();
							m_ninjaRope->active = data->getBool();
							currentWeapon = Encoding::decode(*data, m_weapons.size());
							BaseWorm::clearWeapons();
							while (data->getBool()) {
								size_t index = Encoding::decode(*data, m_weapons.size());
								size_t weapTypeIndex = Encoding::decode(*data, game.weaponList.size());
								if (weapTypeIndex < game.weaponList.size() && index < m_weapons.size()) {
									luaDelete(m_weapons[index]);
									m_weapons[index] = 0;
									m_weapons[index] = new Weapon(game.weaponList[weapTypeIndex], this);
								}
							}
						} break;

						case LuaEvent: {
							int index = data->getInt(8);
							if (LuaEventDef *event = network.indexToLuaEvent(Network::LuaEventGroup::Worm, index)) {
								event->call(getLuaReference(), data.get());
							}
						} break;

						case EVENT_COUNT:
							break;
					}
				}
				break;

			case eZCom_EventInit: {
				sendSyncMessage(conn_id);
			} break;

			default:
				break;
		}
	}
}

void NetWorm::sendLuaEvent(LuaEventDef *event, eZCom_SendMode mode, zU8 rules, ZCom_BitStream *userdata,
						   ZCom_ConnID connID) {
	if (!m_node)
		return;
	ZCom_BitStream data;
	addEvent(&data, LuaEvent);
	data.addInt(event->idx, 8);
	if (userdata) {
		data.addBitStream(userdata);
	}
	if (!connID)
		m_node->sendEvent(mode, rules, &data);
	else
		m_node->sendEventDirect(mode, &data, connID);
}

void NetWorm::correctOwnerPosition() {
	ZCom_BitStream data;
	addEvent(&data, PosCorrection);
	game.level.vectorEncoding.encode<Vec>(data, pos);
	game.level.vectorEncoding.encode<Vec>(data, spd);
	m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_OWNER, &data);
}

void NetWorm::assignOwner(BasePlayer *owner) {
	BaseWorm::assignOwner(owner);
	m_playerID = m_owner->getNodeID();
}

void NetWorm::setOwnerId(ZCom_ConnID _id) {
	m_node->setOwner(_id, true);
}

void NetWorm::sendSyncMessage(ZCom_ConnID id) {
	ZCom_BitStream data;
	addEvent(&data, SYNC);
	data.addBool(m_isActive);
	data.addBool(m_ninjaRope->active);
	Encoding::encode(data, currentWeapon, m_weapons.size());

	for (size_t i = 0; i < m_weapons.size(); ++i) {
		if (m_weapons[i]) {
			data.addBool(true);
			Encoding::encode(data, i, m_weapons.size());
			Encoding::encode(data, m_weapons[i]->getType()->getIndex(), game.weaponList.size());
		}
	}
	data.addBool(false);

	m_node->sendEventDirect(eZCom_ReliableOrdered, &data, id);
}

void NetWorm::sendWeaponMessage(int index, ZCom_BitStream *weaponData, zU8 repRules) {
	ZCom_BitStream data;
	addEvent(&data, WeaponMessage);
	Encoding::encode(data, index, m_weapons.size());
	data.addBitStream(weaponData);
	m_node->sendEvent(eZCom_ReliableOrdered, repRules, &data);
}

ZCom_NodeID NetWorm::getNodeID() {
	if (m_node)
		return m_node->getNetworkID();
	else
		return INVALID_NODE_ID;
}

void NetWorm::respawn() {
	if (m_isAuthority && m_node) {
		// Network-initiated respawn: honor immediately, bypassing the
		// m_timeSinceDeath > minRespawnTime gate in BaseWorm::respawn().
		// That gate exists for auto-respawn timing, not for explicit
		// user-initiated respawn requests (JUMP with inactive worm).
		DLOG("NetWorm::respawn authority nodeID=" << m_node->getNetworkID() << " m_timeSinceDeath=" << m_timeSinceDeath
												  << " m_isActive(before)=" << m_isActive);
		BaseWorm::respawn(game.level.getSpawnLocation(m_owner));
		DLOG("NetWorm::respawn authority nodeID=" << m_node->getNetworkID() << " m_isActive(after)=" << m_isActive);
		if (m_isActive) {
			ZCom_BitStream data;
			addEvent(&data, Respawn);
			game.level.vectorEncoding.encode<Vec>(data, pos);
			m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &data);
		}
	}
}

void NetWorm::dig() {
	if (m_isAuthority && m_node) {
		if (m_isActive) {
			// Deterministic dig burst: seed the gameplay RNG from the worm's
			// network id and its per-worm action counter so the dig particle
			// (and any creation-script sub-spawns) reproduce identically on
			// every peer. See game_rng.h.
			uint32_t seq = m_actionSeq;
			uint32_t seed = mix32(fireSeedNodeID(), seq);
			GameRng rg;
			rg.seed(seed);
			GameplayRngScope scope(rg);
			BaseWorm::dig();
			++m_actionSeq;
			ZCom_BitStream data;
			addEvent(&data, Dig);
			game.level.vectorEncoding.encode<Vec>(data, pos);
			data.addInt(int(getAngle()), Angle::prec);
			data.addInt(seq, 32); // pre-increment action sequence
			data.addInt(seed, 32);
			data.addInt(rg.checksum(), 32);
			m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &data);
		}
	}
}

void NetWorm::die() {
	if (m_isAuthority && m_node) {
		std::string killerName;
		if (m_lastHurt)
			killerName = m_lastHurt->m_name;
		else if (m_lastHurtShooterID)
			killerName = Network::findSavedName(m_lastHurtShooterID);
		m_lastHurtName = killerName; // so BaseWorm::die uses it consistently

		// Deterministic death burst: seed the gameplay RNG from the worm's
		// network id + per-worm action counter so the death particle (and the
		// wormDeath Lua callback's spawns) reproduce identically on every peer.
		// The burst runs BEFORE the event is packed because the burst-tick
		// pos/spd and the burst draw checksum ride along in the payload. See
		// game_rng.h.
		uint32_t seq = m_actionSeq;
		uint32_t seed = mix32(fireSeedNodeID(), seq);
		GameRng rg;
		rg.seed(seed);
		Vec deathPos = pos;
		Vec deathSpd = spd;
		{
			GameplayRngScope scope(rg);
			BaseWorm::die();
		}
		++m_actionSeq;
		ZCom_BitStream data;
		addEvent(&data, Die);
		if (m_lastHurt) {
			data.addInt(static_cast<int>(m_lastHurt->getNodeID()), 32);
		} else {
			data.addInt(INVALID_NODE_ID, 32);
		}
		int wcount = (int)game.weaponList.size();
		Encoding::encode(data, m_lastHurtWeapon + 1, wcount + 1); // -1 -> 0 sentinel
		data.addString(killerName.c_str());
		data.addInt(seq, 32); // pre-increment action sequence
		game.level.vectorEncoding.encode<Vec>(data, deathPos);
		game.level.vectorEncoding.encode<Vec>(data, deathSpd);
		data.addInt(seed, 32);
		data.addInt(rg.checksum(), 32);
		m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &data);
	}
}

void NetWorm::changeWeaponTo(unsigned int weapIndex) {
	if (m_node) {
		ZCom_BitStream data;
		addEvent(&data, ChangeWeapon);
		Encoding::encode(data, weapIndex, m_weapons.size());
		m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_OWNER_2_AUTH | ZCOM_REPRULE_AUTH_2_PROXY, &data);
		BaseWorm::changeWeaponTo(weapIndex);
	}
}

void NetWorm::setWeapon(size_t index, WeaponType *type) {
	if (!network.isClient()) {
		BaseWorm::setWeapon(index, type);
		if (m_node) {
			ZCom_BitStream data;
			addEvent(&data, SetWeapon);
			Encoding::encode(data, index, game.options.maxWeapons);
			if (type) {
				data.addBool(true);
				Encoding::encode(data, type->getIndex(), game.weaponList.size());
			} else
				data.addBool(false);
			m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &data);
		}
	}
}

void NetWorm::clearWeapons() {
	if (!network.isClient()) {
		BaseWorm::clearWeapons();
		if (m_node) {
			ZCom_BitStream data;
			addEvent(&data, ClearWeapons);
			m_node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &data);
		}
	}
}

void NetWorm::damage(float amount, BasePlayer *damager, DamageCause const &cause) {
	if (m_isAuthority) {
		BaseWorm::damage(amount, damager, cause);
	}
}

void NetWorm::finalize() {
	BaseWorm::finalize();
	m_node.reset();
	m_interceptor.reset();
}

NetWormInterceptor::NetWormInterceptor(NetWorm *parent) {
	m_parent = parent;
}

bool NetWormInterceptor::inPreUpdateItem(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role,
										 ZCom_Replicator *_replicator, zU32 _estimated_time_sent) {
	switch (_replicator->getSetup()->getInterceptID()) {
		case NetWorm::PlayerID: {
			ZCom_NodeID recievedID = *static_cast<zU32 *>(_replicator->peekData());
			list<BasePlayer *>::iterator playerIter;
			for (playerIter = game.players.begin(); playerIter != game.players.end(); playerIter++) {
				if ((*playerIter)->getNodeID() == recievedID) {
					(*playerIter)->assignWorm(m_parent);
				}
			}
		} break;
		case NetWorm::Position: {
			// Buffer the incoming position for proxy-side render
			// interpolation. Only proxies buffer: the server relays raw
			// snapshots (it must not interpolate the stream it relays), and
			// the owner never receives its own position back. peekData()
			// returns the decoded pos before commit (the replicator frees
			// the peek buffer on return), so copy the value. Returning true
			// lets unpackData commit normally (pos/spd stay network truth).
			if (!m_parent->m_isAuthority && network.netInterpEnabled) {
				Vec recievedPos = *static_cast<Vec *>(_replicator->peekData());
				m_parent->pushPosSnapshot(recievedPos, SDL_GetTicks());
			}
		} break;
	}
	return true;
}

bool NetWormInterceptor::outPreUpdateItem(ZCom_Node *node, ZCom_ConnID from, eZCom_NodeRole remote_role,
										  ZCom_Replicator *replicator) {

	switch (replicator->getSetup()->getInterceptID()) {
		case NetWorm::Position:
			if (!m_parent->m_isActive) // Prevent non-active worms from replicating position
				return false;
			break;
	}

	return true;
}
