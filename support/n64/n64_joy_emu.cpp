#include <stdio.h>
#include <stdint.h>
#include <math.h>

void n64_joy_emu(int x, int y, int* x2, int* y2, int max_range)
{
	// Move to top right quadrant to standardize solutions
	const int x_flip = x < 0 ? -1 : 1;
	const int y_flip = y < 0 ? -1 : 1;
	const int abs_x = x * x_flip;
	const int abs_y = y * y_flip;

	// Reduce range to radius 97.5807358037f ((69,69) diagonal of original controller)
	// assumes the max range is at least 85 (max cardinal of original controller)
	if (max_range < 85) max_range = 85;
	float scale = 97.5807358037f / max_range;
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
