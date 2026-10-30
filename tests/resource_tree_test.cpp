#include "resource_tree.hpp"

#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main() {
	ResourceTree tree;
	ResourceTree other;
	const auto root = tree.root();
	CHECK(!tree.contains(other.root()) && !tree.add(other.root()));
	CHECK(tree.contains(root) && !tree.parent(root));
	const auto a = tree.add(root), b = tree.add(root), c = tree.add(root);
	CHECK(a && b && c);
	if (!a || !b || !c) return 1;
	CHECK(tree.children(root) == std::vector<ResourceTreeHandle>({*a, *b, *c}));
	const auto child = tree.add(*b);
	CHECK(child && tree.parent(*child) == *b);
	const auto grandchild = child ? tree.add(*child) : std::nullopt;
	CHECK(grandchild && !tree.move(*b, *grandchild) && !tree.move(root, *a));
	CHECK(tree.move(*child, *a) && tree.parent(*child) == *a);
	CHECK(tree.children(*b)->empty() && tree.children(*a) == std::vector<ResourceTreeHandle>({*child}));
	CHECK(tree.move(*child, *b) && tree.parent(*child) == *b);
	CHECK(tree.remove(*b));
	CHECK(tree.children(root) == std::vector<ResourceTreeHandle>({*a, *c}));
	CHECK(!tree.contains(*b) && child && !tree.contains(*child) && grandchild && !tree.contains(*grandchild));
	CHECK(!tree.add(*b) && !tree.remove(*b) && !tree.children(*b));
	const auto next = tree.add(root);
	CHECK(next && !(*next == *b) && !tree.contains(*b));
	CHECK(tree.remove(*a) && tree.children(root) == std::vector<ResourceTreeHandle>({*c, *next}));
	CHECK(tree.remove(*next) && tree.children(root) == std::vector<ResourceTreeHandle>({*c}));
	CHECK(tree.remove(*c) && tree.children(root)->empty());
	CHECK(!tree.remove(root) && tree.contains(root));
	return failures ? 1 : 0;
}
