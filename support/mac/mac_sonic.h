#ifndef MAC_SONIC_H
#define MAC_SONIC_H

#include <stdint.h>

typedef struct
{
	int (*read_words)(uint32_t gaddr, uint16_t *w, int n, int stride);
	int (*write_words)(uint32_t gaddr, const uint16_t *w, int n, int stride);
	int (*read_bytes)(uint32_t gaddr, uint8_t *b, int n);
	int (*write_bytes)(uint32_t gaddr, const uint8_t *b, int n);
	int (*wire_send)(const uint8_t *frame, int len);
} sonic_host_ops;

void     sonic_init(const sonic_host_ops *ops);
void     sonic_reset(void);
void     sonic_reg_write(int reg, uint16_t data);
uint16_t sonic_reg(int reg);
void     sonic_fill_shadows(uint16_t regs[64]);
int      sonic_int_line(void);
void     sonic_set_isr_local(int on);
uint16_t sonic_take_raised(void);
int      sonic_rx_frame(const uint8_t *frame, int len);
void     sonic_tx_continue(void);
void     sonic_time_tick(unsigned us);
void     sonic_set_addr_bits(int bits);

#endif
