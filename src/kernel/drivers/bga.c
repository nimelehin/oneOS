#include <drivers/bga.h>
#include <drivers/driver_manager.h>
#include <fs/devfs/devfs.h>
#include <fs/vfs.h>
#include <log.h>
#include <x86/pci.h>
#include <utils/kassert.h>

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA 0x01CF

#define VBE_DISPI_INDEX_ID 0x0
#define VBE_DISPI_INDEX_XRES 0x1
#define VBE_DISPI_INDEX_YRES 0x2
#define VBE_DISPI_INDEX_BPP 0x3
#define VBE_DISPI_INDEX_ENABLE 0x4
#define VBE_DISPI_INDEX_BANK 0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH 0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET 0x8
#define VBE_DISPI_INDEX_Y_OFFSET 0x9

#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40

static uint16_t bga_screen_width, bga_screen_height;
static uint32_t bga_screen_line_size, bga_screen_buffer_size;

static inline void _bga_write_reg(uint16_t cmd, uint16_t data)
{
    port_16bit_out(VBE_DISPI_IOPORT_INDEX, cmd);
    port_16bit_out(VBE_DISPI_IOPORT_DATA, data);
}

static inline uint16_t _bga_read_reg(uint16_t cmd)
{
    port_16bit_out(VBE_DISPI_IOPORT_INDEX, cmd);
    return port_16bit_in(VBE_DISPI_IOPORT_DATA);
}

static void _bga_set_resolution(uint16_t width, uint16_t height)
{
    _bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    _bga_write_reg(VBE_DISPI_INDEX_XRES, width);
    _bga_write_reg(VBE_DISPI_INDEX_YRES, height);
    _bga_write_reg(VBE_DISPI_INDEX_BPP, 32);
    _bga_write_reg(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    _bga_write_reg(VBE_DISPI_INDEX_X_OFFSET, 0);
    _bga_write_reg(VBE_DISPI_INDEX_Y_OFFSET, 0);
    _bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    _bga_write_reg(VBE_DISPI_INDEX_BANK, 0);

    bga_screen_line_size = (uint32_t)width * 32;
}

static void bga_recieve_notification(uint32_t msg, uint32_t param)
{
    if (msg == DM_NOTIFICATION_DEVFS_READY) {
        dentry_t* mp;
        if (vfs_resolve_path("/dev", &mp) < 0) {
            kpanic("Can't init bga in /dev");
        }

        file_ops_t fops;
        fops.can_read = 0;
        fops.can_write = 0;
        fops.read = 0;
        fops.write = 0;
        fops.ioctl = 0;
        fops.mmap = 0;
        devfs_inode_t* res = devfs_register(mp, "bga", 3, 0, &fops);

        dentry_put(mp);
    }
}

static inline driver_desc_t _bga_driver_info()
{
    driver_desc_t bga_desc;
    bga_desc.type = DRIVER_VIDEO_DEVICE;
    bga_desc.auto_start = false;
    bga_desc.is_device_driver = true;
    bga_desc.is_device_needed = true;
    bga_desc.is_driver_needed = false;
    bga_desc.functions[DRIVER_NOTIFICATION] = bga_recieve_notification;
    bga_desc.functions[DRIVER_VIDEO_INIT] = bga_init;
    bga_desc.functions[DRIVER_VIDEO_SET_RESOLUTION] = bga_set_resolution;
    bga_desc.pci_serve_class = 0x03;
    bga_desc.pci_serve_subclass = 0x00;
    bga_desc.pci_serve_vendor_id = 0x1234;
    bga_desc.pci_serve_device_id = 0x1111;
    return bga_desc;
}

void bga_install()
{
    driver_install(_bga_driver_info());
}

void bga_init(device_t* dev)
{
    bga_set_resolution(1024, 768);
}

void bga_set_resolution(uint16_t width, uint16_t height)
{
    bga_screen_width = width;
    bga_screen_height = height;
    bga_screen_buffer_size = bga_screen_line_size * (uint32_t)height * 2;
    _bga_set_resolution(width, height);
}