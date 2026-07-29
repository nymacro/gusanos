#ifndef GUI_TESTS_STUB_CONTEXT_H
#define GUI_TESTS_STUB_CONTEXT_H

#include "detail/context.h"

namespace GuiTests {

// Minimal Context subclass for headless GUI tests. Overrides the pure virtual
// loaders to return null/no-op so the test can exercise Lua bindings without
// touching the filesystem or real rendering/font subsystems.
class StubContext : public OmfgGUI::Context {
  public:
	StubContext(OmfgGUI::Renderer *renderer) : OmfgGUI::Context(renderer) {}

	virtual bool keyState(int key) override {
		return false;
	}

	virtual OmfgGUI::BaseFont *loadFont(std::string const &name) override {
		(void)name;
		return nullptr;
	}

	virtual OmfgGUI::BaseSpriteSet *loadSpriteSet(std::string const &name) override {
		(void)name;
		return nullptr;
	}

	virtual void loadGSSFile(std::string const &name, bool passive) override {
		(void)name;
		(void)passive;
	}

	virtual OmfgGUI::Wnd *loadXMLFile(std::string const &name, OmfgGUI::Wnd *loadTo) override {
		(void)name;
		(void)loadTo;
		return nullptr;
	}
};

} // namespace GuiTests

#endif // GUI_TESTS_STUB_CONTEXT_H
