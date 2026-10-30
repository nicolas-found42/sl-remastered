#include "natives/resource_duplicate.hpp"

#include <memory>

int main() {
	VmResourceState source{};
	VmResourceState target{};
	source.attachment = std::make_shared<VmResourceAttachment>();
	source.attachment->type = 1;
	target.attachment = std::make_shared<VmResourceAttachment>();
	if (!resourceDuplicateShared(target, &source) || target.attachment != source.attachment) return 1;
	auto shared = target.attachment;
	source.attachment.reset();
	if (target.attachment != shared) return 2;
	if (!resourceDuplicateShared(target, nullptr) || target.attachment != shared) return 3;
	if (!resourceDuplicateShared(target, &source) || target.attachment) return 4;
	for (uint32_t type : {5u, 7u, 13u, 14u}) {
		source.attachment = std::make_shared<VmResourceAttachment>();
		source.attachment->type = type;
		target.attachment = shared;
		if (resourceDuplicateShared(target, &source) || target.attachment != shared) return 5;
	}
	return 0;
}
