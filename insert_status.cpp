				if (hasCDROMMedia(0) || hasCDROMMedia(1) || hasCDROMMedia(2) || hasCDROMMedia(3)) {
					str[n++] = 0x98;
					static int last_auto_load_index = -1;
					static DiscType last_auto_load_type = DISC_UNKNOWN;
					for(int i=0; i<4; i++) {
						if (hasCDROMMedia(i)) {
							DiscType type = getCDROMType(i);
							if (type != DISC_UNKNOWN && (i != last_auto_load_index || type != last_auto_load_type)) {
								last_auto_load_index = i;
								last_auto_load_type = type;
								AutoLoadCore(type);
							}
						}
					}
				} else if (isCDROMPresent(0) || isCDROMPresent(1) || isCDROMPresent(2) || isCDROMPresent(3)) str[n++] = 0x97;
