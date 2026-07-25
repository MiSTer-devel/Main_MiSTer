/*
 * MAC address translation — ported from Amiberry a2065.cpp
 *
 * Translates between:
 *   fakemac: 00:80:10:XX:XX:XX  (Commodore OUI, presented to Amiga driver)
 *   realmac: host NIC MAC with OUI forced to 00:80:10
 *
 * Applied to every packet in both directions (TX and RX).
 * Also fixes ARP sender/target and DHCP CHADDR fields.
 * Recalculates UDP checksum if DHCP CHADDR was modified.
 */

#include "minimig_a2065_types.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static uint8_t fakemac[6];
static uint8_t realmac[6];

void mac_set_addresses(const uint8_t *fake, const uint8_t *real)
{
    memcpy(fakemac, fake, 6);
    memcpy(realmac, real, 6);
}

/* Update only the fakemac (the Amiga-side address munge matches against),
 * leaving realmac untouched. Called from chip_init once the LANCE init block
 * reveals the station address the Amiga actually programmed (from autoconfig
 * er_SerialNumber). Without this the startup fakemac never matches the real
 * Amiga frames and the fakemac<->realmac swap is a no-op. */
void mac_set_fakemac(const uint8_t *fake) { memcpy(fakemac, fake, 6); }

void mac_get_fake(uint8_t *out) { memcpy(out, fakemac, 6); }
void mac_get_real(uint8_t *out) { memcpy(out, realmac, 6); }

/* Swap MAC at packet[0..5] if it matches either known MAC. */
static int dofakemac(uint8_t *packet)
{
    if (!memcmp(fakemac, realmac, 6))
        return 1;
    if (!memcmp(packet, fakemac, 6)) {
        memcpy(packet, realmac, 6);
        return 1;
    }
    if (!memcmp(packet, realmac, 6)) {
        memcpy(packet, fakemac, 6);
        return 1;
    }
    return 0;
}

/*
 * Translate MACs in a complete Ethernet frame.
 * Handles: dst MAC, src MAC, ARP sender/target, DHCP CHADDR.
 * Returns frame length (unchanged), or 0 if packet too short.
 */
int mungepacket(uint8_t *packet, int len)
{
    if (len < 20)
        return 0;
    if (!memcmp(fakemac, realmac, 6))
        return len;

    uint8_t *data = packet + 14;
    uint16_t type = ((uint16_t)packet[12] << 8) | packet[13];
    int ret = 0;

    /* dst and src MACs */
    ret |= dofakemac(packet);
    ret |= dofakemac(packet + 6);

    if (type == 0x0806) {
        /* ARP: hardware type must be Ethernet (1) and hw addr len = 6 */
        if (((data[0] << 8) | data[1]) == 1 && data[4] == 6) {
            ret |= dofakemac(data + 8);          /* sender hardware addr */
            ret |= dofakemac(data + 8 + 6 + 4); /* target hardware addr */
        }
    } else if (type == 0x0800) {
        /* IPv4 */
        int proto = data[9];
        int ihl   = data[0] & 0x0f;
        uint8_t *ipv4 = data;

        data += ihl * 4;

        if (proto == 17) {
            /* UDP */
            int sp   = ((int)data[0] << 8) | data[1];
            int dp   = ((int)data[2] << 8) | data[3];
            int len2 = ((int)data[4] << 8) | data[5];
            int udpcrc = 0;

            /* DHCP ports 67/68: fix CHADDR field at UDP payload + 36 */
            if (sp == 67 || sp == 68 || dp == 67 || dp == 68)
                udpcrc |= dofakemac(data + 8 + 28);

            if (udpcrc && (data[6] || data[7])) {
                /* Recalculate UDP checksum */
                uint32_t sum = 0;
                int i;
                data[6] = data[7] = 0;
                data[len2] = 0; /* pad to even if needed */
                for (i = 0; i < ((len2 + 1) & ~1); i += 2)
                    sum += ((uint32_t)data[i] << 8) | data[i + 1];
                /* IPv4 pseudo-header: src IP, dst IP, proto, UDP len */
                sum += ((uint32_t)ipv4[12] << 8) | ipv4[13];
                sum += ((uint32_t)ipv4[14] << 8) | ipv4[15];
                sum += ((uint32_t)ipv4[16] << 8) | ipv4[17];
                sum += ((uint32_t)ipv4[18] << 8) | ipv4[19];
                sum += 17;
                sum += len2;
                while (sum >> 16)
                    sum = (sum & 0xFFFF) + (sum >> 16);
                sum = ~sum;
                if (sum == 0) sum = 0xffff;
                data[6] = (uint8_t)(sum >> 8);
                data[7] = (uint8_t)(sum);
                ret |= 1;
            }
        }
    }

    return ret ? len : len;
}
