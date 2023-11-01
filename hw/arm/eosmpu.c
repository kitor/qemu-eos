/*
 * Canon EOS ARM MPU emulation
 * Copyrigh 2023 Kajetan Krykwiński / Magic Lantern project
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/arm/boot.h"
#include "hw/arm/armv7m.h"
#include "hw/or-irq.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/misc/unimp.h"
#include "hw/char/pl011.h"
#include "hw/ssi/pl022.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"

void custom_logger(const char *fmt, ...);

/**************
 * region handler for 0x5DFF0000 (early in bootloader)
 */
typedef struct {
    MemoryRegion mem;
    uint32_t f0x154;
} bl_mmio;

/* io range access */
static uint64_t bl_mmio_read(void *ptr, hwaddr addr, unsigned size)
{
    bl_mmio *mmi = (bl_mmio*) ptr;
    custom_logger( "0x5DFFxxxx read: %x %x\n", (int)addr, (int)size);
    switch((uint32_t)addr)
    {
       case 0x154: // 1st stage expects non-zero
           return mmi->f0x154;
    }
    return 0;
}

static void bl_mmio_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    bl_mmio *mmi = (bl_mmio*) ptr;
    custom_logger( "0x5DFFxxxx write: %x %x %x\n", (int)addr, (int)size, (int)val);
    switch((uint32_t)addr)
    {
       case 0x154: // 1st stage expects non-zero
           mmi->f0x154 = (int)val;
           break;
    }
    //   0x18 write e74a9d23
    //  0x154 write 0x1
    //  0x154 read  0x1
    // 0x1200 write 0x4
    // 0x1204 write 0x4
    // 0x1208 write 0x4
}

static const MemoryRegionOps bl_mmio_ops = {
    .read = bl_mmio_read,
    .write = bl_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

////////////////////////////////////////////////////////////////////////////////

/**************
 * region handler for 0x400F3000 (early in bootloader)
 */
typedef struct {
    MemoryRegion mem;
} mmio_0x400f;

/* io range access */
static uint64_t mmio_0x400f_read(void *ptr, hwaddr addr, unsigned size)
{
    //mmio_0x400f *mmi = (mmio_0x400f*) ptr;
    custom_logger( "0x400Fxxxx read: %x %x\n", (int)addr, (int)size);
    switch((uint32_t)addr)
    {
       case 0x3020: //
           return 4;
       case 0x3008:
           return 0x1000000;
           break;
       default:
           return 0;
    }
    return 0;
}

static void mmio_0x400f_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    //mmio_0x400f *mmi = (mmio_0x400f*) ptr;
    custom_logger( "0x400Fxxxx write: %x %x %x\n", (int)addr, (int)size, (int)val);
    switch((uint32_t)addr)
    {
       default:
           return;
    }
}

static const MemoryRegionOps mmio_0x400f_ops = {
    .read = mmio_0x400f_read,
    .write = mmio_0x400f_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

////////////////////////////////////////////////////////////////////////////////

/**************
 * region handler for 0x40093000 (early in bootloader)
 */
typedef struct {
    MemoryRegion mem;
} mmio_0x4009;

/* io range access */
static uint64_t mmio_0x4009_read(void *ptr, hwaddr addr, unsigned size)
{
    //mmio_0x4009 *mmi = (mmio_0x4009*) ptr;
    custom_logger( "0x4009xxxx read: %x %x\n", (int)addr, (int)size);
    switch((uint32_t)addr)
    {
       case 0x8200:
       case 0x9200:
       case 0xA200:
           // at func 5c60 reads from 8200 + id * 1000
           // at func 3184 bit 0x17 = 0; 0x14 = 1; 0x7 = 0; 0x6 =1
           return (1 << 0x14) + (1 << 0x6);
    }
    return 0;
}

static void mmio_0x4009_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    //mmio_0x4009 *mmi = (mmio_0x4009*) ptr;
    custom_logger( "0x4009xxxx write: %x %x %x\n", (int)addr, (int)size, (int)val);
    switch((uint32_t)addr)
    {
        default:
            return;
    }
}

static const MemoryRegionOps mmio_0x4009_ops = {
    .read = mmio_0x4009_read,
    .write = mmio_0x4009_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

////////////////////////////////////////////////////////////////////////////////

/**************
 * region handler for 0x400b3000 (early in bootloader)
 */
typedef struct {
    MemoryRegion mem;
    uint32_t f0xa240;
} mmio_0x400b;

/* io range access */
static uint64_t mmio_0x400b_read(void *ptr, hwaddr addr, unsigned size)
{
    mmio_0x400b *mmi = (mmio_0x400b*) ptr;
    uint32_t val = 0;
    switch((uint32_t)addr)
    {
       case 0xa240:
           //boot1 writes 0x1, then expect &10 == 0; then &10 == 1.
           val = mmi->f0xa240;
           if(val == 0x1){
              mmi->f0xa240 |= 0x10;
              val = mmi->f0xa240;
              break;
           }
           if(val == 0x11){
             mmi->f0xa240 &= ~(0x10);
             val = mmi->f0xa240;
             break;
           }
           break;
       case 0xA4DC:
           // in dryos already.

           break;
       default:
           break;
    }

    custom_logger( "0x400bxxxx read: %x(%x) == %x\n", (int)addr, (int)size, val);
    return val;
}

static void mmio_0x400b_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    mmio_0x400b *mmi = (mmio_0x400b*) ptr;
    custom_logger( "0x400bxxxx write: %x(%x) %x\n", (int)addr, (int)size, (int)val);
    switch((uint32_t)addr)
    {
       case 0xa240: // 1st stage expects non-zero
           mmi->f0xa240 = (int)val;
           break;
    }
}

static const MemoryRegionOps mmio_0x400b_ops = {
    .read = mmio_0x400b_read,
    .write = mmio_0x400b_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

////////////////////////////////////////////////////////////////////////////////

struct EOSMPUMachineClass {
    MachineClass parent;
};

struct EOSMPUMachineState {
    MachineState parent;

    ARMv7MState armv7m;
    MemoryRegion mpurom;
    MemoryRegion ram;
    MemoryRegion ramextra;
    bl_mmio *bl_mmio;
    mmio_0x400f *mmio_0x400f;
    mmio_0x4009 *mmio_0x4009;
    mmio_0x400b *mmio_0x400b;
};



#define TYPE_EOSMPU_MACHINE "eosmpu"
OBJECT_DECLARE_TYPE(EOSMPUMachineState, EOSMPUMachineClass, EOSMPU_MACHINE)

// Main SYSCLK frequency in Hz
// TMPM440F10XBG is 100MHz, is our custom part the same?
#define SYSCLK_FRQ 100000000

void custom_logger(const char *fmt, ...)
{
    va_list myargs;
    va_start(myargs, fmt);
    fprintf(stderr, fmt, myargs);
    va_end(myargs);
}


static void eosmpu_init(MachineState *machine)
{
    //static const int uart_irq[] = {0x3C, 0x3D, 0x3F };
    //static const int sio_uart_irq[] = {0x59, 0x5A, 0x5B };

    EOSMPUMachineState *mms = EOSMPU_MACHINE(machine);
    //EOSMPUMachineClass *mmc = EOSMPU_MACHINE_GET_CLASS(machine);
    MemoryRegion *system_memory = get_system_memory();
    //MachineClass *mc = MACHINE_GET_CLASS(machine);
    DeviceState *armv7m;
    Clock *sysclk;

  //            void memory_region_init_ram(MemoryRegion *mr, Object *owner, const char *name, uint64_t size,                                                                    , Error **errp)
  //  void memory_region_init_ram_from_file(MemoryRegion *mr, Object *owner, const char *name, uint64_t size, uint64_t align, uint32_t ram_flags, const char *path, bool readonly, Error **errp)
 
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    object_initialize_child(OBJECT(mms), "armv7m", &mms->armv7m, TYPE_ARMV7M);
    armv7m = DEVICE(&mms->armv7m);
    qdev_connect_clock_in(DEVICE(&mms->armv7m), "cpuclk", sysclk);

    //ROM at 0x0, size 0x100000; let's init this as RAM for now
    //memory_region_init_ram(&mms->mpurom, NULL, "eosmpu.mpurom", 0x100000, &error_fatal);
    memory_region_init_ram_from_file(&mms->mpurom, NULL, "eosmpu.mpurom", 0x100000, 0, RAM_PMEM, "/tmp/mpu.bin", 0, 1, &error_fatal);
    memory_region_add_subregion(system_memory, 0x0, &mms->mpurom);

    //RAM regions based on MEMR validator function
    // 0x20000000 - 0x2000DFFF
    memory_region_init_ram(&mms->ram, NULL, "eosmpu.ram", 0xE000, &error_fatal);
    memory_region_add_subregion(system_memory, 0x20000000, &mms->ram);

    // 0x22000000 - 0x221BFFFF
    memory_region_init_ram(&mms->ramextra, NULL, "eosmpu.ramextra", 0x200000, &error_fatal);
    memory_region_add_subregion(system_memory, 0x22000000, &mms->ramextra);

    // register region handlers
    *(&mms->bl_mmio) = g_new(bl_mmio, 1);
    memory_region_init_io(&mms->bl_mmio->mem, NULL, &bl_mmio_ops, &mms->bl_mmio, "eosmpu.bl_mmio", 0x10000);
    memory_region_add_subregion(system_memory, 0x5DFF0000, &mms->bl_mmio->mem);

    // PL011 UART at 0x44000000 + maybe extra at i * 0x1000
    /* for (i = 0; i < 2; i++) {
        if (board->dc2 & (1 << i)) {
            pl011_luminary_create(0x40000000 + i * 0x1000,
                                  qdev_get_gpio_in(nvic, uart_irq[i]),
                                  serial_hd(i));
        }
    } */

    *(&mms->mmio_0x4009) = g_new(mmio_0x4009, 1);
    memory_region_init_io(&mms->mmio_0x4009->mem, NULL, &mmio_0x4009_ops, &mms->mmio_0x4009, "eosmpu.mmio_0x4009", 0x10000);
    memory_region_add_subregion(system_memory, 0x40090000, &mms->mmio_0x4009->mem);

    *(&mms->mmio_0x400f) = g_new(mmio_0x400f, 1);
    memory_region_init_io(&mms->mmio_0x400f->mem, NULL, &mmio_0x400f_ops, &mms->mmio_0x400f, "eosmpu.mmio_0x400f", 0x10000);
    memory_region_add_subregion(system_memory, 0x400F0000, &mms->mmio_0x400f->mem);

    // SIO is at 400bb000 + i* 0x100; 4 channels
    // Toshiba specific implementation. R5 seems to use 400bb100 as UART
    *(&mms->mmio_0x400b) = g_new(mmio_0x400b, 1);
    memory_region_init_io(&mms->mmio_0x400b->mem, NULL, &mmio_0x400b_ops, &mms->mmio_0x400b, "eosmpu.mmio_0x400b", 0x10000);
    memory_region_add_subregion(system_memory, 0x400b0000, &mms->mmio_0x400b->mem);

    qdev_prop_set_string(armv7m, "cpu-type", machine->cpu_type);
    qdev_prop_set_bit(armv7m, "enable-bitband", true);

    object_property_set_link(OBJECT(&mms->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&mms->armv7m), &error_fatal);

    //unnsure, peripheral range. Are those just devices that are allowed to MEMR?
    // 0x40000000 - 0x40001fff
    // 0x40010000 - 0x4001ffff
    // 0x4003e000 - 0x4003ffff
    // 0x40050000 - 0x400fffff
    // 0x42000000 - 0x43ffffff
    // 0x5dff0000 - 0x5fffffff

    // 0x40000000 - 0x400fffff
    // 0x42000000 - 0x43ffffff
}

static void eosmpu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->max_cpus = 1;
    //not sure how to use it
    mc->default_ram_size = 16 * KiB;
    mc->default_ram_id = "eosmpu.default_ram";
}

static void eosmpu_r5_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Canon EOS MPU";
    mc->init = eosmpu_init;
    mc->max_cpus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m4");
}

static const TypeInfo eosmpu_info = {
    .name = TYPE_EOSMPU_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(EOSMPUMachineState),
    .class_size = sizeof(EOSMPUMachineClass),
    .class_init = eosmpu_class_init,
};

static const TypeInfo eosmpu_r5_info = {
    .name = MACHINE_TYPE_NAME("eosmpu-r5"),
    .parent = TYPE_EOSMPU_MACHINE,
    .class_init = eosmpu_r5_class_init,
};

static void eosmpu_machine_init(void)
{
    type_register_static(&eosmpu_info);
    type_register_static(&eosmpu_r5_info);
}

type_init(eosmpu_machine_init)
