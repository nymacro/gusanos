#ifndef BASE_WORM_H
#define BASE_WORM_H

#include "network_compat.h"
#include "util/vec.h"
#include "util/angle.h"
#include "base_object.h"

#include <string>
#include <vector>
#include "luaapi/types.h"

class BasePlayer;
class NinjaRope;
class Weapon;
class WeaponType;
struct LuaEventDef;
#ifndef DEDSERV
class SpriteSet;
class BaseAnimator;
class Viewport;
#endif

class BaseWorm : public BaseObject {
  public:
	enum Actions {
		MOVELEFT,
		MOVERIGHT,
		// AIMUP,
		// AIMDOWN,
		FIRE,
		FIRE2,
		JUMP,
		DIG,
		NINJAROPE,
		CHANGEWEAPON,
		RESPAWN
	};

	enum Direction {
		Down = 0,
		Left,
		Up,
		Right,

		DirMax
	};

	static LuaReference metaTable;
	// static int const luaID = 2;

	BaseWorm();
	virtual ~BaseWorm();

	virtual void assignOwner(BasePlayer *owner);

	// void draw(BITMAP* where,int xOff, int yOff);
	void draw(Viewport *viewport);

	void calculateReactionForce(BaseVec<long> origin, Direction dir);
	void calculateAllReactionForces(BaseVec<float> &nextPos, BaseVec<long> &inextPos);
	void processMoveAndDig(void);
	void processPhysics();
	void processJumpingAndNinjaropeControls();

	virtual void think();
	void actionStart(Actions action, float intensity = 1.0f);
	void actionStop(Actions action, float intensity = 0.0f);
	void addAimSpeed(AngleDiff speed);
	void addRopeLength(float distance);

	Vec getWeaponPos();
#ifndef DEDSERV
	Vec getRenderPos();
#endif

	float getHealth();
	bool isChanging() {
		return changing;
	}

	// True if this instance decides gameplay outcomes (death, etc.) locally.
	// Single-player/local worms are always authoritative. Networked worms
	// override this to reflect their replication role: only the authority
	// may trigger death from the locally-tracked health value; non-authoritative
	// clients must rely on the replicated Die event instead.
	virtual bool isAuthority() const {
		return true;
	}

	// True if THIS machine is the simulator for this worm's physics/rope.
	// This is the gate that decides whether to run BaseWorm::think() /
	// NinjaRope::think() locally. Defaults to isAuthority() (correct for
	// single-player/local worms). NetWorm overrides it with the combined
	// role/owner test: a server relays a remote-client-owned worm (don't
	// simulate), and a proxy renders another player's worm (don't
	// simulate); only the true simulator (server for local/AI worms, owner
	// client for its own worm) integrates physics.
	virtual bool isLocalAuthority() const {
		return isAuthority();
	}

	virtual void damage(float amount, BasePlayer *damager, DamageCause const &cause);

	// This are virtual so that NetWorm can know about them and tell others over the network.
	virtual void respawn();
	void respawn(const Vec &newPos);

	virtual void dig();
	void dig(const Vec &digPos, Angle angle);

	virtual void die();
	virtual void changeWeaponTo(unsigned int weapIndex);

	virtual void setWeapon(size_t index, WeaponType *type);
	virtual void setWeapons(std::vector<WeaponType *> const &weaps);
	virtual void clearWeapons();

	Weapon *getCurrentWeapon(); // Where and what for is this used? Lua maybe? >:O

	// getWeaponIndexOffset can be used to get the currentWeapon index or
	// to get the one to the right or the left or the one 1000 units to the
	// right ( it will wrap the value so that its always inside the worm's weapons size )
	int getWeaponIndexOffset(int offset);
	Angle getAngle();
	void setDir(int d); // Only use this if you are going to sync it over netplay with an event
	int getDir() {
		return m_dir;
	}
	bool isCollidingWith(const Vec &point, float radius);
	bool isActive();
	void removeRefsToPlayer(BasePlayer *player);

#ifndef DEDSERV
	void showFirecone(SpriteSet *sprite, int frames, float distance);
#endif

	NinjaRope *getNinjaRopeObj();

	void setShowingWeaponText(bool show);

	AngleDiff aimSpeed; // Useless to add setters and getters for this
	Angle aimAngle;

	virtual void sendWeaponMessage(int index, ZCom_BitStream *data, zU8 repRules = ZCOM_REPRULE_AUTH_2_ALL) {}
	virtual eZCom_NodeRole getRole() {
		return eZCom_RoleUndefined;
	}
	/*
	virtual LuaReference getLuaReference();
	virtual void finalize();
	*/

	virtual void makeReference();
	virtual void finalize();

	virtual void sendLuaEvent(LuaEventDef *event, eZCom_SendMode mode, zU8 rules, ZCom_BitStream *userdata,
							  ZCom_ConnID connID) {}

	/*
		void* operator new(size_t count);

		void operator delete(void* block)
		{
			// Lua frees the memory
		}

		void* operator new(size_t count, void* space)
		{
			return space;
		}
	*/

  protected:
	// LuaReference luaReference;

	// Tick every weapon's Weapon::think() (timers, fire trigger ->
	// primaryShoot->run() spawns the authoritative projectile, ammo/reload
	// bookkeeping, SHOOT/OutOfAmmo net messages). Extracted from think() so
	// the server can run it for remote-client-owned worms inside the
	// NetWorm::think() authority gate. Safe there because it neither
	// integrates physics nor mutates pos/spd; aimAngle/currentWeapon arrive
	// replicated from the owner and the FIRE action sets primaryShooting via
	// the networked event. The owner client cannot spawn networked particles,
	// so the server MUST run this for shots to replicate.
	void runWeaponThink();

#ifndef DEDSERV
	Vec renderPos;
#endif

	int reacts[DirMax];

	float aimRecoilSpeed;
	float health;
	// float currentRopeLength; //moved to Ninjarope

#ifndef DEDSERV
	int m_fireconeTime;
	float m_fireconeDistance;
#endif

	int m_timeSinceDeath; // Used for the min and max respawn time sv variables

	size_t currentWeapon;

	std::vector<Weapon *> m_weapons;
	int m_weaponCount;

	BasePlayer *m_lastHurt;
	int m_lastHurtWeapon = -1;
	unsigned int m_lastHurtShooterID = 0;
	std::string m_lastHurtName;
	NinjaRope *m_ninjaRope;

#ifndef DEDSERV
	SpriteSet *skin;
	SpriteSet *skinMask;
	SpriteSet *m_currentFirecone;
	BaseAnimator *m_fireconeAnimator;

	BaseAnimator *m_animator;
#endif
	// Smaller vars last to improve alignment and/or decrease structure size
	bool m_isActive;
	bool movingLeft;
	bool movingRight;
	float m_movingLeftIntensity;
	float m_movingRightIntensity;
	bool jumping;
	bool animate;
	bool movable;  // What do we need this for? // Dunno, did I put this here? :o
	bool changing; // This shouldnt be in the worm class ( its player stuff >:O )
	bool showingWeaponText;
	int m_dir;
};

#endif // _WORM_H_
