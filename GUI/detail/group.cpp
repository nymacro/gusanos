#include "group.h"

using std::cerr;
using std::cout;
using std::endl;

namespace OmfgGUI {

LuaReference Group::metaTable;

bool Group::render() {
	Renderer *renderer = context()->renderer();

	if (m_formatting.background.skin) {
		renderer->drawSkinnedBox(*m_formatting.background.skin, getRect(), m_formatting.background.color);
	} else {
		renderer->drawBox(getRect(), m_formatting.background.color, m_formatting.borders[0].color,
						  m_formatting.borders[1].color, m_formatting.borders[2].color, m_formatting.borders[3].color);
	}

	if (m_formatting.background.spriteSet) {
		renderer->drawSprite(*m_formatting.background.spriteSet, 0, getRect().centerX(), getRect().centerY());
	}

	return true;
}

void Group::process() {}

int Group::classID() {
	return Context::Group;
}

} // namespace OmfgGUI
