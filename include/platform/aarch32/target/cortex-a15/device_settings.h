#ifndef __oneOS__CORTEXA15_DEVSETTINGS_H
#define __oneOS__CORTEXA15_DEVSETTINGS_H

/**
 * Used devices:
 *      gicv2
 *      sp804
 *      pl181
 *      pl111
 *      pl050
 */

/* Base is read from CBAR */
#define GICv2_DISTRIBUTOR_OFFSET 0x1000
#define GICv2_CPU_INTERFACE_OFFSET 0x2000

#define SP804_BASE 0x1c110000

#define PL181_BASE 0x1c050000

#define PL111_BASE 0x1c1f0000

#define PL050_KEYBOARD_BASE 0x1c060000

#define PL050_MOUSE_BASE 0x1c070000

/**
 * Interrupt lines:
 *      SP804 TIMER1: 2nd line in SPI (32+2)
 */

#define SP804_TIMER1_IRQ_LINE (32 + 2)

#define PL050_KEYBOARD_IRQ_LINE (32 + 12)
#define PL050_MOUSE_IRQ_LINE (32 + 13)

#endif /* __oneOS__CORTEXA15_DEVSETTINGS_H */