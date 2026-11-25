// Tests for the nightly auto sleep timer. The firmware dependencies that
// reach the filesystem and the wall clock are replaced here; the timer logic
// under test is the same source the player builds.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/playback/sleeptimer.h"

static bool stub_clock_set;
static long stub_auto_enabled;
static long stub_auto_start;
static long stub_auto_end;
static long stub_minutes;
static char writes[16][96];
static int write_count;

#define CHECK(condition)                                                                                     \
	do {                                                                                                     \
		if (!(condition)) {                                                                                  \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                             \
			exit(1);                                                                                         \
		}                                                                                                    \
	} while (0)

long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	if (!strcmp(key, "sleep_minutes")) return stub_minutes;
	if (!strcmp(key, "auto_sleep_timer")) return stub_auto_enabled;
	if (!strcmp(key, "auto_sleep_start")) return stub_auto_start;
	if (!strcmp(key, "auto_sleep_end")) return stub_auto_end;
	return fallback;
}

void config_set_int(const char *section, const char *key, long value) {
	if (write_count < (int)(sizeof(writes) / sizeof(writes[0]))) {
		snprintf(writes[write_count], sizeof(writes[0]), "%s/%s=%ld", section, key, value);
	}
	write_count++;
}

bool config_save(void) { return true; }

bool clock_is_set(void) { return stub_clock_set; }

static void test_span(void) {
	// 22:00 to 06:00 crosses midnight.
	CHECK(sleeptimer_span_contains(1380, 1320, 360)); // 23:00
	CHECK(sleeptimer_span_contains(359, 1320, 360));  // 05:59
	CHECK(!sleeptimer_span_contains(1319, 1320, 360)); // 21:59
	CHECK(!sleeptimer_span_contains(360, 1320, 360));  // 06:00

	// Equal endpoints mean every hour.
	CHECK(sleeptimer_span_contains(0, 720, 720));
	CHECK(sleeptimer_span_contains(1439, 720, 720));
}

static void test_no_arm_without_clock(void) {
	stub_clock_set = false;
	stub_auto_enabled = 1;
	stub_auto_start = 0;
	stub_auto_end = 0;
	stub_minutes = 30;
	sleeptimer_init();
	sleeptimer_note_playing(SLEEPTIMER_MUSIC);
	sleeptimer_check_auto();
	CHECK(!sleeptimer_running(SLEEPTIMER_MUSIC));
}

static void test_auto_arms_and_restarts_on_activity(void) {
	stub_clock_set = true;
	stub_auto_enabled = 1;
	stub_auto_start = 0;
	stub_auto_end = 0;
	stub_minutes = 30;
	sleeptimer_init();
	sleeptimer_note_playing(SLEEPTIMER_MUSIC);
	sleeptimer_check_auto();
	CHECK(sleeptimer_running(SLEEPTIMER_MUSIC));

	sleeptimer_note_activity();
	CHECK(sleeptimer_running(SLEEPTIMER_MUSIC));
	CHECK(sleeptimer_remaining(SLEEPTIMER_MUSIC) == 30 * 60);
}

static void test_activity_without_playback(void) {
	stub_clock_set = true;
	stub_auto_enabled = 1;
	stub_auto_start = 0;
	stub_auto_end = 0;
	stub_minutes = 30;
	sleeptimer_init();
	sleeptimer_note_activity();
	CHECK(!sleeptimer_running(SLEEPTIMER_MUSIC));
	CHECK(!sleeptimer_running(SLEEPTIMER_AUDIOBOOK));
}

static void test_auto_set_leaves_manual_keys_alone(void) {
	stub_auto_enabled = 0;
	stub_auto_start = 0;
	stub_auto_end = 0;
	sleeptimer_init();
	write_count = 0;
	sleeptimer_auto_t value = {.enabled = true, .start = 1320, .end = 360};
	sleeptimer_auto_set(&value);
	CHECK(write_count == 3);
	for (int i = 0; i < write_count; i++) {
		CHECK(strncmp(writes[i], "player/auto_sleep_", 18) == 0);
	}
}

int main(void) {
	test_span();
	test_no_arm_without_clock();
	test_auto_arms_and_restarts_on_activity();
	test_activity_without_playback();
	test_auto_set_leaves_manual_keys_alone();
	puts("auto sleep timer tests passed");
	return 0;
}
