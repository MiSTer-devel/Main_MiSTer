#ifndef MAC_ETH_Q8_H
#define MAC_ETH_Q8_H

#include <stdint.h>

typedef struct
{
	volatile uint8_t  *xfer;
	volatile uint64_t *ops;
	volatile uint64_t *cmd;
	volatile uint64_t *stat;
	volatile uint64_t *isr_set;
	volatile uint64_t *isr_ack;
	void (*idle)(void);
} q8_mailbox;

void q8_init(const q8_mailbox *m);

int  q8_read_words(uint32_t ga, uint16_t *w, int n, int stride);
int  q8_write_words(uint32_t ga, const uint16_t *w, int n, int stride);
int  q8_read_bytes(uint32_t ga, uint8_t *b, int n);
int  q8_write_bytes(uint32_t ga, const uint8_t *b, int n);

int  q8_flush(void);
void q8_begin(void);

void     q8_isr_reset(void);
void     q8_isr_post(uint16_t applied);
uint16_t q8_isr_qualify(uint16_t data, uint16_t seen);

#endif
