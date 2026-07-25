#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include "net_node.h"
#include <cstdint>
#include <iterator>
#include <list>
#include <unordered_map>

class NodeRegistry {
  public:
	bool insert(ZCom_Node *node);
	void remove(ZCom_Node *node);
	void clear();

	ZCom_Node *find(uint32_t nodeID) const;
	bool contains(const ZCom_Node *node) const;
	std::size_t size() const;

	// Pre-advances the iterator before invoking f, so f may destroy node.
	template <typename F>
	void forEach(F &&f) {
		for (auto it = m_nodes.begin(); it != m_nodes.end();) {
			ZCom_Node *node = it->node;
			++it;
			f(node);
		}
	}

	template <typename F>
	void forEach(F &&f) const {
		for (auto it = m_nodes.begin(); it != m_nodes.end();) {
			ZCom_Node *node = it->node;
			++it;
			f(node);
		}
	}

  private:
	struct Entry {
		ZCom_Node *node;
	};

	std::list<Entry> m_nodes;
	std::unordered_map<uint32_t, ZCom_Node *> m_byId;
	std::unordered_map<const void *, std::list<Entry>::iterator> m_byPtr;
};

#endif
