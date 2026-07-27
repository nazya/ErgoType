#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "filter-maccel.h"
#include "filter-synchronous.h"

static void
assert_close(float actual, float expected)
{
	assert(fabsf(actual - expected) <= 0.005f);
}

static void
test_maccel(void)
{
	struct maccel accel;
	struct coords_residue residue = { 0, 0 };

	maccel_init(&accel, 1000);
	accel.last_time_ms = 100;
	assert_close(maccel_filter(&accel, &residue, 1, 0, 101).x,
		     213.0f / 1024.0f);
	assert_close(maccel_filter(&accel, &residue, 3, 0, 102).x,
		     1105.0f / 1024.0f);
	assert_close(maccel_filter(&accel, &residue, 10, 0, 103).x,
		     9074.0f / 1024.0f);

	residue = (struct coords_residue){ 100, -100 };
	maccel_filter(&accel, &residue, -1, 1, 104);
	assert(residue.x == 0.0f);
	assert(residue.y == 0.0f);

	residue = (struct coords_residue){ 100, 100 };
	maccel_filter(&accel, &residue, 1, 1, 305);
	assert(residue.x == 0.0f);
	assert(residue.y == 0.0f);

	maccel_init(&accel, 1000);
	accel.last_time_ms = 1;
	assert_close(maccel_filter(&accel, &residue, 1, 0, 1).x,
		     213.0f / 1024.0f);
}

static void
test_synchronous(void)
{
	struct synchronous_accel accel;

	synchronous_accel_init(&accel, 1000, 10, 3, 2, 1, 2);
	assert_close(synchronous_accel_filter(&accel, 5, 0, 1).x, 5.0f);
	assert_close(synchronous_accel_filter(&accel, 1, 0, 2).x,
		     682.0f / 1024.0f);
	assert_close(synchronous_accel_filter(&accel, 25, 0, 3).x,
		     38388.0f / 1024.0f);

	synchronous_accel_init(&accel, 1000, 5, 2, 1, 0, 1);
	assert_close(synchronous_accel_filter(&accel, 1, 0, 1).x, 0.5f);
	assert_close(synchronous_accel_filter(&accel, 20, 0, 2).x, 40.0f);

	synchronous_accel_init(&accel, 1000, 5, 2, 1, 0, 1);
	synchronous_accel_filter(&accel, 1, 0, 1);
	assert_close(synchronous_accel_filter(&accel, 10, 0, 3).x, 10.0f);

	synchronous_accel_init(&accel, 1000, 5, 2, 1, 0, 1);
	accel.last_time_ms = 1;
	assert_close(synchronous_accel_filter(&accel, 5, 0, 1).x, 5.0f);
}

int
main(void)
{
	test_maccel();
	test_synchronous();
	return 0;
}
