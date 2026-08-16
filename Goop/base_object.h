#ifndef base_object_h
#define base_object_h

#include "util/angle.h"
#include "util/vec.h"
#include "luaapi/types.h"
#include "glua.h"

struct BITMAP;
class Viewport;

class BasePlayer;

struct DamageCause {
	int weaponIndex = -1;		// firing WeaponType index; -1 = unattributed
	unsigned int shooterID = 0; // firing player's uniqueID; 0 = none
};

class BaseObject : public LuaObject {
  public:
	static LuaReference metaTable;

	BaseObject(BasePlayer *owner = 0, Vec pos_ = Vec(), Vec spd_ = Vec());
	virtual ~BaseObject();

#ifndef DEDSERV
	virtual void draw(Viewport *viewport) {}
#endif
	virtual void think() {}

	virtual Vec getRenderPos();

	virtual BasePlayer *getOwner();

	virtual Angle getAngle();

	// Gets the object's dir ( left = -1 right = 1 )
	virtual int getDir();

	virtual bool isCollidingWith(const Vec &point, float radius);

	virtual void removeRefsToPlayer(BasePlayer *player);

	virtual void addAngleSpeed(AngleDiff speed) {}

	// Tells the object to remove itself ( The object may not agree and nothing will happen )
	virtual void remove();

	virtual void makeReference();

#ifndef DEDSERV
	// Sets a destination alpha value and the time in logic frames it will take to reach that value
	virtual void setAlphaFade(int frames, int dest) {}
#endif

	virtual void customEvent(size_t index) {}

	virtual void damage(float amount, BasePlayer *damager, DamageCause const &cause) {}

	// Gets the WeaponType index of the object that caused this object (-1 if none)
	int getWeaponIndex() const {
		return m_weaponIndex;
	}
	// Gets the uniqueID of the player that caused this object (0 if none)
	unsigned int getShooterID() const {
		return m_shooterID;
	}
	// Sets the weapon index and shooter ID for attribution tracking
	void setCause(int widx, unsigned int sid) {
		m_weaponIndex = widx;
		m_shooterID = sid;
	}

	virtual void addSpeed(Vec spd_) {
		spd += spd_;
	}

	virtual void setPos(Vec pos_) {
		pos = pos_;
	}

	// IMPORTANT: The pos and spd vectors should be used as read only. ( Because of netplay needs )
	// To change their values use the setters provided.
	Vec pos;
	Vec spd;

	LuaReference luaData;

	BaseObject *nextS_;
	BaseObject *nextD_;
	BaseObject *prevD_;
	int cellIndex_;
	Vec lastRelocatePos_; // pos at last relocate; relocateIfNecessary skips when unchanged

  protected:
	BasePlayer *m_owner;

	int m_weaponIndex = -1;		  // firing WeaponType index; -1 = unattributed
	unsigned int m_shooterID = 0; // firing player's uniqueID; 0 = none

  public:
	// If this is true the object will be removed from the objects list in the next frame
	bool deleteMe;
};

#endif // _base_object_h_
