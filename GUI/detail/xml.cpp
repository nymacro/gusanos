#include "xml-grammar.h"
#include "context.h"
#include "wnd.h"
#include "list.h"
#include "button.h"
#include "edit.h"
#include "group.h"
#include "check.h"
#include <sstream>
#include <iostream>
#include <utility>
#include "luaapi/types.h"
#include "luaapi/context.h"

using namespace std;

namespace OmfgGUI {

struct XMLHandler {
	struct Tag {
		Tag(std::string const &label_) : label(label_) {}

		std::string label;
		std::map<std::string, std::string> attributes;
	};

	struct WndInfo {
		WndInfo(Wnd *wnd_) : wnd(wnd_) {}

		Wnd *wnd;
	};

	XMLHandler(Context &context_, Wnd *dest_) : tag(""), context(context_), firstWindow(0) {
		windows.push(WndInfo(dest_));
	}

	void error(std::string err) {
		cerr << err << endl;
	}

	void beginTag(std::string const &label) {
		tag = Tag(label);
	}

	void beginAttributes() {}

	void attribute(std::string const &name, std::string const &value) {
		tag.attributes[name] = value;
	}

	std::string const &getAttrib(std::string const &name, std::string const &def) {
		std::map<std::string, std::string>::iterator i = tag.attributes.find(name);
		if (i == tag.attributes.end())
			return def;
		return i->second;
	}

	void endAttributes() {
		Wnd *newWindow = 0;

		if (tag.label == "window") {
			newWindow = lua_new_weak(Wnd, (windows.top().wnd, tag.attributes), lua);
		} else if (tag.label == "list") {
			newWindow = lua_new_weak(List, (windows.top().wnd, tag.attributes), lua);
		} else if (tag.label == "button") {
			newWindow = lua_new_weak(Button, (windows.top().wnd, tag.attributes), lua);
		} else if (tag.label == "group") {
			newWindow = lua_new_weak(Group, (windows.top().wnd, tag.attributes), lua);
		} else if (tag.label == "edit") {
			newWindow = lua_new_weak(Edit, (windows.top().wnd, tag.attributes), lua);
		} else if (tag.label == "check") {
			newWindow = lua_new_weak(Check, (windows.top().wnd, tag.attributes), lua);
		}

		if (!windows.top().wnd) {
			context.setRoot(newWindow);
		} else {
			// Parenting is done here (not in the Wnd constructor) so that the
			// window's luaReference is already assigned by the creation macro;
			// addChild uses it to install the strong GC ownership reference.
			windows.top().wnd->addChild(newWindow);
		}

		if (newWindow) {
			if (!firstWindow)
				firstWindow = newWindow;
			windows.push(WndInfo(newWindow)); // Done last
		}
	}

	void endTag(std::string const &label) {
		windows.pop();
	}

	Tag tag;

	std::stack<WndInfo> windows;
	Context &context;
	Wnd *firstWindow;
};

Wnd *Context::buildFromXML(std::istream &s, Wnd *dest) {
	if (dest && dest->m_context != this) {
		return 0; // The destination window belongs to a different context
	}

	XMLHandler handler(*this, dest);
	xmlDocument(s, handler);
	return handler.firstWindow;
}

} // namespace OmfgGUI
