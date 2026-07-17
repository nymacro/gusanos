#ifndef OMFG_GUI_LIST_H
#define OMFG_GUI_LIST_H

#include "wnd.h"
#include "llist.h"
#include "luaapi/context.h"

#include <string>
#include <map>
#include <cassert>
#include <boost/lexical_cast.hpp>
using boost::lexical_cast;
//#include <list>

namespace OmfgGUI
{
	
class List;


struct ListNode : public LNodeImp<ListNode>
{
	static LuaReference metaTable;
	
	typedef LList<ListNode> list_t;
	typedef list_t::iterator node_iter_t;
	typedef list_t::reference node_ref_t;
	
	friend class List;
	
	ListNode(std::string const& text)
	: selected(false), expanded(true)
	, parent(0), visibleChildren(0), level(0)
	, list(0)
	, m_destroyed(false)
	{
		guiAliveSet().insert(this);
		columns.push_back(text);
	}
	
	virtual ~ListNode();
	
	node_iter_t push_back(ListNode* node)
	{
		children.insert(node);
		
		node->level = level + 1;
		node->parent = this;
		changeChildrenCount(1);

		// Keep the child node alive for the GC (see m_ownedNodes). The child's
		// luaReference is assigned by the creation macro before push_back runs.
		if(node->luaReference)
		{
			lua.pushWeakReference(node->luaReference);
			m_ownedNodes[node] = lua.createReference();
		}

		return node_iter_t(node);
	}
	
	void resizeColumns(size_t s);
	
	void render(Renderer* renderer, long& y);

	void renderFrom(Renderer* renderer, long& y);
	
	static node_iter_t getPrevVisible(node_iter_t i);
	static node_iter_t getNextVisible(node_iter_t i);
	static int findOffsetTo(node_iter_t i, node_iter_t to);
	static node_iter_t findRelative(node_iter_t i, int aIdx);
	
	void* operator new(size_t count);
	
	void operator delete(void* block)
	{
		// Lua frees the memory
	}
	
	void* operator new(size_t count, void* space)
	{
		return space;
	}
	
	void setText(unsigned int column, std::string const& text)
	{
		if(column < columns.size())
			columns[column] = text;
	}
	
	std::string getText(unsigned int column)
	{
		if(column < columns.size())
			return columns[column];
		else
			return "";
	}
	
	std::vector<std::string> const& getFields()
	{
		return columns;
	}
	
	void changeChildrenCount(long change);
	
	void clearSelections();
	
	//std::string text;
	std::vector<std::string> columns;
	bool        selected;
	bool        expanded;
	ListNode*   parent;
	long        visibleChildren;
	long        level;
	List*       list;
	list_t      children;
	LuaReference luaReference;
	LuaReference luaData;

	// Strong Lua references this node holds to keep its child nodes alive for
	// the garbage collector. The C++ parent (this node) owns the child's
	// lifetime, but Lua cannot see that edge, so without an explicit reference
	// a child node whose only other reference was transient would be collected
	// and vanish from the list with a dangling C++ pointer. Released in the
	// destructor (and in List::clear).
	std::map<ListNode*, LuaReference> m_ownedNodes;

	bool m_destroyed;};

class List : public Wnd
{
public:
	static LuaReference metaTable;

	// typedef std::list<Node> list_t;
	typedef LList<ListNode> list_t;
	typedef list_t::iterator node_iter_t;
	typedef list_t::reference node_ref_t;
	static const long rowHeight = 12;
	
	struct ColumnHeader
	{
		ColumnHeader(std::string const& name_, double widthFactor_)
		: name(name_), widthFactor(widthFactor_)
		{
		}
		
		std::string name;
		double      widthFactor;
	};

	friend struct ListNode;
	
	List(Wnd* parent, /*std::string const& tagLabel, std::string const& className,
	  std::string const& id,*/ std::map<std::string, std::string> const& attributes)
	: Wnd(parent, attributes, "list"), m_RootNode("root")
	, m_Base(0), m_basePos(0), m_MainSel(0), m_totalWidthFactor(0.0)
	, m_visibleChildren(0)
	{
		assert(!m_RootNode.parent);
		m_RootNode.list = this;
		m_RootNode.setNext(0);
		m_RootNode.setPrev(0);
	}
	
	void addColumn(ColumnHeader const& column);
	
	node_iter_t verify(node_iter_t i);
	
	node_iter_t push_back(ListNode* node)
	{
		node->list = this;
		node->columns.resize(m_columnHeaders.size());
		m_RootNode.children.insert(node);

		// Keep the node alive for the GC (the root owns it). The node's
		// luaReference is assigned by the creation macro before push_back runs.
		if(node->luaReference)
		{
			lua.pushWeakReference(node->luaReference);
			m_RootNode.m_ownedNodes[node] = lua.createReference();
		}

		++m_visibleChildren;
		node_iter_t i(node);
		i->parent = 0;
		
		if(!m_Base)
			m_Base = i;
		if(!m_MainSel)
			m_MainSel = i;
			
		return i;
	}
	
	node_iter_t push_back(ListNode* node, ListNode* parent)
	{
		node->list = this;
		node->columns.resize(m_columnHeaders.size());
		node_iter_t i = parent->push_back(node);
		
		return i;
	}
	
	void expand(node_iter_t i)
	{
		if(i->children.begin())
		{
			if(i->expanded)
			{
				i->expanded = false;
				if(i->parent)
					i->parent->changeChildrenCount(-i->visibleChildren);
				else
					m_visibleChildren -= i->visibleChildren;
				
				setBase(m_Base);
			}
			else
			{
				i->expanded = true;
				if(i->parent)
					i->parent->changeChildrenCount(i->visibleChildren);
				else
					m_visibleChildren += i->visibleChildren;
				
				setBase(m_Base);
				
				int offs = ListNode::findOffsetTo(m_RootNode.children.begin(), i);
				
				if(offs < m_basePos)
					setBase(i, offs);
				else if(offs + i->visibleChildren >= m_basePos + visibleRows())
					setBasePos(offs + i->visibleChildren - visibleRows() + 1);
			}
		}
	}
	
	void scroll(long amount)
	{
		if(m_Base)
		{
			node_iter_t i = ListNode::findRelative(m_Base, amount);
			m_Base = i;
		}
	}
	
	void clear()
	{
		m_MainSel = m_Base = node_iter_t(0);
		m_visibleChildren = 0;

		// Release the strong GC ownership references the root holds for its
		// direct children. The child nodes are finalized by their own __gc
		// (Lua owns the memory), so we only orphan the C++ links here.
		for(std::map<ListNode*, LuaReference>::iterator i = m_RootNode.m_ownedNodes.begin(), e = m_RootNode.m_ownedNodes.end(); i != e; ++i)
			lua.destroyReference(i->second);
		m_RootNode.m_ownedNodes.clear();

		m_RootNode.children.clear();
	}
	
	struct NumericLT
	{
		NumericLT(unsigned int column_)
		: column(column_)
		{
			
		}
		
		bool operator()(ListNode* a, ListNode* b)
		{
			return lexical_cast<int>(a->getText(column))
				> lexical_cast<int>(b->getText(column));
		}
		
		unsigned int column;
	};
	
	void sortNumerically(int byColumn)
	{
		NumericLT criteria(byColumn);
		m_RootNode.children.sort(criteria);
		
		m_basePos = 0;
		m_Base = m_RootNode.children.begin();
	}
	
	struct LuaLT
	{
		LuaLT(LuaContext& context_, LuaReference comparer)
		: context(context_), comp(comparer)
		{
			
		}
		
		bool operator()(ListNode* a, ListNode* b);
		
		LuaContext& context;
		LuaReference comp;
	};
	
	void sortLua(LuaReference comparer);
	
	bool isValid()
	{
		return false;
	}

	void setMainSel(node_iter_t iter);
	
	bool checkSelection();
	
	node_iter_t getMainSel()
	{
		return m_MainSel;
	}
	
	void updateBase()
	{
		m_Base = ListNode::findRelative(m_RootNode.children.begin(), m_basePos);
	}
	
	void scrollBottom();
	
	void setBasePos(int pos)
	{
		if(pos > m_visibleChildren - visibleRows())
			pos = m_visibleChildren - visibleRows();
		if(pos < 0)
			pos = 0;
		m_basePos = pos;
		updateBase();
	}
	
	void setBase(node_iter_t i)
	{
		int pos = ListNode::findOffsetTo(m_RootNode.children.begin(), i);
		setBase(i, pos);
	}
	
	// This assumes that pos is the position of i !
	// Don't use it unless you know this is the case.
	void setBase(node_iter_t i, int pos)
	{
		m_basePos = pos;
		if(pos > m_visibleChildren - visibleRows())
			pos = m_visibleChildren - visibleRows();
		if(pos < 0)
			pos = 0;
			
		if(pos != m_basePos)
		{
			m_basePos = pos;
			updateBase();
		}
		else
			m_Base = i;
	}
	
	int visibleRows()
	{
		return getRect().getHeight() / rowHeight - 1;
	}
	
	node_iter_t getFirstNode()
	{
		return m_RootNode.children.begin();
	}
	
	virtual bool render();
	virtual bool mouseDown(ulong newX, ulong newY, Context::MouseKey::type button);
	
	virtual bool mouseScroll(ulong newX, ulong newY, int offs);
	
	virtual bool keyDown(int key);
	
	virtual void applyFormatting(Context::GSSpropertyMap const&);

	virtual int classID();
	
private:
	bool verify_(node_iter_t i, node_iter_t n);
	
	struct ListFormatting
	{
		ListFormatting()
		: headerColor(RGB(170, 170, 255))
		, selectionColor(RGB(170, 170, 255))
		, selectionFrameColor(RGB(0, 0, 0))
		, indent(3.0)
		{
			
		}

		
		RGB headerColor;
		RGB selectionColor;
		RGB selectionFrameColor;
		double indent;

	} m_listFormatting;
	
	//list_t           m_Nodes;
	ListNode         m_RootNode;
	node_iter_t      m_Base;
	int              m_basePos;
	node_iter_t      m_MainSel;
	std::vector<ColumnHeader> m_columnHeaders;
	double           m_totalWidthFactor;
	int m_visibleChildren;
};

}

#endif //OMFG_GUI_LIST_H
