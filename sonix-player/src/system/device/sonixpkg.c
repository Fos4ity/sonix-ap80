#include "sonixpkg.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "src/system/core/utils.h"
#include "src/system/device/factoryreset.h"
#include "src/system/ebook/epub_arena.h"
#include "src/system/ebook/epub_zip.h"
#include "src/system/image/miniz/miniz_tinfl.h"

#define LFH_SIGNATURE 0x04034b50u
#define LFH_MIN 30u

// ---------------------------------------------------------------------------
// where and which version
// ---------------------------------------------------------------------------

static char root[512];
static bool root_known;

// The running program's full path. /proc/self/exe is not enough on the AP80:
// for a program on the card (vfat, mounted at /usr/data/mnt/sd_0 and reached
// through the /data symlink) the kernel gives "/sonix/sonix_player", the path
// inside the card. So the name it was started by (/proc/self/cmdline) and the
// card's usual places are tried too, and the one that is the same file as
// /proc/self/exe (device and inode) wins.
bool sonixpkg_exe_path(char *out, size_t size) {
	struct stat self;
	if (stat("/proc/self/exe", &self) != 0) {
		return false;
	}
	char candidates[4][512];
	int count = 0;

	FILE *f = fopen("/proc/self/cmdline", "rb");
	if (f) {
		size_t n = fread(candidates[count], 1, sizeof(candidates[0]) - 1, f);
		fclose(f);
		candidates[count][n] = '\0'; // argv[0] ends at the first NUL anyway
		if (candidates[count][0] == '/') {
			count++;
		}
	}
	ssize_t n = readlink("/proc/self/exe", candidates[count], sizeof(candidates[0]) - 1);
	if (n > 0) {
		candidates[count][n] = '\0';
		count++;
	}
	snprintf(candidates[count++], sizeof(candidates[0]), "/data/mnt/sd_0/sonix/sonix_player");
	snprintf(candidates[count++], sizeof(candidates[0]), "/usr/data/mnt/sd_0/sonix/sonix_player");

	for (int i = 0; i < count; i++) {
		struct stat st;
		if (stat(candidates[i], &st) == 0 && st.st_dev == self.st_dev && st.st_ino == self.st_ino) {
			snprintf(out, size, "%s", candidates[i]);
			return true;
		}
	}
	return false;
}

static void find_root(void) {
	if (root_known) {
		return;
	}
	root_known = true;
	root[0] = '\0';

	const char *forced = getenv("SONIX_UPDATE_ROOT");
	if (forced && *forced) {
		snprintf(root, sizeof(root), "%s", forced);
		return;
	}
	if (!factoryreset_beside_stock()) {
		return;
	}
	// <card>/sonix/sonix_player -> <card>
	char exe[512];
	if (!sonixpkg_exe_path(exe, sizeof(exe))) {
		printf("update: cannot tell where the program is; no updates\n");
		return;
	}
	if (strstr(exe, "/mnt/") == NULL) {
		return; // not on the card
	}
	char *slash = strrchr(exe, '/');
	if (!slash) {
		return;
	}
	*slash = '\0';
	slash = strrchr(exe, '/');
	if (!slash || strcmp(slash + 1, "sonix") != 0) {
		return; // started from a renamed copy (sonix-1.0/...): not ours to update
	}
	*slash = '\0';
	snprintf(root, sizeof(root), "%s", exe);
	printf("update: Sonix package on the card at %s\n", root);
}

bool sonixpkg_active(void) {
	find_root();
	return root[0] != '\0';
}

const char *sonixpkg_root(void) {
	find_root();
	return root;
}

const char *sonixpkg_version(void) {
	const char *fake = getenv("SONIX_UPDATE_FAKE_VERSION");
	return fake && *fake ? fake : SONIXPKG_VERSION;
}

// ---------------------------------------------------------------------------
// CRC-32 (the zip one)
// ---------------------------------------------------------------------------

static uint32_t crc_table[256];

static void crc_init(void) {
	if (crc_table[1]) {
		return;
	}
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) {
			c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		}
		crc_table[i] = c;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
	crc = ~crc;
	while (n--) {
		crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}
	return ~crc;
}

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------

static uint8_t io_buf[64 * 1024];

// True when `path` is a file of `size` bytes whose CRC-32 is `crc`.
static bool same_file(const char *path, uint32_t size, uint32_t crc) {
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != size) {
		return false;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	uint32_t c = 0;
	size_t n;
	while ((n = fread(io_buf, 1, sizeof(io_buf), f)) > 0) {
		c = crc_update(c, io_buf, n);
	}
	bool ok = !ferror(f);
	fclose(f);
	return ok && c == crc;
}

// mkdir -p for the folders above `path` (the last part is a file name).
static bool make_parents(const char *path) {
	char tmp[1300];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			printf("update: cannot create %s: %s\n", tmp, strerror(errno));
			return false;
		}
		*p = '/';
	}
	return true;
}

// Names the package may contain: "sonix.sh" and anything under "sonix/".
// Everything else (the .rockbox note) is left alone, and nothing may climb out.
static bool wanted_name(const char *name) {
	if (strstr(name, "..") || strchr(name, '\\') || name[0] == '/') {
		return false;
	}
	return strcmp(name, "sonix.sh") == 0 || strncmp(name, "sonix/", 6) == 0;
}

// Unpacks member `e` of `zip` into `out_path`, checking its CRC-32.
static bool unpack(const epub_zip_t *zip, const epub_zip_entry_t *e, const char *out_path) {
	uint8_t local[LFH_MIN];
	if (fseek(zip->file, (long)e->local_offset, SEEK_SET) != 0 || fread(local, 1, LFH_MIN, zip->file) != LFH_MIN) {
		return false;
	}
	uint32_t sig = (uint32_t)local[0] | ((uint32_t)local[1] << 8) | ((uint32_t)local[2] << 16) |
				   ((uint32_t)local[3] << 24);
	if (sig != LFH_SIGNATURE) {
		return false;
	}
	long data_at = (long)e->local_offset + (long)LFH_MIN + (local[26] | (local[27] << 8)) +
				   (local[28] | (local[29] << 8));
	if (fseek(zip->file, data_at, SEEK_SET) != 0) {
		return false;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		printf("update: cannot create %s: %s\n", out_path, strerror(errno));
		return false;
	}

	bool ok = true;
	uint32_t crc = 0;
	uint32_t written = 0;

	if (e->method == 0) {
		uint32_t left = e->comp_size;
		while (ok && left > 0) {
			size_t want = left < sizeof(io_buf) ? left : sizeof(io_buf);
			if (fread(io_buf, 1, want, zip->file) != want || fwrite(io_buf, 1, want, out) != want) {
				ok = false;
				break;
			}
			crc = crc_update(crc, io_buf, want);
			written += (uint32_t)want;
			left -= (uint32_t)want;
		}
	} else {
		// Streaming inflate through tinfl's 32 KB window: the program itself is
		// ten megabytes, and this player has 64 MB of RAM for everything.
		tinfl_decompressor *inf = tinfl_decompressor_alloc();
		uint8_t *dict = malloc(TINFL_LZ_DICT_SIZE);
		if (!inf || !dict) {
			ok = false;
		} else {
			tinfl_init(inf);
			uint32_t comp_left = e->comp_size;
			size_t in_avail = 0, in_ofs = 0, dict_ofs = 0;
			for (;;) {
				if (in_avail == 0 && comp_left > 0) {
					size_t want = comp_left < sizeof(io_buf) ? comp_left : sizeof(io_buf);
					if (fread(io_buf, 1, want, zip->file) != want) {
						ok = false;
						break;
					}
					in_ofs = 0;
					in_avail = want;
					comp_left -= (uint32_t)want;
				}
				size_t in_bytes = in_avail;
				size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
				tinfl_status st = tinfl_decompress(inf, io_buf + in_ofs, &in_bytes, dict, dict + dict_ofs, &out_bytes,
												   comp_left > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
				in_ofs += in_bytes;
				in_avail -= in_bytes;
				if (out_bytes) {
					if (fwrite(dict + dict_ofs, 1, out_bytes, out) != out_bytes) {
						ok = false;
						break;
					}
					crc = crc_update(crc, dict + dict_ofs, out_bytes);
					written += (uint32_t)out_bytes;
					dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
				}
				if (st == TINFL_STATUS_DONE) {
					break;
				}
				if (st < TINFL_STATUS_DONE ||
					(st == TINFL_STATUS_NEEDS_MORE_INPUT && in_avail == 0 && comp_left == 0)) {
					ok = false;
					break;
				}
			}
		}
		free(dict);
		if (inf) {
			tinfl_decompressor_free(inf);
		}
	}

	if (fflush(out) != 0 || fsync(fileno(out)) != 0) {
		ok = false;
	}
	if (fclose(out) != 0) {
		ok = false;
	}
	if (ok && (written != e->uncomp_size || crc != e->crc32)) {
		printf("update: %s unpacked wrong (%u bytes, crc %08x, expected %u, %08x)\n", out_path, written, crc,
			   e->uncomp_size, e->crc32);
		ok = false;
	}
	return ok;
}

// ---------------------------------------------------------------------------
// the install
// ---------------------------------------------------------------------------

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pkg_state_t state = PKG_IDLE;
static pkg_result_t result = PKG_OK;
static int progress_done, progress_total;
static int changed_count;
static char backup_name[128];
static char install_zip[600];

static void set_progress(int done, int total) {
	pthread_mutex_lock(&lock);
	progress_done = done;
	progress_total = total;
	pthread_mutex_unlock(&lock);
}

// "sonix-zaloha-1.1", or "-2", "-3"... when that folder is already there.
static void pick_backup_name(void) {
	char base[96];
	snprintf(base, sizeof(base), "sonix-zaloha-%s", sonixpkg_version());
	snprintf(backup_name, sizeof(backup_name), "%s", base);
	for (int i = 2; i < 100; i++) {
		char path[700];
		snprintf(path, sizeof(path), "%s/%s", root, backup_name);
		struct stat st;
		if (stat(path, &st) != 0) {
			return;
		}
		snprintf(backup_name, sizeof(backup_name), "%s-%d", base, i);
	}
}

pkg_result_t sonixpkg_install(const char *zip_path) {
	find_root();
	crc_init();
	changed_count = 0;
	backup_name[0] = '\0';
	if (!root[0]) {
		return PKG_NOT_PACKAGE;
	}

	epub_arena_t arena;
	arena_init(&arena, "update", 256 * 1024);
	epub_zip_t zip;
	if (!epub_zip_open(&zip, zip_path, &arena)) {
		printf("update: %s is not a zip\n", zip_path);
		arena_destroy(&arena);
		return PKG_NOT_PACKAGE;
	}

	uint8_t *todo = calloc(zip.count ? zip.count : 1, 1); // 1: unpacked as .new, waiting
	bool has_player = false;
	for (uint32_t i = 0; i < zip.count; i++) {
		if (strcmp(epub_zip_name(&zip, i), "sonix/sonix_player") == 0 && zip.entries[i].uncomp_size > 0) {
			has_player = true;
		}
	}
	if (!todo || !has_player) {
		printf("update: %s has no sonix/sonix_player\n", zip_path);
		free(todo);
		epub_zip_close(&zip);
		arena_destroy(&arena);
		return PKG_NOT_PACKAGE;
	}

	// 1. unpack what differs, beside the old files
	pkg_result_t r = PKG_OK;
	char path[1024], tmp[1100];
	int total = (int)zip.count;
	for (uint32_t i = 0; i < zip.count && r == PKG_OK; i++) {
		set_progress((int)i, total);
		const char *name = epub_zip_name(&zip, i);
		if (!wanted_name(name)) {
			continue;
		}
		const epub_zip_entry_t *e = &zip.entries[i];
		snprintf(path, sizeof(path), "%s/%s", root, name);
		if (same_file(path, e->uncomp_size, e->crc32)) {
			continue;
		}
		snprintf(tmp, sizeof(tmp), "%s.new", path);
		if (!make_parents(path) || !unpack(&zip, e, tmp)) {
			printf("update: unpacking %s failed\n", name);
			unlink(tmp);
			r = PKG_UNPACK_FAILED;
			break;
		}
		todo[i] = 1;
		changed_count++;
	}

	if (r != PKG_OK) {
		// Only our own .new files go; the card is as it was.
		for (uint32_t i = 0; i < zip.count; i++) {
			if (todo[i]) {
				snprintf(tmp, sizeof(tmp), "%s/%s.new", root, epub_zip_name(&zip, i));
				unlink(tmp);
			}
		}
		changed_count = 0;
		free(todo);
		epub_zip_close(&zip);
		arena_destroy(&arena);
		sync();
		return r;
	}

	// 2. move each old file into the backup folder, the new one into its place
	FILE *list = NULL;
	if (changed_count > 0) {
		pick_backup_name();
		snprintf(path, sizeof(path), "%s/%s/zmeny.txt", root, backup_name);
		if (make_parents(path)) {
			list = fopen(path, "w");
		}
		if (list) {
			time_t now = time(NULL);
			fprintf(list,
					"Sonix update from %s. Files the update replaced are kept here with their paths;\n"
					"copy one back to undo it. 'new' = the file was not on the card before.\n"
					"Date: %s\n",
					sonixpkg_version(), ctime(&now));
		}
	}

	int done = 0;
	for (uint32_t i = 0; i < zip.count; i++) {
		if (!todo[i]) {
			continue;
		}
		const char *name = epub_zip_name(&zip, i);
		snprintf(path, sizeof(path), "%s/%s", root, name);
		snprintf(tmp, sizeof(tmp), "%s.new", path);
		char old[1200];
		snprintf(old, sizeof(old), "%s/%s/%s", root, backup_name, name);

		struct stat st;
		bool existed = stat(path, &st) == 0;
		if (existed && (!make_parents(old) || rename(path, old) != 0)) {
			printf("update: cannot move %s to %s: %s\n", path, old, strerror(errno));
			r = PKG_PARTIAL;
			continue; // the old file stays in place, its .new beside it
		}
		if (rename(tmp, path) != 0) {
			printf("update: cannot put %s in place: %s\n", path, strerror(errno));
			if (existed) {
				rename(old, path); // the old one back
			}
			r = PKG_PARTIAL;
			continue;
		}
		if (strcmp(name, "sonix/sonix_player") == 0 || strcmp(name, "sonix.sh") == 0) {
			chmod(path, 0755);
		}
		if (list) {
			fprintf(list, "%s %s\n", existed ? "replaced" : "new     ", name);
		}
		done++;
		set_progress(total - (changed_count - done), total);
	}
	if (list) {
		fclose(list);
	}
	set_progress(total, total);

	printf("update: %d files changed, backup in %s, result %d\n", changed_count, backup_name[0] ? backup_name : "-",
		   (int)r);
	free(todo);
	epub_zip_close(&zip);
	arena_destroy(&arena);
	sync();
	return r;
}

// ---------------------------------------------------------------------------
// the worker
// ---------------------------------------------------------------------------

static void *install_thread(void *unused) {
	(void)unused;
	thread_be_background("sonix update");
	pkg_result_t r = sonixpkg_install(install_zip);
	pthread_mutex_lock(&lock);
	result = r;
	state = PKG_FINISHED;
	pthread_mutex_unlock(&lock);
	return NULL;
}

bool sonixpkg_install_start(const char *zip_path) {
	pthread_mutex_lock(&lock);
	if (state == PKG_RUNNING) {
		pthread_mutex_unlock(&lock);
		return false;
	}
	state = PKG_RUNNING;
	progress_done = 0;
	progress_total = 0;
	snprintf(install_zip, sizeof(install_zip), "%s", zip_path);
	pthread_mutex_unlock(&lock);

	pthread_t thread;
	if (pthread_create(&thread, NULL, install_thread, NULL) != 0) {
		pthread_mutex_lock(&lock);
		result = PKG_UNPACK_FAILED;
		state = PKG_FINISHED;
		pthread_mutex_unlock(&lock);
		return true;
	}
	pthread_detach(thread);
	return true;
}

pkg_state_t sonixpkg_state(void) {
	pthread_mutex_lock(&lock);
	pkg_state_t s = state;
	pthread_mutex_unlock(&lock);
	return s;
}

pkg_result_t sonixpkg_result(void) {
	pthread_mutex_lock(&lock);
	pkg_result_t r = result;
	pthread_mutex_unlock(&lock);
	return r;
}

void sonixpkg_acknowledge(void) {
	pthread_mutex_lock(&lock);
	if (state == PKG_FINISHED) {
		state = PKG_IDLE;
	}
	pthread_mutex_unlock(&lock);
}

void sonixpkg_progress(int *done, int *total) {
	pthread_mutex_lock(&lock);
	if (done) {
		*done = progress_done;
	}
	if (total) {
		*total = progress_total;
	}
	pthread_mutex_unlock(&lock);
}

int sonixpkg_changed(void) { return changed_count; }

const char *sonixpkg_backup_name(void) { return backup_name; }
