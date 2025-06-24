/*
 ============================================================================
 Name        : seektime.c
 Author      : Dave Shadoff
 Version     :
 Copyright   : (C) 2018 Dave Shadoff
 Description : Program to determine seek time, based on start and end sector numbers
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct sector_group {
		int		sec_per_revolution;
		int		sec_start;
		int		sec_end;
		float	rotation_ms;
		float	rotation_vsync;
} sector_group;

#define NUM_SECTOR_GROUPS	14

sector_group sector_list[NUM_SECTOR_GROUPS] = {
	{ 10,	0,		12572,	133.47,	 8.00 },
	{ 11,	12573,	30244,	146.82,	 8.81 },		// Except for the first and last groups,
	{ 12,	30245,	49523,	160.17,	 9.61 },		// there are 1606.5 tracks in each range
	{ 13,	49524,	70408,	173.51,	10.41 },
	{ 14,	70409,	92900,	186.86,	11.21 },
	{ 15,	92901,	116998,	200.21,	12.01 },
	{ 16,	116999,	142703,	213.56,	12.81 },
	{ 17,	142704,	170014,	226.90,	13.61 },
	{ 18,	170015,	198932,	240.25,	14.42 },
	{ 19,	198933,	229456,	253.60,	15.22 },
	{ 20,	229457,	261587,	266.95,	16.02 },
	{ 21,	261588,	295324,	280.29,	16.82 },
	{ 22,	295325,	330668,	293.64,	17.62 },
	{ 23,	330669,	333012,	306.99,	18.42 }
};


static int find_group(int sector_num)
{
	int i;
	int group_index = 0;

	for (i = 0; i < NUM_SECTOR_GROUPS; i++)
	{
		if ((sector_num >= sector_list[i].sec_start) && (sector_num <= sector_list[i].sec_end))
		{
			group_index = i;
			break;
		}
	}
	return group_index;
}

float get_cd_seek_ms(int start_sector, int target_sector)
{
	int start_index;
	int target_index;

	float track_difference;
	float milliseconds = 0;

	// 360000 = number of sectors in an 80-minute CD (which weren't available)
	// 
	if ((start_sector <= 360000) && (start_sector >= 0) && (target_sector <= 360000) && (target_sector >= 0))
	{

		// First, we identify which group the start and end are in
		start_index = find_group(start_sector);
		target_index = find_group(target_sector);

		// Now we find the track difference
		//
		// Note: except for the first and last sector groups, all groups are 1606.48 tracks per group.
		//
		if (target_index == start_index)
		{
			track_difference = (float)(abs(target_sector - start_sector) / sector_list[target_index].sec_per_revolution);
		}
		else if (target_index > start_index)
		{
			track_difference = (sector_list[start_index].sec_end - start_sector) / sector_list[start_index].sec_per_revolution;
			track_difference += (target_sector - sector_list[target_index].sec_start) / sector_list[target_index].sec_per_revolution;
			track_difference += (1606.48 * (target_index - start_index - 1));
		}
		else // start_index > target_index
		{
			track_difference = (start_sector - sector_list[start_index].sec_start) / sector_list[start_index].sec_per_revolution;
			track_difference += (sector_list[target_index].sec_end - target_sector) / sector_list[target_index].sec_per_revolution;
			track_difference += (1606.48 * (start_index - target_index - 1));
		}

		// Now, we use the algorithm to determine how long to wait
		if (abs(target_sector - start_sector) <= 3)
		{
			milliseconds = (2 * 1000 / 60);
		}
		else if (abs(target_sector - start_sector) < 7)
		{
			milliseconds = (9 * 1000 / 60) + (float)(sector_list[target_index].rotation_ms * 0.75);
		}
		else if (track_difference <= 80)
		{
			milliseconds = (17 * 1000 / 60) + (float)(sector_list[target_index].rotation_ms * 0.75);
		}
		else if (track_difference <= 160)
		{
			milliseconds = (22 * 1000 / 60) + (float)(sector_list[target_index].rotation_ms * 0.75);
		}
		else if (track_difference <= 644)
		{
			milliseconds = (22 * 1000 / 60) + (float)(sector_list[target_index].rotation_ms * 0.75) + (float)((track_difference - 161) * 16.66 / 80);
		}
		else
		{
			milliseconds = (36 * 1000 / 60) + (float)(sector_list[target_index].rotation_ms * 0.5) + (float)((track_difference - 644) * 16.66 / 195);
		}
	}
	else
		milliseconds = 0;

	printf("From sector %d to sector %d:\n", start_sector, target_sector);
	printf("Time = %.2f milliseconds\n", milliseconds);

	return milliseconds;
}
