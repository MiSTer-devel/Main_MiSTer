#ifndef VIDEOBRAIN_H
#define VIDEOBRAIN_H

#define VIDEOBRAIN_BIT_NEW_TAPE 46
#define VIDEOBRAIN_BIT_NEW_PRINTER 47

void videobrain_init();
void videobrain_new_image(int bit);
void videobrain_poll();

#endif
