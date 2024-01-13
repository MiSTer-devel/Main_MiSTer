#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "../../user_io.h"

static constexpr int MAX_DIAG = 69;
static constexpr int MAX_CARDINAL = 85;
static constexpr float OUTER_DEADZONE = 2.0f;
static constexpr float WEDGE_BOUNDARY = (float)(MAX_CARDINAL - MAX_DIAG) / MAX_DIAG;
static constexpr float MAX_DIST = hypotf(MAX_DIAG, MAX_DIAG);

void stick_swap(int num, int stick, int* num2, int* stick2)
{
	int get = user_io_status_get("TV", 1);
	int p2 = get & 1;
	int p3 = get & 2;
	int swap = get & 4;

	//reverse sticks
	if (swap) {
		stick = stick ? 0 : 1;
	}

	//p1 right stick -> p3
	if (p3) {
		if (stick && (num < 2)) {
			num += 2;
			stick = 0;
		}
		//swap sticks to minimize conflict
		else if (!stick && (2 < num) && (num < 5)) {
			num -= 2;
			stick = 1;
		}
	}

	//p1 right stick -> p2
	if (p2) {
		if (stick && ((num == 0) || (num == 2))) {
			num++;
			stick = 0;
		}
		else if (!stick && (num % 2 == 1)) {
			num--;
			stick = 1;
		}
	}

	*num2 = num;
	*stick2 = stick;
}

void n64_joy_emu(const int x, const int y, int* x2, int* y2, int max_cardinal, float max_range) {
	// Move to top right quadrant to standardize solutions
	const int x_flip = x < 0 ? -1 : 1;
	const int y_flip = y < 0 ? -1 : 1;
	const float abs_x = x * x_flip;
	const float abs_y = y * y_flip;

	// Either reduce range to radius 97.5807358037f ((69, 69) diagonal of original controller)
	// or reduce cardinals to 85, whichever is less aggressive (smaller reduction in scaling)
	// (subtracts 2 from each to allow for minor outer deadzone)
	// assumes the max range is at least 85 (max cardinal of original controller)
	if (max_cardinal < MAX_CARDINAL) max_cardinal = MAX_CARDINAL;
	if (max_range < MAX_DIST) max_range = MAX_DIST;

	const float scale_cardinal = MAX_CARDINAL / (max_cardinal - OUTER_DEADZONE);
	const float scale_range = MAX_DIST / (max_range - OUTER_DEADZONE);
	const float scale = scale_cardinal > scale_range ? scale_cardinal : scale_range;
	const float scaled_x = abs_x * scale;
	const float scaled_y = abs_y * scale;

	// Move to octagon's lower wedge in top right quadrant to further standardize solution
	float scaled_max;
	float scaled_min;

	if (abs_x > abs_y) {
		scaled_max = scaled_x;
		scaled_min = scaled_y;
	}
	else {
		scaled_max = scaled_y;
		scaled_min = scaled_x;
	}

	// Clamp scaled_min and scaled_max
	// Note: wedge boundary is given by x = 85 - y * ((85 - 69) / 69)
	// If x + y * (16 / 69) > 85, coordinates exceed boundary and need clamped
	const float boundary = scaled_max + scaled_min * WEDGE_BOUNDARY;
	if (boundary > MAX_CARDINAL) {
		// We know target value is on:
		//   1) Boundary line: x = 85 - y * (16 / 69)
		//   2) Observed slope line: y = (scaled_max / scaled_min) * x
		// Solving system of equations yields:
		scaled_min = MAX_CARDINAL * scaled_min / boundary;
		scaled_max = MAX_CARDINAL - scaled_min * WEDGE_BOUNDARY; // Boundary line
	}

	// Move back from wedge to actual coordinates
	if (abs_x > abs_y) {
		*x2 = nearbyintf(scaled_max * x_flip);
		*y2 = nearbyintf(scaled_min * y_flip);
	}
	else {
		*x2 = nearbyintf(scaled_min * x_flip);
		*y2 = nearbyintf(scaled_max * y_flip);
	}
}
