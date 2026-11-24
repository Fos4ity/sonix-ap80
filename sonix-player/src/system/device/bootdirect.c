#include "bootdirect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/system/device/sonixpkg.h"

// The boot menu as the firmware installs it. SONIX_BOOTMENU names another
// file, for the simulator.
#define BOOTMENU_PATH "/usr/bin/bootloader.rb"
#define BOOTMENU_MAX (4 * 1024 * 1024)

static bool file_path(char *out, size_t size) {
	if (!sonixpkg_active()) {
		return false;
	}
	int n = snprintf(out, size, "%s/sonix/" BOOTDIRECT_FILE, sonixpkg_root());
	return n > 0 && (size_t)n < size;
}

bool bootdirect_applies(void) { return sonixpkg_active(); }

// A boot menu that reads the file has the file's name in it.
static bool menu_knows_file(void) {
	const char *path = getenv("SONIX_BOOTMENU");
	if (!path || !*path) {
		path = BOOTMENU_PATH;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	bool found = false;
	char *data = malloc(BOOTMENU_MAX);
	if (data) {
		size_t len = fread(data, 1, BOOTMENU_MAX, f);
		const char *needle = "sonix/" BOOTDIRECT_FILE;
		size_t nlen = strlen(needle);
		for (size_t i = 0; i + nlen <= len; i++) {
			if (data[i] == needle[0] && memcmp(data + i, needle, nlen) == 0) {
				found = true;
				break;
			}
		}
		free(data);
	}
	fclose(f);
	return found;
}

bool bootdirect_supported(void) {
	static int known = -1;
	if (known < 0) {
		known = bootdirect_applies() && menu_knows_file() ? 1 : 0;
		printf("bootdirect: boot menu %s the switch\n", known ? "reads" : "does not read");
	}
	return known == 1;
}

bool bootdirect_enabled(void) {
	char path[600];
	return file_path(path, sizeof(path)) && access(path, F_OK) == 0;
}

bool bootdirect_set(bool on) {
	char path[600];
	if (!file_path(path, sizeof(path))) {
		return false;
	}
	if (!on) {
		return unlink(path) == 0 || access(path, F_OK) != 0;
	}
	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	// The text is for whoever finds the file on the card; the boot menu only
	// looks at whether it exists.
	bool ok = fputs("Sonix starts without the boot menu while this file is here.\r\n"
					"Hold a button other than Power while the player starts to see the menu,\r\n"
					"or delete this file.\r\n",
					f) >= 0;
	ok = fclose(f) == 0 && ok;
	if (ok) {
		sync();
	}
	return ok;
}
