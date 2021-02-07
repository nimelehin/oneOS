/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#ifndef __oneOS__DRIVERS__AARCH32__PL111_H
#define __oneOS__DRIVERS__AARCH32__PL111_H

#include <drivers/driver_manager.h>
#include <platform/aarch32/target/cortex-a15/device_settings.h>
#include <types.h>
#include <utils/mask.h>

enum PL111RegisterMasks {
    MASKDEFINE(PIXELS_PER_LINE, 2, 6),
    MASKDEFINE(LINES_PER_PANEL, 0, 10),

    MASKDEFINE(LCD_POWER, 11, 1),
    MASKDEFINE(LCD_ENDIAN, 9, 2),
    MASKDEFINE(LCD_BGR, 8, 1),
    MASKDEFINE(LCD_DUAL, 7, 1),
    MASKDEFINE(LCD_TFT, 5, 1),
    MASKDEFINE(LCD_BW, 4, 1),
    MASKDEFINE(LCD_BPP, 1, 3),
    MASKDEFINE(LCD_EN, 0, 1),
};

enum PL111Consts {
    NUM_TIMINGS = 4,
    NUM_PALETTE_WORDS = 0x378,
    LCD_16_BPP = 4, // Register constant for 16 bits per pixel
    LCD_24_BPP = 5, // Register constant for 24 bits per pixel
};

struct pl111_registers {
    uint32_t lcd_timing_0;
    uint32_t lcd_timing_1;
    uint32_t lcd_timing_2;
    uint32_t lcd_timing_3;
    uint32_t lcd_upbase;
    uint32_t lcd_lpbase;
    uint32_t lcd_control;
    uint32_t lcd_imsc;
    uint32_t lcd_ris;
    uint32_t lcd_mis;
    uint32_t lcd_icr;
    uint32_t lcd_upcurr;
    uint32_t lcd_lpcurr;
    // TO BE CONTINUED
};
typedef struct pl111_registers pl111_registers_t;

void pl111_install();

#endif //__oneOS__DRIVERS__AARCH32__PL111_H
