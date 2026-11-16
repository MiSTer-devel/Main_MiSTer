#ifndef IIGS_UTHERNET2_H
#define IIGS_UTHERNET2_H

// Apple IIgs Uthernet II card: call every pass of the user_io poll loop.
// Does nothing unless the Apple-IIgs core is loaded with the card enabled.
char is_iigs();
void iigs_uthernet2_poll(void);

#endif
