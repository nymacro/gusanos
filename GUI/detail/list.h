#ifndef OMFG_GUI_LIST_H
#define OMFG_GUI_LIST_H

#include "wnd.h"
#include "llist.h"

#include <string>
#include <boost/lexical_cast.hpp>
using boost::lexical_cast;
//#include <list>

namespace OmfgGUI
{

class List : public Wnd
{
public:
	struct Node;
	
	// typedef std::list<Node> list_t;
	typedef LList<Node> list_t;
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
	
	struct Node : public LNodeImp<Node>
	{
		friend class List;
		
		Node(std::string const& text)
		: selected(false), expanded(true)
		, parent(0), totalChildrenCount(0), level(0)
		{
			columns.push_back(text);
		}
		
		node_iter_t push_back(Node* node)
		{
			children.insert(node);
			
			node->level = level + 1;
			node->parent = this;
			changeChildrenCount(1);

			return node_iter_t(node);
		}
		
		void resizeColumns(size_t s);
		
		void render(Renderer* renderer, long& y, List& list);

		void renderFrom(Renderer* renderer, long& y, List& list);
		
		static List::node_iter_t getPrevVisible(node_iter_t i);
		static List::node_iter_t getNextVisible(node_iter_t i);
		static int findOffsetTo(node_iter_t i, node_iter_t to);
		static node_iter_t findRelative(node_iter_t i, int aIdx);
		
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
		
		void changeChildrenCount(long change)
		{
			if(expanded && parent)
				parent->changeChildrenCount(change);
			totalChildrenCount += change;
		}
		
		//std::string text;
		std::vector<std::string> columns;
		bool        selected;
		bool        expanded;
		//list_t*     parentList;
		Node*       parent;
		long        totalChildrenCount;
		//bool        hasParent;
		long        level;
		//TODO: columns

		list_t      children;
	};
	
	friend struct Node;
	
	List(Wnd* parent, std::string const& tagLabel, std::string const& className,
	  std::string const& id, std::map<std::string, std::string> const& attributes)
	: Wnd(parent, tagLabel, className, id, attributes, ""), m_RootNode("root")
	, m_Base(0), m_basePos(0), m_MainSel(0)
	{

	}
	
	void addColumn(ColumnHeader const& column);
	
	node_iter_t push_back(Node* node)
	{
		node->columns.resize(m_columnHeaders.size());
		node_iter_t i = m_RootNode.children.insert(node);
		node->parent = 0; // Just to be sure
		node->level = m_RootNode.level + 1;
		
		if(!m_Base)
			m_Base = node_iter_t(node);
		if(!m_MainSel)
			m_MainSel = node_iter_t(node);
			
		//++m_RowCount; // Root node is always visible

		return i;
	}
	
	void expand(node_iter_t i)
	{
		if(i->expanded)
		{
			i->expanded = false;
			if(i->parent)
				i->parent->changeChildrenCount(-i->totalChildrenCount);
		}
		else
		{
			i->expanded = true;
			if(i->parent)
				i->parent->changeChildrenCount(i->totalChildrenCount);
		}
	}
	
	void scroll(long amount)
	{
		if(m_Base)
		{
			node_iter_t i = Node::findRelative(m_Base, amount);
			m_Base = i;
		}
	}
	
	void clear()
	{
		m_MainSel = m_Base = node_iter_t(0);
		m_RootNode.totalChildrenCount = 0;
		m_RootNode.children.clear();
	}
	
	struct NumericLT
	{
		NumericLT(unsigned int column_)
		: column(column_)
		{
			
		}
		
		bool operator()(Node* a, Node* b)
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
		
		m_Base = m_RootNode.children.begin();
	}
	
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
		m_Base = Node::findRelative(m_RootNode.children.begin(), m_basePos);
	}
	
	void setBasePos(int pos)
	{
		m_basePos = pos;
		updateBase();
	}
	
	int visibleRows()
	{
		return getRect().getHeight() / rowHeight - 1;
	}
	
	virtual bool render(Renderer* renderer);
	virtual bool mouseDown(ulong newX, ulong newY, Context::MouseKey::type button);
	
	virtual bool keyDown(int key);
	
	virtual void applyFormatting(Context::GSSpropertyMap const&);

	virtual int classID();
	
private:
	struct ListFormatting
	{
		ListFormatting()
		: headerColor(RGB(170, 170, 255))
		, selectionColor(RGB(170, 170, 255))
		{
			
		}

		
		RGB headerColor;
		RGB selectionColor;

	} m_listFormatting;
	
	//list_t           m_Nodes;
	Node             m_RootNode;
	node_iter_t      m_Base;
	int              m_basePos;
	node_iter_t      m_MainSel;
	std::vector<ColumnHeader> m_columnHeaders;
};

}

#endif //OMFG_GUI_LIST_H
