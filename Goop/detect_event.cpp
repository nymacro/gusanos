#include "detect_event.h"

#include "events.h"
#include "base_object.h"
#include "game.h"

DetectEvent::DetectEvent(float range, bool detectOwner, int detectFilter)
	: m_range(range), m_detectOwner(detectOwner), m_detectFilter(detectFilter) {}

DetectEvent::DetectEvent(std::vector<BaseAction *> &actions_, float range, bool detectOwner, int detectFilter)
	: Event(actions_), m_range(range), m_detectOwner(detectOwner), m_detectFilter(detectFilter) {}

DetectEvent::~DetectEvent() {}

void DetectEvent::check(BaseObject *ownerObject) {
	int x = int(ownerObject->pos.x);
	int y = int(ownerObject->pos.y);
	int radius = int(m_range);
	int x1 = x - radius;
	int y1 = y - radius;
	int x2 = x + radius;
	int y2 = y + radius;

	if (m_detectFilter & 1) // 1 is the worm collision layer flag
	{
		for (auto worm = game.objects.beginColLayer(Grid::WormColLayer); worm; ++worm) {
			if (&*worm != ownerObject) {
				if (m_detectOwner || worm->getOwner() != ownerObject->getOwner())
					if (worm->isCollidingWith(ownerObject->pos, m_range)) {
						run(ownerObject, &*worm);
					}
			}
		}
	}

	for (int customFilter = Grid::CustomColLayerStart, filterFlag = 2; customFilter < Grid::ColLayerCount;
		 ++customFilter, filterFlag *= 2) {
		if (m_detectFilter & filterFlag) {
			for (auto object = game.objects.beginArea(x1, y1, x2, y2, customFilter); object; ++object) {
				if (&*object != ownerObject) {
					if (!object->deleteMe && (m_detectOwner || object->getOwner() != ownerObject->getOwner()))
						if (object->isCollidingWith(ownerObject->pos, m_range)) {
							run(ownerObject, &*object);
						}
				}
			}
		}
	}
}
