#include <windows.h>
#include <mmsystem.h>
#include <cstdio>

static bool command(const char* input) {
	char output[256]{};
	const MCIERROR result = mciSendStringA(input, output, sizeof output, nullptr);
	if (result) {
		char error[256]{};
		mciGetErrorStringA(result, error, sizeof error);
		std::printf("%s: error %lu %s\n", input, static_cast<unsigned long>(result), error);
		return false;
	}
	std::printf("%s: %s\n", input, output);
	return true;
}
int main() {
	std::printf("D: drive type %u (5 means CD-ROM)\n", GetDriveTypeA("D:\\"));
	if (command("open D: type cdaudio alias stockdisc wait")) {
		command("set stockdisc time format tmsf wait");
		command("status stockdisc number of tracks wait");
		command("status stockdisc length wait");
		command("close stockdisc wait");
	}
	return 0;
}
