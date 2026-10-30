#include "owner_links.hpp"

int main() {
	LinkOwner first, second;
	first.value = 0x12345678;
	second.value = 0xabcdef01;
	OwnerLink a, b, c;
	setLinkOwner(a, &first);
	if (first.head != &a || a.owner != &first || a.cachedValue != first.value || a.previous || a.next) return 1;
	setLinkOwner(b, &first);
	setLinkOwner(c, &first);
	if (first.head != &c || c.next != &b || b.previous != &c || b.next != &a || a.previous != &b) return 1;
	first.value = 7;
	setLinkOwner(b, &first);
	if (first.head != &c || b.cachedValue != 0x12345678 || b.previous != &c || b.next != &a) return 1;
	setLinkOwner(b, &second);
	if (c.next != &a || a.previous != &c || second.head != &b || b.previous || b.next ||
		b.owner != &second || b.cachedValue != second.value) return 1;
	setLinkOwner(c, nullptr);
	if (first.head != &a || a.previous || c.owner || c.previous || c.next || c.cachedValue) return 1;
	setLinkOwner(a, nullptr);
	if (first.head || a.owner || a.previous || a.next || a.cachedValue) return 1;
	setLinkOwner(a, nullptr);
	setLinkOwner(b, nullptr);
	if (first.head || second.head || b.owner || b.cachedValue) return 1;
	return 0;
}
