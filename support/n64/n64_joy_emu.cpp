#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "../../user_io.h"

#define N64_MAX_DIAG 69
#define N64_MAX_DIST sqrt(N64_MAX_DIAG * N64_MAX_DIAG * 2)
#define N64_MAX_CARDINAL 85
#define OUTER_DEADZONE 2.0f
#define WEDGE_BOUNDARY (N64_MAX_CARDINAL - 69.0f) / 69.0f



void stick_swap(int num, int stick, int *num2, int *stick2)
{
	int get=user_io_status_get("TV",1);
	int p2=get%2;
	int p3=get&2;
	int swap=get&4;
	if(swap) //reverse sticks
	{
		stick=1-stick;
	}
	if(p3) //p1 right stick -> p3
	{
		if (stick && num<2) 
		{
			num+=2;
			stick=0;
		}
		else if(!stick && 2<num && num<5) //swap sticks to minimize conflict
		{
			num-=2;
			stick=1;
		}
	}
	if(p2) //p1 right stick -> p2
	{
		if (stick && ( (num==0) | (num==2))) 
		{
			num++;
			stick=0;
		}
		else if(!stick && num%2==1)
		{
			num--;
			stick=1;
		}
	}
	*num2=num;
	*stick2=stick;
}

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
	if (max_cardinal < N64_MAX_CARDINAL) max_cardinal = N64_MAX_CARDINAL;
	if (max_range < N64_MAX_DIST) max_range = N64_MAX_DIST;
	float scale_cardinal = N64_MAX_CARDINAL / (max_cardinal - OUTER_DEADZONE);
	float scale_range = N64_MAX_DIST / (max_range - OUTER_DEADZONE);
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
	float boundary = scaled_max + scaled_min * WEDGE_BOUNDARY;
	if (boundary > N64_MAX_CARDINAL) {
		// We know target value is on:
		//   1) Boundary line: x = 85 - y * (16 / 69)
		//   2) Observed slope line: y = (scaled_max / scaled_min) * x
		// Solving system of equations yields:
		scaled_min = N64_MAX_CARDINAL * scaled_min / boundary;
		scaled_max = N64_MAX_CARDINAL - scaled_min * WEDGE_BOUNDARY; // Boundary line
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

#undef N64_MAX_DIAG
#undef N64_MAX_DIST
#undef N64_MAX_CARDINAL
#undef OUTER_DEADZONE
#undef WEDGE_BOUNDARY
