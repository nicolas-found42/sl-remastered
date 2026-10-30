#include "resource_tree.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace {
std::atomic<uint64_t> nextHandle{1};
std::optional<ResourceTreeHandle> allocateHandle() {
	uint64_t value = nextHandle.load(std::memory_order_relaxed);
	while (value != std::numeric_limits<uint64_t>::max()) {
		if (nextHandle.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
			return ResourceTreeHandle{value};
	}
	return {};
}
}
ResourceTree::ResourceTree() {
	const auto handle = allocateHandle();
	if (!handle) throw std::overflow_error("resource tree handles exhausted");
	root_ = *handle;
	nodes_.push_back({root_, {}, {}});
}
ResourceTree::Node* ResourceTree::find(ResourceTreeHandle handle) {
	for (auto& node : nodes_) if (node.handle == handle) return &node;
	return nullptr;
}
const ResourceTree::Node* ResourceTree::find(ResourceTreeHandle handle) const {
	for (const auto& node : nodes_) if (node.handle == handle) return &node;
	return nullptr;
}
bool ResourceTree::contains(ResourceTreeHandle handle) const { return find(handle) != nullptr; }
std::optional<ResourceTreeHandle> ResourceTree::add(ResourceTreeHandle parentHandle) {
	if (!find(parentHandle)) return {};
	const auto allocated = allocateHandle();
	if (!allocated) return {};
	const ResourceTreeHandle child = *allocated;
	// Append the node first; parent pointers into the vector may change during insertion.
	nodes_.push_back({child, parentHandle, {}});
	try {
		find(parentHandle)->children.push_back(child);
	} catch (...) {
		nodes_.pop_back();
		throw;
	}
	return child;
}
bool ResourceTree::move(ResourceTreeHandle handle, ResourceTreeHandle newParentHandle) {
	if (handle == root_) return false;
	Node* node = find(handle);
	Node* newParent = find(newParentHandle);
	if (!node || !newParent) return false;
	if (node->parent == newParentHandle) return true;
	for (ResourceTreeHandle cursor = newParentHandle; cursor.id != 0;) {
		if (cursor == handle) return false;
		const Node* current = find(cursor);
		if (!current || cursor == root_) break;
		cursor = current->parent;
	}
	Node* oldParent = find(node->parent);
	if (!oldParent) return false;
	auto& siblings = oldParent->children;
	siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
	node->parent = newParentHandle;
	newParent->children.push_back(handle);
	return true;
}
bool ResourceTree::remove(ResourceTreeHandle handle) {
	const Node* node = find(handle);
	if (!node || handle == root_) return false;
	const ResourceTreeHandle parentHandle = node->parent;
	std::vector<ResourceTreeHandle> removed{handle};
	for (size_t i = 0; i < removed.size(); ++i) {
		const auto* current = find(removed[i]);
		removed.insert(removed.end(), current->children.begin(), current->children.end());
	}
	auto& siblings = find(parentHandle)->children;
	siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
	nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const Node& candidate) {
		return std::find(removed.begin(), removed.end(), candidate.handle) != removed.end();
	}), nodes_.end());
	return true;
}
std::optional<ResourceTreeHandle> ResourceTree::parent(ResourceTreeHandle handle) const {
	const Node* node = find(handle);
	if (!node || handle == root_) return {};
	return node->parent;
}
std::optional<std::vector<ResourceTreeHandle>> ResourceTree::children(ResourceTreeHandle handle) const {
	const Node* node = find(handle);
	if (!node) return {};
	return node->children;
}
