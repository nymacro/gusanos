#include "node_registry.h"
#include <cassert>

bool NodeRegistry::insert(ZCom_Node *node) {
	if (!node)
		return false;

	const void *ptr = node;
	if (m_byPtr.find(ptr) != m_byPtr.end())
		return false;

	uint32_t nodeID = node->getNetworkID();
	assert(nodeID != 0 && "NodeRegistry::insert called before node ID was assigned");

	auto it = m_nodes.insert(m_nodes.end(), Entry{node});
	m_byPtr[ptr] = it;
	m_byId[nodeID] = node;
	return true;
}

void NodeRegistry::remove(ZCom_Node *node) {
	if (!node)
		return;

	auto it = m_byPtr.find(node);
	if (it == m_byPtr.end())
		return;

	m_nodes.erase(it->second);
	m_byPtr.erase(it);
	m_byId.erase(node->getNetworkID());
}

void NodeRegistry::clear() {
	m_nodes.clear();
	m_byPtr.clear();
	m_byId.clear();
}

ZCom_Node *NodeRegistry::find(uint32_t nodeID) const {
	auto it = m_byId.find(nodeID);
	if (it == m_byId.end())
		return nullptr;
	return it->second;
}

bool NodeRegistry::contains(const ZCom_Node *node) const {
	return m_byPtr.find(node) != m_byPtr.end();
}

std::size_t NodeRegistry::size() const {
	return m_nodes.size();
}
