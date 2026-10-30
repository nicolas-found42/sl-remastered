#include "owner_links.hpp"

void setLinkOwner(OwnerLink& link, LinkOwner* owner) {
	if (link.owner == owner) return;
	if (link.owner) {
		if (link.previous) link.previous->next = link.next;
		else link.owner->head = link.next;
		if (link.next) link.next->previous = link.previous;
	}
	link.previous = nullptr;
	link.next = nullptr;
	link.cachedValue = 0;
	link.owner = nullptr;
	if (owner) {
		link.next = owner->head;
		if (link.next) link.next->previous = &link;
		owner->head = &link;
		link.owner = owner;
		link.cachedValue = owner->value;
	}
}
