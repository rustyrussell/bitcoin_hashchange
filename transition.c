/* Simulate the effect of miners during transition. */
#include <ccan/isaac/isaac.h>
#include <ccan/time/time.h>
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

int main(int argc, char *argv[])
{
	struct isaac_ctx isaac;
	struct timespec ts = time_now();
	unsigned int old_target = 8000000, new_target = 0xFFFFFFFF;
	unsigned long seed;
	unsigned int i, rounds = 2016, time;
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
	opt_register_noarg("-v|--verbose", opt_set_bool,
			 &verbose, "Verbose output");
	opt_register_noarg("-h|--help", opt_usage_and_exit, "",
			   "This usage message");

	opt_parse(&argc, argv, opt_log_stderr_exit);
	if (argc != 1)
		opt_usage_and_exit(NULL);

	isaac_init(&isaac, (unsigned char *)&ts, sizeof(ts));
	timestamp = malloc(sizeof(*timestamp) * rounds);

	if (verbose)
		printf("  Seed is %lu, %u rounds\n", seed, rounds);

	for (i = 0; i < rounds; i++) {
		uint32_t res, duration = 0;

		/* Solve old hash */
		while ((res = isaac_next_uint32(&isaac)) > old_target)
			duration++;

		if (verbose)
			printf("  Old hash finished in %u\n", duration);

		if (noise) {
			int32_t n = add_noise(&isaac);
			if (verbose)
				printf("  Adding noise %+i\n", n);
			duration += n;
		}

		time += duration;
		timestamp[i] = time;

		if ((res & 0xF) != 0) {
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
	return 0;
}
	
