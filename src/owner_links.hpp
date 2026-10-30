#pragma once

#include <cstdint>

struct OwnerLink;

// Owners and links must remain at stable addresses while attached. Detach before destroying either.
struct LinkOwner {
	LinkOwner() = default;
	LinkOwner(const LinkOwner&) = delete;
	LinkOwner& operator=(const LinkOwner&) = delete;
	OwnerLink* head = nullptr;
	uint32_t value = 0;
};

struct OwnerLink {
	OwnerLink() = default;
	OwnerLink(const OwnerLink&) = delete;
	OwnerLink& operator=(const OwnerLink&) = delete;
	OwnerLink* previous = nullptr;
	OwnerLink* next = nullptr;
	uint32_t cachedValue = 0;
	LinkOwner* owner = nullptr;
};

void setLinkOwner(OwnerLink& link, LinkOwner* owner);
