// st_ipf.h - IPF images for the Atari ST core, through a temporary SCP
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef ST_IPF_H
#define ST_IPF_H

#include <stddef.h>

bool st_ipf_to_scp(const char *ipf_path, const char *scp_path, char *err, size_t errlen);

#endif
