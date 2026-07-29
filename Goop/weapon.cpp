#include "weapon.h"
#include "weapon_type.h"
#include "events.h"
#include "base_worm.h"
#include "game.h"
#include "util/vec.h"
#include "util/angle.h"
#include "util/log.h"
#include "timer_event.h"
#include "sprite_set.h"
#include "sprite.h"
#include "luaapi/context.h"
#include "network.h"

#include <iostream>

class BaseObject;

LuaReference Weapon::metaTable;

Weapon::Weapon(WeaponType *type, BaseWorm *owner) {
	m_type = type;
	m_owner = owner;
	primaryShooting = false;
	ammo = type->ammo;
	inactiveTime = 0;
	reloading = false;
	reloadTime = 0;
	m_outOfAmmo = false;

	sentOutOfAmmo = false;
	outOfAmmoCheck = false;

	for (auto t : m_type->timer) {
		timer.push_back(t->createState());
	}

	for (auto t : m_type->activeTimer) {
		activeTimer.push_back(t->createState());
	}
}

Weapon::~Weapon() {}

void Weapon::reset() {
	primaryShooting = false;
	ammo = m_type->ammo;
	inactiveTime = 0;
	reloading = false;
	reloadTime = 0;
	m_outOfAmmo = false;

	for (auto &t : timer) {
		t.completeReset();
	}
	for (auto &t : activeTimer) {
		t.completeReset();
	}
	for (auto &t : shootTimer) {
		t.completeReset();
	}
}

void Weapon::think(bool isFocused, size_t index) {
	// Whether this side runs primaryShoot locally and therefore owns the ammo
	// counter. The authority always does; a client does only for predicted
	// weapons (syncHax == false). For such weapons the client must also drive
	// the full out-of-ammo/reload cycle locally — deferring to the server
	// round-trip (OutOfAmmoCheck) stalls firing every time ammo hits 0, because
	// the client stops shooting while the server keeps firing, producing the
	// desync where the server shows particles the client never sees and the
	// client's ammo bar sticks at 0.
	bool localPredict = !network.isClient() || !m_type->syncHax;
	for (auto &t : timer) {
		if (t.tick()) {
			t.event->run(m_owner, 0, 0, this);
		}
	}

	if (isFocused) {
		for (auto &t : activeTimer) {
			if (t.tick()) {
				t.event->run(m_owner, 0, 0, this);
			}
		}
	} else {
		for (auto &t : activeTimer) {
			t.completeReset();
		}
	}

	if (inactiveTime > 0)
		inactiveTime--;
	else if (isFocused) {
		if (primaryShooting && ammo > 0) {
			if (m_type->primaryShoot) {
				if (m_owner->getRole() != eZCom_RoleProxy || !m_type->syncHax) {
					m_type->primaryShoot->run(m_owner, NULL, NULL, this);
					if (m_owner->getRole() == eZCom_RoleAuthority && m_type->syncHax) {
						ZCom_BitStream data;
						Encoding::encode(data, SHOOT, EventsCount);
						m_owner->sendWeaponMessage(index, &data, ZCOM_REPRULE_AUTH_2_PROXY);
					}
				}
			}
		}
	}
	if (isFocused) {
		if (ammo <= 0 && !reloading && !m_outOfAmmo) {
			m_outOfAmmo = true;
			// Predicted shooting (localPredict) or non-synced reload: handle the
			// out-of-ammo transition locally. Only server-authoritative weapons
			// (syncHax) with synced reload defer to the server via OutOfAmmoCheck.
			if (localPredict || !m_type->syncReload) {
				outOfAmmo();

				if (network.isHost() && m_type->syncHax && m_type->syncReload) {
					ZCom_BitStream data;
					// data.addInt( OUTOFAMMO , 8);
					Encoding::encode(data, OUTOFAMMO, EventsCount);
					m_owner->sendWeaponMessage(index, &data);
					sentOutOfAmmo = true;
				}
			} else {
				ZCom_BitStream data;
				Encoding::encode(data, OutOfAmmoCheck, EventsCount);
				m_owner->sendWeaponMessage(index, &data, ZCOM_REPRULE_OWNER_2_AUTH);
				// std::cout << "sent check plz message" << endl;
			}
		}
		if (reloading) {
			if (reloadTime > 0)
				--reloadTime;
			else if (localPredict || !m_type->syncReload) {
				reload();

				if (network.isHost() && m_type->syncHax && m_type->syncReload) {
					ZCom_BitStream data;
					// data.addInt( RELOADED , 8);
					Encoding::encode(data, RELOADED, EventsCount);
					m_owner->sendWeaponMessage(index, &data);
				}
			}
		}
	}

	if (outOfAmmoCheck) {
		// std::cout << "checking out of ammo" << endl;
		outOfAmmoCheck = false;
		if (ammo > 0) {
			// std::cout << "Sending correction" << endl;
			ZCom_BitStream data;
			Encoding::encode(data, AmmoCorrection, EventsCount);
			Encoding::encode(data, ammo, m_type->ammo + 1);
			m_owner->sendWeaponMessage(index, &data, ZCOM_REPRULE_AUTH_2_OWNER);
		} else {
			// std::cout << "Everything was in order" << endl;
			sentOutOfAmmo = false;
		}
	}
}

#ifndef DEDSERV
void Weapon::drawBottom(BITMAP *where, int x, int y) {
	if (m_type->laserSightIntensity > 0) {
		Vec direction(m_owner->getAngle());
		Vec inc;
		float intensityInc = 0;
		if (fabs(direction.x) > fabs(direction.y)) {
			inc = direction / fabs(direction.x);
		} else {
			inc = direction / fabs(direction.y);
		}

		if (m_type->laserSightRange > 0) {
			intensityInc = -(m_type->laserSightIntensity / m_type->laserSightRange) * inc.length();
		}
		Vec posDiff;
		float intensity = m_type->laserSightIntensity;
		while (game.level.getMaterial((int)(m_owner->pos.x + posDiff.x), (int)(m_owner->pos.y + posDiff.y))
				   .particle_pass) {
			if (rnd() < intensity) {
				if (m_type->laserSightBlender != NONE)
					gfx.setBlender(m_type->laserSightBlender, m_type->laserSightAlpha);
				putpixel(where, (int)(posDiff.x) + x, (int)(posDiff.y) + y, m_type->laserSightColour);
				solid_mode();
			}
			posDiff += inc;
			intensity += intensityInc;
		}
	}
}

void Weapon::drawTop(BITMAP *where, int x, int y) {
	if (m_type->skin) {
		m_type->skin->getSprite(0, m_owner->getAngle())->draw(where, x, y);
	}
}
#endif

void Weapon::recieveMessage(ZCom_BitStream *data) {
	Events event = static_cast<Events>(Encoding::decode(*data, EventsCount));
	switch (event) {
		case OUTOFAMMO: {
			outOfAmmo();
		} break;

		case RELOADED: {
			reload();
		} break;

		case SHOOT: {
			m_type->primaryShoot->run(m_owner, NULL, NULL, this);
			ammo--;
		} break;

		case OutOfAmmoCheck: {
			outOfAmmoCheck = true;
		} break;

		case AmmoCorrection: {
			ammo = Encoding::decode(*data, m_type->ammo + 1);
			m_outOfAmmo = false;
		}

		case EventsCount:
			break;
	}
}

void Weapon::outOfAmmo() {
	if (m_type->outOfAmmo)
		m_type->outOfAmmo->run(m_owner, NULL, NULL, this);
	ammo = 0;
	if (!reloading) {
		reloading = true;
		reloadTime = m_type->reloadTime;
	}
}

void Weapon::reload() {
	if (m_type->reloadEnd)
		m_type->reloadEnd->run(m_owner, NULL, NULL, this);
	reloading = false;
	ammo = m_type->ammo;
	m_outOfAmmo = false;
}

void Weapon::actionStart(Actions action) {
	switch (action) {
		case PRIMARY_TRIGGER:
			if (!inactiveTime && m_type->primaryPressed && ammo > 0) {
				m_type->primaryPressed->run(m_owner, NULL, NULL, this);
			}
			primaryShooting = true;
			break;

		case SECONDARY_TRIGGER:
			// TODO
			break;
	}
}

void Weapon::actionStop(Actions action) {
	switch (action) {
		case PRIMARY_TRIGGER:
			if (primaryShooting && m_type->primaryReleased) {
				m_type->primaryReleased->run(m_owner, NULL, NULL, this);
			}
			primaryShooting = false;
			break;

		case SECONDARY_TRIGGER:
			// TODO
			break;
	}
}

BaseWorm *Weapon::getOwner() {
	return m_owner;
}

void Weapon::delay(int time) {
	inactiveTime = time;
}

void Weapon::useAmmo(int amount) {
	ammo -= amount;
	if (ammo < 0)
		ammo = 0;
}

void Weapon::makeReference() {
	lua.pushFullReference(*this, metaTable);
}

void Weapon::finalize() {
	m_owner = 0;
	m_type = 0;
}
