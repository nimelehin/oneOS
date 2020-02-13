#ifndef __oneOS__INTERRUPTS__IRQHANDLER_H
#define __oneOS__INTERRUPTS__IRQHANDLER_H

#include <x86/idt.h>
#include <drivers/display.h>

void irq_handler(trapframe_t *tf);
void irq_redirect(uint8_t int_no);
void irq_empty_handler();

#endif