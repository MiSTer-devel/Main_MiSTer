#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define N64_MAX_DIST 97.5807358037f

void n64_joy_emu(int x, int y, int* x2, int* y2, int max_cardinal, float max_range)
{
	// Move to top right quadrant to standardize solutions
	const int x_flip = x < 0 ? -1 : 1;
	const int y_flip = y < 0 ? -1 : 1;
	const int abs_x = x * x_flip;
	const int abs_y = y * y_flip;

	// Either reduce range to radius 97.5807358037f ((69,69) diagonal of original controller)
	// or reduce cardinals to 85, whichever is less aggressive (smaller reduction in scaling)
	// (subtracts 2 from each to allow for minor outer deadzone)
	// assumes the max range is at least 85 (max cardinal of original controller)
	if (max_cardinal < 85) max_cardinal = 85;
	if (max_range < N64_MAX_DIST) max_range = N64_MAX_DIST;
	float scale_cardinal = 85.0f / (max_cardinal - 2);
	float scale_range = N64_MAX_DIST / (max_range - 2);
	float scale = scale_cardinal > scale_range ? scale_cardinal : scale_range;
	float scaled_x = abs_x * scale;
	float scaled_y = abs_y * scale;

	// Move to octagon's lower wedge in top right quadrant to further standardize solution
	float scaled_max;
	float scaled_min;
	if (abs_x > abs_y) {
		scaled_max = scaled_x;
		scaled_min = scaled_y;
	} else {
		scaled_max = scaled_y;
		scaled_min = scaled_x;
	}

	// Clamp scaled_min and scaled_max
	// Note: wedge boundary is given by x = 85 - y * ((85 - 69) / 69)
	// If x + y * (16 / 69) > 85, coordinates exceed boundary and need clamped
	float boundary = scaled_max + scaled_min * 0.231884057971f;
	if (boundary > 85) {
		// We know target value is on:
		//   1) Boundary line: x = 85 - y * (16 / 69)
		//   2) Observed slope line: y = (scaled_max / scaled_min) * x
		// Solving system of equations yields:
		scaled_min = 85 * scaled_min / boundary;
		scaled_max = 85 - scaled_min * 0.231884057971f; // Boundary line
	}

	// Move back from wedge to actual coordinates
	if (abs_x > abs_y) {
		*x2 = x_flip * scaled_max;
		*y2 = y_flip * scaled_min;
	} else {
		*x2 = x_flip * scaled_min;
		*y2 = y_flip * scaled_max;
	}
}

#undef N64_MAX_DIST