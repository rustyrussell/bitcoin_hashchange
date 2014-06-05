/* Simulate the effect of miners during transition. */
#include <ccan/isaac/isaac.h>
#include <ccan/time/time.h>
#include <ccan/str/str.h>
#include <ccan/opt/opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* How many 10-minute blocks in two weeks. */
#define BLOCKS_PER_FORTNIGHT 2016
/* How many seconds in two weeks. */
#define IDEAL_FORTNIGHT (14 * 24 * 60 * 60)

/* This gives approximately normal distribution noise. */
static int32_t add_noise(struct isaac_ctx *isaac)
{
	int i;
	uint32_t sum = 0;
	int32_t ret;

	for (i = 0; i < 12; i++)
		sum += (isaac_next_uint32(isaac) & 0x0FFFFFFF);

	ret = sum - 6 * 0x0FFFFFFF;

	/* Noise scaling found by trial and error to match fortnight
	 * 115.  This gives +/- 96. */
	ret /= 0x1000000;
	return ret;
}

/* Aim for roughly 10 minute averages by default */
#define DEFAULT_TARGET (0xFFFFFFFF / 600)

int main(int argc, char *argv[])
{
	struct isaac_ctx isaac;
	struct timespec ts = time_now();
	unsigned int old_target = DEFAULT_TARGET , new_target = DEFAULT_TARGET;
	unsigned long seed;
	unsigned int i, r, fortnights = 26, time, sha_fail = -1;
	int32_t *timestamp;
	bool noise = false, verbose = false;

	seed = (ts.tv_sec << 10) + ts.tv_nsec;

	opt_register_arg("--seed", opt_set_ulongval, NULL, &seed,
			 "Seed value for rng (default based on time)");
	opt_register_arg("--old-target", opt_set_uintval, opt_show_uintval,
			 &old_target, "Target difficulty for old hash");
	opt_register_arg("--new-target", opt_set_uintval, opt_show_uintval,
			 &new_target, "Target difficulty for new hash");
	opt_register_noarg("--noise", opt_set_bool,
			 &noise, "Add noise to timestamps");
	opt_register_arg("--fortnights", opt_set_uintval, opt_show_uintval,
			 &fortnights, "Number of fortnights to convert");
	opt_register_noarg("-v|--verbose", opt_set_bool,
			 &verbose, "Verbose output");
	opt_register_noarg("-h|--help", opt_usage_and_exit, "",
			   "This usage message");
	opt_register_arg("--sha-fail", opt_set_uintval, NULL, &sha_fail,
			 "Fortnight at which to drop SHA difficulty to 0");

	opt_parse(&argc, argv, opt_log_stderr_exit);
	if (argc != 1)
		opt_usage_and_exit(NULL);

	isaac_init(&isaac, (unsigned char *)&seed, sizeof(seed));
	timestamp = malloc(sizeof(*timestamp) * BLOCKS_PER_FORTNIGHT);

	if (verbose)
		printf("  Seed is %lu, %u fortnights\n", seed, fortnights);

	for (i = 0; i < fortnights + 10; i++) {
		int64_t timediff;
		uint32_t old_t, new_t, old_time = 0, new_time = 0;

		/* New should represent i/fortnights work. */
		if (i == 0)
			new_t = -1U;
		else if (i >= fortnights)
			new_t = new_target;
		else
			new_t = (uint64_t)new_target * fortnights / i;

		if (i >= fortnights)
			old_t = -1U;
		else
			old_t = (uint64_t)old_target * fortnights
				/ (fortnights - i);

		/* Simulate SHA being completely cracked on this round. */
		if (i >= sha_fail)
			old_t = -1U;

		for (r = 0; r < BLOCKS_PER_FORTNIGHT; r++) {
			uint32_t res;
			int32_t duration = 0;

			/* Solve old hash */
			while ((res = isaac_next_uint32(&isaac)) > old_t)
				duration++;

			time += duration;
			old_time += duration;
			if (noise) {
				int32_t n = add_noise(&isaac);
				if (verbose)
					printf("  Adding noise %+i\n", n);
				timestamp[r] = time + n;
			} else {
				timestamp[r] = time;
			}

			/* Solve new hash */
			duration = 0;
			while (isaac_next_uint32(&isaac) > new_t)
				duration++;

			time += duration;
			new_time += duration;
		}
		timediff = timestamp[BLOCKS_PER_FORTNIGHT-1] - timestamp[0];

		if (verbose && i == 0)
			printf("# Fortnight, time(oldhash), time(newhash)\n");
		printf("%i, %i, %i\n", i, old_time, new_time);

		/* First round, we adjust both. */
		if (i % 2 == 0 || i >= fortnights) {
			/* Update targets for both hashes. */
			old_target = (uint64_t)old_target * timediff / IDEAL_FORTNIGHT;
			new_target = (uint64_t)new_target * timediff / IDEAL_FORTNIGHT;
		} else {
			/* Second round, we blame new hash entirely. */
			int64_t old_time, new_time, desired_new_time;

			/* Assume old hash is using correct amount. */
			old_time = (uint64_t)IDEAL_FORTNIGHT * (fortnights - i)
				/ fortnights;
			/* Therefore new hash is responsible for the rest */
			new_time = timediff - old_time;

			/* This is the time we want. */
			desired_new_time = IDEAL_FORTNIGHT - old_time;

			/* Clamp to within a factor of 4 (can go
			 * negative, but cancels out). */
			if (desired_new_time > 4*new_time)
				desired_new_time = 4*new_time;
			else if (desired_new_time < new_time / 4)
				desired_new_time = new_time / 4;

			new_target = (int64_t)new_target * new_time / desired_new_time;
		}
	}
	return 0;
}
