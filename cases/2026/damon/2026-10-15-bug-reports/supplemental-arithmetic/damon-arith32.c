#include <stdio.h>
#include <limits.h>

#define mult_frac(x, n, d) ({ \
	typeof(x) x_ = (x); \
	typeof(n) n_ = (n); \
	typeof(d) d_ = (d); \
	typeof(x_) q = x_ / d_; \
	typeof(x_) r = x_ % d_; \
	q * n_ + r * n_ / d_; \
})

static unsigned long feed(unsigned long last_input, unsigned long score)
{
	const unsigned long goal = 10000, min_input = 10000;
	unsigned long diff, compensation;
	if (score == goal)
		return last_input;
	if (score >= goal * 2)
		return min_input;
	diff = score > goal ? score - goal : goal - score;
	compensation = mult_frac(last_input, diff, goal);
	if (score > goal)
		return last_input - compensation > min_input ?
			last_input - compensation : min_input;
	if (last_input < ULONG_MAX - compensation)
		return last_input + compensation;
	return ULONG_MAX;
}

int main(void)
{
	unsigned long region_size = 256UL * 1024, max_events = 0;
	unsigned long access_events = 0, goal_bp = 400;
	unsigned long target, score, adaptation, new_sample;
	unsigned long access_bytes = 64UL * 1024 * 1024;
	unsigned long bw = access_bytes * 1000L * 1000L / 100000UL;
	unsigned long max_percentile = 0, final_percentile = 0;
	long percentiles[101];
	int next_percentile = 0;
	for (int i = 0; i <= 100; i++)
		percentiles[i] = -999;
	for (int i = 0; i < 4; i++) {
		max_events += region_size * 20UL;
		access_events += region_size * 1U;
	}
	target = max_events * goal_bp / 10000;
	target = target ? target : 1;
	score = mult_frac(access_events, 10000, target);
	adaptation = feed(100000000UL, score) / 10000;
	if (adaptation <= 10000)
		adaptation = 5000 + adaptation / 2;
	new_sample = 5000UL * adaptation / 10000;
	for (unsigned long i = 1; i <= 16; i++) {
		unsigned long accounted_bytes = i * 4UL * 1024 * 1024;
		unsigned long percentile = accounted_bytes * 100 / access_bytes;
		if (percentile > max_percentile)
			max_percentile = percentile;
		if (i == 16)
			final_percentile = percentile;
		while (next_percentile <= percentile)
			percentiles[next_percentile++] = (long)i;
	}
	printf("long_bits=%zu max_events=%lu access_events=%lu target=%lu score=%lu adaptation=%lu new_sample=%lu\n",
		sizeof(unsigned long) * CHAR_BIT, max_events, access_events,
		target, score, adaptation, new_sample);
	printf("bandwidth=%lu percentile_max=%lu percentile_final=%lu next_percentile=%d index63=%ld index100=%ld\n",
		bw, max_percentile, final_percentile, next_percentile,
		percentiles[63], percentiles[100]);
	printf("correct_target=%llu correct_bandwidth=%llu\n",
		(unsigned long long)max_events * goal_bp / 10000,
		(unsigned long long)access_bytes * 1000000 / 100000);
	return 0;
}
