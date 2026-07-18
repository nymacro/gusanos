#ifndef GUI_TESTS_STUB_RENDERER_H
#define GUI_TESTS_STUB_RENDERER_H

#include "detail/renderer.h"

namespace GuiTests
{

// No-op Renderer for headless GUI tests. The baseline tests do not exercise
// any rendering paths, but Context requires a Renderer* in its constructor.
class StubRenderer : public OmfgGUI::Renderer
{
public:
	virtual void drawBox(
		Rect const& rect,
		OmfgGUI::RGB const& color,
		OmfgGUI::RGB const& borderLeftColor,
		OmfgGUI::RGB const& borderTopColor,
		OmfgGUI::RGB const& borderRightColor,
		OmfgGUI::RGB const& borderBottomColor) override
	{}

	virtual void drawFrame(
		Rect const& rect,
		OmfgGUI::RGB const& color) override
	{}

	virtual void drawBox(
		Rect const& rect,
		OmfgGUI::RGB const& color) override
	{}

	virtual void drawVLine(ulong x, ulong y1, ulong y2, OmfgGUI::RGB const& color) override
	{}

	virtual void drawText(OmfgGUI::BaseFont const& font, std::string const& str, ulong flags, ulong x, ulong y, OmfgGUI::RGB const& aColor) override
	{}

	virtual std::pair<int, int> getTextDimensions(OmfgGUI::BaseFont const& font, std::string::const_iterator b, std::string::const_iterator e) override
	{
		return std::make_pair(0, 0);
	}

	virtual int getTextCoordToIndex(OmfgGUI::BaseFont const& font, std::string::const_iterator b, std::string::const_iterator e, int x) override
	{
		return 0;
	}

	virtual void drawSprite(OmfgGUI::BaseSpriteSet const& spriteSet, int frame, ulong x, ulong y) override
	{}

	virtual void drawSprite(OmfgGUI::BaseSpriteSet const& spriteSet, int frame, ulong x, ulong y, ulong left, ulong top, ulong bottom, ulong right) override
	{}

	virtual void setClip(Rect const& rect) override
	{
		m_clip = rect;
	}

	virtual Rect const& getClip() override
	{
		return m_clip;
	}

	virtual Rect const& getViewportRect() override
	{
		return m_viewport;
	}

	virtual void setAddBlender(int alpha) override
	{}

	virtual void setAlphaBlender(int alpha) override
	{}

	virtual void resetBlending() override
	{}

	virtual void drawSkinnedBox(OmfgGUI::BaseSpriteSet const& skin, Rect const& rect, OmfgGUI::RGB const& backgroundColor) override
	{}

private:
	Rect m_clip;
	Rect m_viewport;
};

}

#endif // GUI_TESTS_STUB_RENDERER_H
