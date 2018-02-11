#ifndef __BATTERY_H__
#define __BATTERY_H__

struct battery_data_t
{
	short load_current;
	short capacity;
	short current;
	short time;
	short voltage;
	short cell[4];
};

int getBattery(int quick, struct battery_data_t *data);

#endif
