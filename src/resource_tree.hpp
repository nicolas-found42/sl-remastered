#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Host-only handles; these are not stock resource IDs or native pointers.
struct ResourceTreeHandle {
	uint64_t id = 0;
};
inline bool operator==(ResourceTreeHandle a, ResourceTreeHandle b) { return a.id == b.id; }

class ResourceTree {
public:
	ResourceTree();
	ResourceTree(const ResourceTree&) = delete;
	ResourceTree& operator=(const ResourceTree&) = delete;
	ResourceTreeHandle root() const { return root_; }
	bool contains(ResourceTreeHandle handle) const;
	std::optional<ResourceTreeHandle> add(ResourceTreeHandle parent);
	bool move(ResourceTreeHandle handle, ResourceTreeHandle newParent);
	bool remove(ResourceTreeHandle handle);
	std::optional<ResourceTreeHandle> parent(ResourceTreeHandle handle) const;
	std::optional<std::vector<ResourceTreeHandle>> children(ResourceTreeHandle handle) const;
private:
	struct Node {
		ResourceTreeHandle handle;
		ResourceTreeHandle parent;
		std::vector<ResourceTreeHandle> children;
	};
	Node* find(ResourceTreeHandle handle);
	const Node* find(ResourceTreeHandle handle) const;
	std::vector<Node> nodes_;
	ResourceTreeHandle root_;
};
