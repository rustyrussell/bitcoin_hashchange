/* Simulate the effect of miners during transition. */
#include <ccan/isaac/isaac.h>
#include <ccan/time/time.h>
#include <stdio.h>
#include <stdlib.h>

/* This gives approximately normal distribution noise. */
static int32_t add_noise(struct isaac_ctx *isaac, uint32_t val)
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
	return ret + val;
}

int main(int argc, char *argv[])
{
	struct isaac_ctx isaac;
	struct timespec ts = time_now();
	uint32_t old_target = 8000000, new_target = 0xFFFFFFFF;
	unsigned int i, rounds = 2016, time = 0, *timestamp;
	bool noise = true;

	isaac_init(&isaac, (unsigned char *)&ts, sizeof(ts));
	timestamp = malloc(sizeof(*timestamp) * rounds);

	for (i = 0; i < rounds; i++) {
		int32_t diff;
		uint32_t res;

		/* Solve old hash */
		while ((res = isaac_next_uint32(&isaac)) > old_target)
			time++;
		timestamp[i] = time;

		if ((res & 0xF) != 0) {
			/* Hard block.  Solve new hash */
			while (isaac_next_uint32(&isaac) > new_target)
				time++;
		}

		if (i != 0) {
			diff = timestamp[i] - timestamp[i-1];
			if (noise)
				diff = add_noise(&isaac, diff);
			printf("%i\n", diff);
		}
	}
	return 0;
}
	
