/* Simulate the effect of miners during transition. */
#include <ccan/isaac/isaac.h>
#include <ccan/time/time.h>
#include <ccan/str/str.h>
#include <ccan/opt/opt.h>
#include <stdio.h>
#include <stdlib.h>

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

enum bias {
	NONE,
	NEW_HASH,
	OLD_HASH
};

static char *opt_set_bias(const char *arg, enum bias *bias)
{
	if (streq(arg, "new"))
		*bias = NEW_HASH;
	else if (streq(arg, "old"))
		*bias = OLD_HASH;
	else
		return "Unknown value";
	return NULL;
}


/* Other nodes won't accept time going backwards compared to last 11 */
static int32_t min_timestamp(const int32_t *timestamp, int round)
{
	int64_t total = 0;
	int i;

	if (round == 0)
		return 1;

	/* Average last 11 timestamps. */
	for (i = round - 1; i > round - 12; i--) {
		if (i >= 0)
			total += timestamp[i];
		else
			total += timestamp[0];
	}

	return total / 11 + 1;
}

/* Other nodes won't accept time more than 2 hours in future. */
static int32_t max_timestamp(uint32_t time)
{
	return time + 2 * 60 * 60;
}

static int32_t doctor_timestamp(const int32_t *timestamp, int round,
				bool *hardblock, int32_t time, enum bias bias)
{
	/* Don't doctor in round 0. */
	if (round == 0)
		return time;

	if (bias == NEW_HASH) {
		/* Make hard blocks as fast as possible, easy blocks
		 * as slow as possible. */
		if (hardblock[round-1])
			return min_timestamp(timestamp, round);
		else
			return max_timestamp(time);
	} else {
		/* Make hard blocks as slow as possible, easy blocks
		 * as fast as possible. */
		if (hardblock[round-1])
			return max_timestamp(time);
		else
			return min_timestamp(timestamp, round);
	}
}

int main(int argc, char *argv[])
{
	struct isaac_ctx isaac;
	struct timespec ts = time_now();
	unsigned int old_target = 8000000, new_target = 0xFFFFFFFF;
	unsigned long seed;
	unsigned int i, rounds = 2016, time, bias_percent = 50;
	int32_t *timestamp;
	bool noise = false, verbose = false, *hardblock;
	enum bias bias = NONE;

	seed = (ts.tv_sec << 10) + ts.tv_nsec;

	opt_register_arg("--seed", opt_set_ulongval, NULL, &seed,
			 "Seed value for rng (default based on time)");
	opt_register_arg("--old-target", opt_set_uintval, opt_show_uintval,
			 &old_target, "Target difficulty for old hash");
	opt_register_arg("--new-target", opt_set_uintval, opt_show_uintval,
			 &new_target, "Target difficulty for new hash");
	opt_register_noarg("--noise", opt_set_bool,
			 &noise, "Add noise to timestamps");
	opt_register_arg("--bias", opt_set_bias, NULL, &bias,
			   "Doctor timestamps to promote 'old' or 'new' hash");
	opt_register_arg("--bias-percent", opt_set_uintval, opt_show_uintval,
			 &bias_percent, "Percentage of blocks being biassed");
	opt_register_noarg("-v|--verbose", opt_set_bool,
			 &verbose, "Verbose output");
	opt_register_noarg("-h|--help", opt_usage_and_exit, "",
			   "This usage message");

	opt_parse(&argc, argv, opt_log_stderr_exit);
	if (argc != 1)
		opt_usage_and_exit(NULL);

	isaac_init(&isaac, (unsigned char *)&ts, sizeof(ts));
	timestamp = malloc(sizeof(*timestamp) * rounds);
	hardblock = malloc(sizeof(*hardblock) * rounds);

	if (verbose)
		printf("  Seed is %lu, %u rounds\n", seed, rounds);

	for (i = 0; i < rounds; i++) {
		uint32_t res;
		int32_t duration = 0;

		/* Solve old hash */
		while ((res = isaac_next_uint32(&isaac)) > old_target)
			duration++;

		if (verbose)
			printf("  Old hash finished in %u\n", duration);

		/* Bottom 4 bits indicate if this is a hard block. */
		hardblock[i] = ((res & 0xF) != 0);

		time += duration;

		if (bias != NONE
		    && isaac_next_uint(&isaac, 100) < bias_percent) {
			timestamp[i] = doctor_timestamp(timestamp, i,
							hardblock, time, bias);
		} else if (noise) {
			int32_t n = add_noise(&isaac);
			if (verbose)
				printf("  Adding noise %+i\n", n);
			timestamp[i] = time + n;
		} else {
			timestamp[i] = time;
		}

		if (hardblock[i]) {
			/* Hard block.  Solve new hash */
			duration = 0;
			while (isaac_next_uint32(&isaac) > new_target)
				duration++;
			if (verbose)
				printf("  New hash finished in %u\n", duration);
			time += duration;
		}

		if (i != 0)
			printf("%i\n", timestamp[i] - timestamp[i-1]);
	}

	if (verbose) {
		int64_t hard_tot = 0, easy_tot = 0;
		unsigned num_hard = 0, num_easy = 0;

		for (i = 0; i+1 < rounds; i++) {
			if (hardblock[i]) {
				hard_tot += timestamp[i+1] - timestamp[i];
				num_hard++;
			} else {
				easy_tot += timestamp[i+1] - timestamp[i];
				num_easy++;
			}
		}
		printf("%u easy, average duration %lli\n", num_easy,
		       easy_tot / num_easy);
		printf("%u hard, average duration %lli\n", num_hard,
		       hard_tot / num_hard);
	}
	return 0;
}
