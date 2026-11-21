#ifndef FOLDER_STEP_H
#define FOLDER_STEP_H

#include <stdint.h>
#include <string>
#include <vector>

// "Folder as playlist": a core asks (UIO_GET_FILEREQ) for the next, previous,
// random or first file in the folder of a file it has loaded, and Main reports
// the position back (UIO_SET_FILEPOS). This file holds the logic that needs no
// hardware; menu.cpp and user_io.cpp do the loading and the SPI transfers.

// Request codes in the low nibble of the data word of UIO_GET_FILEREQ.
#define FOLDER_NONE   0
#define FOLDER_NEXT   1
#define FOLDER_PREV   2
#define FOLDER_RANDOM 3
#define FOLDER_FIRST  4

// Index of the file to load for a request, or -1 (empty list, unknown code).
// current is the index of the loaded file in list, or -1 if it is not in it
// (then every request gives the first file, random any file). NEXT and PREV
// wrap around. RANDOM never returns current if the list has more than one file.
int folder_step(const std::vector<std::string> &list, int current, int code, unsigned rnd);

// Index of name in list (exact match first, then ignoring case), or -1.
int folder_find(const std::vector<std::string> &list, const char *name);

// Handles the reply to the UIO_GET_FILEREQ command word:
//   reply = {slot[7:0], 4'hA, counter[3:0]}
// slot is the file index (ioctl index) the core uses as folder playlist, 0 means
// "the file loaded last". It is taken from every reply. Only if the counter is
// new, the data word (request code in [3:0]) is read with next_word() and the
// counter is taken over afterwards, so an aborted transfer is retried. The same
// counter twice gives one request. Returns 1 if the core answered at all.
int folder_poll(uint16_t reply, uint16_t (*next_word)(void));

// One pending request, kept until the OSD is closed. A newer one replaces it.
int folder_request_take();
int folder_slot();
int folder_core_active();

// Forget everything (new core).
void folder_reset();

#endif
