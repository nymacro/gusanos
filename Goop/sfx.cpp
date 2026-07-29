#ifndef DEDSERV

#include "sfx.h"
#include "gconsole.h"
#include "base_object.h"

#include <vector>
#include <list>
#include <memory>
#include "fmod_compat.h"
#include <boost/utility.hpp>

using namespace std;

Sfx sfx;

namespace {
bool m_initialized = false;

std::list<std::pair<int, BaseObject *>> chanObject;
std::vector<std::unique_ptr<Listener>> listeners;

int m_volume;
int m_listenerDistance;
} // namespace

void volume(int oldValue) {
	if (sfx)
		sfx.volumeChange();
}

Sfx::Sfx() {}

Sfx::~Sfx() {}

void Sfx::init() {
	FSOUND_Init(44100, 32, 0);
	volumeChange();

	console.addLogMsg("* SDL3_mixer initialized");
	m_initialized = true;
}

void Sfx::shutDown() {
	FSOUND_Close();
}

void Sfx::registerInConsole() {
	console.registerVariables()("SFX_VOLUME", &m_volume, 255, volume)("SFX_LISTENER_DISTANCE", &m_listenerDistance, 20);

	// NOTE: When/if adding a callback to sfx variables, make it do nothing if
	// sfx.operator bool() returns false.
}

void Sfx::think() {
	FSOUND_Update();

	// Update listener position from the first active listener (the local worm).
	for (size_t i = 0; i < listeners.size(); ++i) {
		FSOUND_3D_Listener_SetCurrent(i, listeners.size());
		float pos[3] = {listeners[i]->pos.x, listeners[i]->pos.y, -static_cast<float>(m_listenerDistance)};
		FSOUND_3D_Listener_SetAttributes(pos, NULL, 0, 0, 1, 0, 1, 0);
	}

	// Update 3d channel that follow objects positions
	for (auto obj = chanObject.begin(), obj_end = chanObject.end(); obj != obj_end;) {
		auto next = obj;
		++next;
		if (!obj->second || obj->second->deleteMe || !FSOUND_IsPlaying(obj->first)) {
			chanObject.erase(obj);
		} else {
			float pos[3] = {obj->second->pos.x, obj->second->pos.y, 0};
			FSOUND_3D_SetAttributes(obj->first, pos, NULL);
		}
		obj = next;
	}
}

void Sfx::setChanObject(int chan, BaseObject *object) {
	chanObject.push_back(pair<int, BaseObject *>(chan, object));
}

void Sfx::clear() {
	chanObject.clear();
}

Listener *Sfx::newListener() {
	listeners.push_back(std::unique_ptr<Listener>(new Listener));
	return listeners.back().get();
}

void Sfx::freeListener(Listener *listener) {
	for (auto i = listeners.begin(); i != listeners.end(); ++i) {
		if (i->get() == listener) {
			listeners.erase(i);
			break;
		}
	}
}

void Sfx::volumeChange() {
	FSOUND_SetSFXMasterVolume(m_volume);
}

Sfx::operator bool() {
	return m_initialized;
}

#endif
