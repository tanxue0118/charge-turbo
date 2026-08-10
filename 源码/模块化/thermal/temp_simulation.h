#ifndef TURBO_CHARGE_TEMP_SIMULATION_H
#define TURBO_CHARGE_TEMP_SIMULATION_H

#include "global.h"

int read_temp_mc(const char *path, int *out);
int cleanup_battery_temp_simulation(TempSimState *st);
int apply_battery_temp_simulation(TempSimState *st, int is_charging);
int current_simulated_temp_mc(void);
void handle_option_generation_change(unsigned long *last_generation,
                                     TempSimState *temp_sim_state,
                                     int is_charging);

#endif
