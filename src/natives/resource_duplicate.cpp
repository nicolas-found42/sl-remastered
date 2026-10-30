#include "natives/resource_duplicate.hpp"

bool resourceDuplicateShared(VmResourceState& receiver, const VmResourceState* source) {
	if (!source) return true;
	const auto attachment = source->attachment;
	if (attachment) {
		switch (attachment->type) {
		case 5:
		case 7:
		case 13:
		case 14:
			return false;
		}
	}
	receiver.attachment = attachment;
	return true;
}
