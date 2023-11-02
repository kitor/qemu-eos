/*
 * Canon EOS QOM PoC
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
#include "hw/cpu/a9mpcore.h"
#include "hw/or-irq.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"
#include "cpu.h"

// this shall go to header file
#define DIGIC8_PERIPHBASE       (0xC000000)
#define DIGIC8_GIC_CPU_IF_ADDR  DIGIC8_PERIPHBASE + 0x100
#define DIGIC8_NUM_CPUS         (2)
#define DIGIC8_NUM_IRQ_GIC      (64)
#define DIGIC8_NUM_IRQ_LEGACY   (512)

// this shall go into UART device implementation
typedef struct {
    MemoryRegion mem;
    uint32_t flags;
    uint32_t int_flags;
    uint32_t reg_st;
    uint32_t reg_rx;
    uint32_t uart_just_received;
} DigicUartState;

#define ST_RX_RDY (1 << 0)
#define ST_TX_RDY (1 << 1)

static uint64_t DigicUartDev_read(void *ptr, hwaddr addr, unsigned size) {
    DigicUartState *s = (DigicUartState*)ptr;
    
    //printf( "DigicUartDev read: %lx %x\n", addr, (int)size);
    switch((uint32_t)addr) { 
       case 0x0:
           return 0;
       case 0x04:
           //read char
           s->reg_st &= ~(ST_RX_RDY);
           return s->reg_rx;
       case 0x14:
           //return s->reg_st;
           return 2;
       default:
           return 0;
    }
    return 0;
}

static void DigicUartDev_write(void* ptr, hwaddr addr, uint64_t val, unsigned size)
{
    DigicUartState *s = (DigicUartState*)ptr;
    //printf( "DigicUartDev write: %lx %x %x\n", addr, (int)size, (int)val);
    switch((uint32_t)addr) {
      case 0x00:
          //write char
          if (val != (val & 0xFF)) {
              printf("Invalid uart char: '0x%08X'\n", (uint32_t)val);
	       	}
          printf("%c", (char)val);
          // add interrupt handling?
          break;
      case 0x08:
          s->flags &= ~0x800;
          break;
      case 0x10:
          // R writes 0x19
          break;
      case 0x14:
          if(val & 1) {
              // "Reset RX indicator";
              s->reg_st &= ~ST_RX_RDY;
              s->uart_just_received = 100;
          }
          else {
              s->reg_st = val;
          }
          break;
      case 0x18: 
          s->int_flags  = val & 1;
          break;
      default:
           return;
    }
}

static const MemoryRegionOps DigicUart_ops = {
    .read = DigicUartDev_read,
    .write = DigicUartDev_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

typedef struct DigicUart {
    uint32_t rx_int;
    uint32_t tx_int;
} DigicUart;

typedef struct SpiROMClass {
    hwaddr address;
    uint64_t size;
    uint32_t exists;
} SpiROMClass;

struct DIGIC8MachineClass {
    MachineClass parent;
    DigicUart uart;
    hwaddr boot_address;
};

struct DIGIC8MachineState {
    MachineClass parent;
    ARMCPU              cpu[DIGIC8_NUM_CPUS];
    A9MPPrivState       a9mpcore;
    DigicUartState      *uart;
};

#define TYPE_DIGIC8_MACHINE "digic8"
OBJECT_DECLARE_TYPE(DIGIC8MachineState, DIGIC8MachineClass, DIGIC8_MACHINE)

struct DIGIC8EOSMachineClass {
    DIGIC8MachineClass digic;
    uint64_t ram_size;
    SpiROMClass rom0;
    SpiROMClass rom1;
};

struct DIGIC8EOSMachineState {
    DIGIC8MachineState digic;
    MemoryRegion ram_cached;
    MemoryRegion ram_uncached;
    MemoryRegion tcm;
    MemoryRegion rom0;
    MemoryRegion rom1;
};

#define TYPE_DIGIC8_EOS_MACHINE "digic8-eos"
OBJECT_DECLARE_TYPE(DIGIC8EOSMachineState, DIGIC8EOSMachineClass, DIGIC8_EOS_MACHINE)

struct EOSRMachineClass {
    DIGIC8EOSMachineClass eos;
};

struct EOSRMachineState {
    DIGIC8EOSMachineState eos;
};

#define TYPE_EOSR_MACHINE "eos-r-machine"
OBJECT_DECLARE_TYPE(EOSRMachineState, EOSRMachineClass, EOSR_MACHINE)

// Main SYSCLK frequency in Hz (1ghz?)
#define SYSCLK_FRQ 1000000000



static void eos_r_init(MachineState *machine)
{
    printf("eos_r_init\n");
  
    EOSRMachineState *s = EOSR_MACHINE(machine);
    EOSRMachineClass *c = EOSR_MACHINE_GET_CLASS(machine);
    //MemoryRegion *system_memory = get_system_memory();
    int i;
 
    /*
    // What about clocks? a9 seems to run without one defined?
    Clock *sysclk;
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);
    // Yet NPCM7xx defines a clock like this:
    //object_initialize_child(machine, "clk", &s->eos.digic.clk, TYPE_NPCM7XX_CLK);
    */

    // Create CPU objects for cores.
    // For reference about Cortex A9 setup see npcm7xx.c
    for (i = 0; i < DIGIC8_NUM_CPUS; i++) {
        object_initialize_child((Object *)machine, "cpu[*]", &s->eos.digic.cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a9"));
    }  
    object_initialize_child((Object *)machine, "a9mpcore", &s->eos.digic.a9mpcore, TYPE_A9MPCORE_PRIV);
    

    //ROM0; let's init this as RAM for now
    memory_region_init_ram_from_file(&s->eos.rom0, NULL, "eos.rom0", 
            *(uint64_t *)&c->eos.rom0.size, 0, RAM_PMEM, "/tmp/rom0.bin", 0, 1, &error_fatal);
    memory_region_add_subregion(get_system_memory(), *(uint64_t *)&c->eos.rom0.address, &s->eos.rom0);
 
    //ROM1; let's init this as RAM for now
    if(&c->eos.rom1.size) {
        memory_region_init_ram_from_file(&s->eos.rom1, NULL, "eos.rom1", 
                *(uint64_t *)&c->eos.rom1.size, 0, RAM_PMEM, "/tmp/rom1.bin", 0, 1, &error_fatal);
        memory_region_add_subregion(get_system_memory(), *(uint64_t *)&c->eos.rom1.address, &s->eos.rom1);
    } 

    //RAM - uncacheable part (0x40000000 and above)
    uint64_t ram_size = c->eos.ram_size;
    memory_region_init_ram(&s->eos.ram_uncached, NULL, "eos.ram_uncached", ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x40000000, &s->eos.ram_uncached);
    
    //RAM - caheable part (0x0 up to 0x3FFFFFFF)
    uint64_t uncacheable_size = ram_size > 0x40000000 ? 0x40000000: ram_size;
    memory_region_init_alias(&s->eos.ram_cached, NULL, "eos.ram_cached", &s->eos.ram_uncached, 0x00000000, uncacheable_size);
    memory_region_add_subregion(get_system_memory(), 0x0, &s->eos.ram_cached); 

    // TCM, 0xDF000000 - size unknown, assume 0x01000000
    memory_region_init_ram(&s->eos.tcm, NULL, "eos.tcm", 0x1000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0xDF000000, &s->eos.tcm);

    // Connect UART dev
    *(&s->eos.digic.uart) = g_new(DigicUartState, 1);
    memory_region_init_io(&s->eos.digic.uart->mem, NULL, &DigicUart_ops, &s->eos.digic.uart, "digic8.uart", 0x1000);
    memory_region_add_subregion(get_system_memory(), 0xC0800000, &s->eos.digic.uart->mem);

    
    // realize
    for (i = 0; i < DIGIC8_NUM_CPUS; i++) {
        object_property_set_int(OBJECT(&s->eos.digic.cpu[i]), "mp-affinity",
                                arm_cpu_mp_affinity(i, 2),
                                &error_abort);
        object_property_set_int(OBJECT(&s->eos.digic.cpu[i]), "reset-cbar",
                                DIGIC8_PERIPHBASE, &error_abort); 
        object_property_set_bool(OBJECT(&s->eos.digic.cpu[i]), "reset-hivecs", true,
                                 &error_abort);

        /* Disable security extensions. */
        object_property_set_bool(OBJECT(&s->eos.digic.cpu[i]), "has_el3", false,
                                 &error_abort);

        if (!qdev_realize(DEVICE(&s->eos.digic.cpu[i]), NULL,  &error_fatal)) {
            return;
        }
    }
    
    /* A9MPCORE peripherals. Can only fail if we pass bad parameters here. */
    object_property_set_int(OBJECT(&s->eos.digic.a9mpcore), "num-cpu", DIGIC8_NUM_CPUS,
                            &error_abort);
    object_property_set_int(OBJECT(&s->eos.digic.a9mpcore), "num-irq", DIGIC8_NUM_IRQ_GIC,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->eos.digic.a9mpcore), &error_abort);
    //sysbus_mmio_map(SYS_BUS_DEVICE(&s->eos.digic.a9mpcore), 0, NPCM7XX_CPUP_BA); // GIC SCU?
    
    for (i = 0; i < DIGIC8_NUM_CPUS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eos.digic.a9mpcore), i,
                           qdev_get_gpio_in(DEVICE(&s->eos.digic.cpu[i]), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eos.digic.a9mpcore), i + DIGIC8_NUM_CPUS,
                           qdev_get_gpio_in(DEVICE(&s->eos.digic.cpu[i]), ARM_CPU_FIQ));
    }

    for (i = 0; i < DIGIC8_NUM_CPUS; i++) {  
        cpu_set_pc(CPU(&s->eos.digic.cpu[i]), c->eos.digic.boot_address);
    }
}

static void digic8_class_init(ObjectClass *oc, void *data)
{
    printf("digic8_class_init\n");
    // Digic 8 CPU
    MachineClass *mc = MACHINE_CLASS(oc);

    // ARM part is a dual-core ARM Cortex A9
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
    mc->max_cpus = DIGIC8_NUM_CPUS;
    mc->default_cpus = mc->max_cpus;
    
    // Early development, disable transaction failures at all
    mc->ignore_memory_transaction_failures = true;
  
    DIGIC8MachineClass *digic = DIGIC8_MACHINE_CLASS(oc);  
    // Built in "Canon UART" interrupts
    digic->uart.rx_int = 0x15D;
    digic->uart.tx_int = 0x16D;
    printf("/digic8_class_init\n");
}

static void digic8_eos_class_init(ObjectClass *oc, void *data)
{
    printf("digic8_eos_class_init\n");
    // Digic 8 "EOS" machine. Has MPU 
    // RAM 0x40000000 to 0xBFFFFFFF (up to 2GB)
    // RAM mirrored from 0x40000000 to 0x0 (up to 1GB)
    DIGIC8EOSMachineClass *eos = DIGIC8_EOS_MACHINE_CLASS(oc);
    // ROM0 at 0xE0000000, optional ROM1 at 0xF0000000
    eos->rom0.address = 0xE0000000;
    eos->rom1.address = 0xF0000000;
    
    // boots from 0xE0000000 (start of ROM0)
    eos->digic.boot_address = 0xE0000000;
    printf("/digic8_eos_class_init\n");
}

static void eos_r_class_init(ObjectClass *oc, void *data)
{
    printf("eos_r_class_init\n");
    // Canon EOS R
    // Digic 8 "EOS" with 2GB of RAM. Single SDXC UHS-II slot
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Canon EOS R";
    mc->init = eos_r_init;

    printf("eos_r_class_init get\n");
    EOSRMachineClass *device = EOSR_MACHINE_CLASS(oc);
    printf("eos_r_class_init set\n");
    device->eos.ram_size  = 0x80000000; // 2GB
    device->eos.rom0.size = 0x02000000;
    device->eos.rom1.size = 0x04000000;
    printf("/eos_r_class_init\n");
}

// Definition of CPUs
static const TypeInfo digic8_info = {
    .name = TYPE_DIGIC8_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(DIGIC8MachineState),
    .class_size = sizeof(DIGIC8MachineClass),
    .class_init = digic8_class_init,
};

// Definition of machine sub-types
static const TypeInfo digic8_eos_info = {
    .name = TYPE_DIGIC8_EOS_MACHINE,
    .parent = TYPE_DIGIC8_MACHINE,
    .abstract = true,
    .instance_size = sizeof(DIGIC8EOSMachineState),
    .class_size = sizeof(DIGIC8EOSMachineClass),
    .class_init = digic8_eos_class_init,
};

// Definition of cameras
static const TypeInfo eosr_info = {
    .name = "eos-r-machine", //MACHINE(TYPE_EOSR_MACHINE),
    .parent = TYPE_DIGIC8_EOS_MACHINE,
    .instance_size = sizeof(EOSRMachineState),
    .class_size = sizeof(EOSRMachineClass),
    .class_init = eos_r_class_init,
}; 

// Register types
static void eos_machine_init(void)
{
    type_register_static(&digic8_info);
    type_register_static(&digic8_eos_info);
    type_register_static(&eosr_info);
}

type_init(eos_machine_init)
