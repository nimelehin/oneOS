/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <drivers/aarch32/pl050.h>
#include <drivers/generic/keyboard.h>
#include <errno.h>
#include <log.h>
#include <mem/vmm/vmm.h>
#include <mem/vmm/zoner.h>
#include <platform/aarch32/interrupts.h>
#include <utils.h>

#define DEBUG_PL050

static zone_t mapped_zone;
static volatile pl050_registers_t* registers = (pl050_registers_t*)PL050_KEYBOARD_BASE;

static inline int _pl050_map_itself()
{
    mapped_zone = zoner_new_zone(sizeof(pl050_registers_t));
    vmm_map_page(mapped_zone.start, PL050_KEYBOARD_BASE, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);
    registers = (pl050_registers_t*)mapped_zone.ptr;
    return 0;
}

static void pl050_keyboard_recieve_notification(uint32_t msg, uint32_t param)
{
    if (msg == DM_NOTIFICATION_DEVFS_READY) {
        if (msg == DM_NOTIFICATION_DEVFS_READY) {
            if (generic_keyboard_create_devfs() < 0) {
                kpanic("Can't init pl050_keyboard in /dev");
            }
        }
    }
}

static void _pl050_keyboard_int_handler()
{
    uint32_t data = registers->data;
    generic_emit_key_set1(data);
}

static inline void _keybaord_send_cmd(uint8_t cmd)
{
    registers->data = cmd;
    while ((registers->stat) & (1 << 5)) { }
    int tmp = registers->data;
    ASSERT(tmp == 0xfa);
}

void pl050_keyboard_init(device_t* dev)
{
#ifdef DEBUG_PL050
    log("PL050 KBD: Turning on");
#endif
    registers->cr = 0x4 | 0x10;

    // Turning Scan Code Set 1
    _keybaord_send_cmd(0xF0);
    _keybaord_send_cmd(0x01);

    irq_register_handler(PL050_KEYBOARD_IRQ_LINE, 0, 0, _pl050_keyboard_int_handler);
    generic_keyboard_init();
}

static driver_desc_t _pl050_keyboard_driver_info()
{
    driver_desc_t desc;
    desc.type = DRIVER_INPUT_SYSTEMS_DEVICE;
    desc.auto_start = true;
    desc.is_device_driver = false;
    desc.is_device_needed = false;
    desc.is_driver_needed = false;
    desc.functions[DRIVER_NOTIFICATION] = pl050_keyboard_recieve_notification;
    desc.functions[DRIVER_INPUT_SYSTEMS_ADD_DEVICE] = pl050_keyboard_init;
    desc.functions[DRIVER_INPUT_SYSTEMS_GET_LAST_KEY] = 0;
    desc.functions[DRIVER_INPUT_SYSTEMS_DISCARD_LAST_KEY] = 0;
    desc.pci_serve_class = 0xff;
    desc.pci_serve_subclass = 0xff;
    desc.pci_serve_vendor_id = 0x00;
    desc.pci_serve_device_id = 0x00;
    return desc;
}

void pl050_keyboard_install()
{
    if (_pl050_map_itself()) {
#ifdef DEBUG_PL050
        log_error("PL050 KBD: Can't map itself!");
#endif
        return;
    }

    driver_install(_pl050_keyboard_driver_info(), "pl050kb");
}