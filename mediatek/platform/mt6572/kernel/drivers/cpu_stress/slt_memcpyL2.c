#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/module.h>
#include <asm/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>
#include <asm/io.h>
#include <asm/memory.h>
#include <asm/outercache.h>
#include <linux/spinlock.h>

#include <linux/leds-mt65xx.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include "slt.h"

extern int fp3_memcpyL2_start(int cpu_id);

static int g_iCPU0_PassFail, g_iCPU1_PassFail;
static int g_iMemcpyL2LoopCount;
int g_iTestMem_CPU0, g_iTestMem_CPU1;

static struct device_driver slt_cpu0_memcpyL2_drv =
{
    .name = "slt_cpu0_memcpyL2",
    .bus = &platform_bus_type,
    .owner = THIS_MODULE,
};

static struct device_driver slt_cpu1_memcpyL2_drv =
{
    .name = "slt_cpu1_memcpyL2",
    .bus = &platform_bus_type,
    .owner = THIS_MODULE,
};

static struct device_driver slt_memcpyL2_loop_count_drv =
{
    .name = "slt_memcpyL2_loop_count",
    .bus = &platform_bus_type,
    .owner = THIS_MODULE,
};

#define DEFINE_SLT_CPU_MEMCPYL2_SHOW(_N)    \
static ssize_t slt_cpu##_N##_memcpyL2_show(struct device_driver *driver, char *buf) \
{   \
    if(g_iCPU##_N##_PassFail == -1) \
        return snprintf(buf, PAGE_SIZE, "CPU%d MemcpyL2 - CPU%d is powered off\n", _N, _N); \
    else    \
        return snprintf(buf, PAGE_SIZE, "CPU%d MemcpyL2 - %s(loop_count = %d)\n", _N, g_iCPU##_N##_PassFail != g_iMemcpyL2LoopCount ? "FAIL" : "PASS", g_iCPU##_N##_PassFail);  \
}

DEFINE_SLT_CPU_MEMCPYL2_SHOW(0)
DEFINE_SLT_CPU_MEMCPYL2_SHOW(1)

static ssize_t slt_memcpyL2_loop_count_show(struct device_driver *driver, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "MemcpyL2 Loop Count = %d\n", g_iMemcpyL2LoopCount);
}

#define DEFINE_SLT_CPU_MEMCPYL2_STORE(_N)    \
static ssize_t slt_cpu##_N##_memcpyL2_store(struct device_driver *driver, const char *buf, size_t count)    \
{   \
    unsigned int i, ret;    \
    unsigned long mask; \
    int retry=0;    \
    unsigned int pTestMem; \
    DEFINE_SPINLOCK(cpu##_N##_lock);    \
    unsigned long cpu##_N##_flags;     \
    \
    g_iCPU##_N##_PassFail = 0;  \
    \
    pTestMem = (unsigned int)vmalloc(64*1024);  \
    /* pTestMem = pTestMem & 0xFFF80000; */ \
    if((void*)pTestMem == NULL) \
    {   \
        printk("allocate memory for cpu%d speed test fail\n", _N);  \
        return 0;   \
    }   \
    else    \
    {   \
        g_iTestMem_CPU##_N = pTestMem;  \
    }   \
    \
    mask = (1 << _N); /* processor _N */ \
    while(sched_setaffinity(0, (struct cpumask*) &mask) < 0)    \
    {   \
        printk("Could not set cpu%d affinity for current process(%d).\n", _N, retry);  \
        g_iCPU##_N##_PassFail = -1; \
        retry++;    \
        if(retry > 100) \
        {   \
            vfree((void*)pTestMem); \
            return count;   \
        }   \
    }   \
    \
    printk("\n>> CPU%d MemcpyL2 test start (cpu id = %d) <<\n\n", _N, raw_smp_processor_id());  \
    \
    for (i = 0, g_iCPU##_N##_PassFail = 0; i < g_iMemcpyL2LoopCount; i++) {    \
        spin_lock_irqsave(&cpu##_N##_lock, cpu##_N##_flags);    \
        ret = fp3_memcpyL2_start(_N);    /* 1: PASS, 0:Fail, -1: target CPU power off */  \
        spin_unlock_irqrestore(&cpu##_N##_lock, cpu##_N##_flags);   \
        if(ret != -1)   \
        {   \
             g_iCPU##_N##_PassFail += ret;  \
        }   \
        else    \
        {   \
             g_iCPU##_N##_PassFail = -1;    \
             break; \
        }   \
    }   \
    \
    if (g_iCPU##_N##_PassFail == g_iMemcpyL2LoopCount) {    \
        printk("\n>> CPU%d memcpyL2 test pass <<\n\n", _N); \
    }else { \
        printk("\n>> CPU%d memcpyL2 test fail (loop count = %d)<<\n\n", _N, g_iCPU##_N##_PassFail);  \
    }   \
    \
    vfree((void*)pTestMem); \
    return count;   \
}

DEFINE_SLT_CPU_MEMCPYL2_STORE(0)
DEFINE_SLT_CPU_MEMCPYL2_STORE(1)

static ssize_t slt_memcpyL2_loop_count_store(struct device_driver *driver, const char *buf, size_t count)
{
    int result;

    if ((result = sscanf(buf, "%d", &g_iMemcpyL2LoopCount)) == 1)
    {
        printk("set SLT MemcpyL2 test loop count = %d successfully\n", g_iMemcpyL2LoopCount);
    }
    else
    {
        printk("bad argument!!\n");
        return -EINVAL;
    }

    return count;
}

DRIVER_ATTR(slt_cpu0_memcpyL2, 0644, slt_cpu0_memcpyL2_show, slt_cpu0_memcpyL2_store);
DRIVER_ATTR(slt_cpu1_memcpyL2, 0644, slt_cpu1_memcpyL2_show, slt_cpu1_memcpyL2_store);
DRIVER_ATTR(slt_memcpyL2_loop_count, 0644, slt_memcpyL2_loop_count_show, slt_memcpyL2_loop_count_store);

#define DEFINE_SLT_CPU_MEMCPYL2_INIT(_N)    \
int __init slt_cpu##_N##_memcpyL2_init(void) \
{   \
    int ret;    \
    \
    ret = driver_register(&slt_cpu##_N##_memcpyL2_drv);  \
    if (ret) {  \
        printk("fail to create SLT CPU%d MemcpyL2 driver\n", _N);    \
    }   \
    else    \
    {   \
        printk("success to create SLT CPU%d MemcpyL2 driver\n", _N); \
    }   \
    \
    ret = driver_create_file(&slt_cpu##_N##_memcpyL2_drv, &driver_attr_slt_cpu##_N##_memcpyL2);   \
    if (ret) {  \
        printk("fail to create SLT CPU%d MemcpyL2 sysfs files\n", _N);   \
    }   \
    else    \
    {   \
        printk("success to create SLT CPU%d MemcpyL2 sysfs files\n", _N);    \
    }   \
    \
    return 0;   \
}

DEFINE_SLT_CPU_MEMCPYL2_INIT(0)
DEFINE_SLT_CPU_MEMCPYL2_INIT(1)

int __init slt_memcpyL2_loop_count_init(void)
{
    int ret;

    ret = driver_register(&slt_memcpyL2_loop_count_drv);
    if (ret) {
        printk("fail to create MemcpyL2 loop count driver\n");
    }
    else
    {
        printk("success to create MemcpyL2 loop count driver\n");
    }


    ret = driver_create_file(&slt_memcpyL2_loop_count_drv, &driver_attr_slt_memcpyL2_loop_count);
    if (ret) {
        printk("fail to create MemcpyL2 loop count sysfs files\n");
    }
    else
    {
        printk("success to create MemcpyL2 loop count sysfs files\n");
    }

    g_iMemcpyL2LoopCount = SLT_LOOP_CNT;

    return 0;
}
arch_initcall(slt_cpu0_memcpyL2_init);
arch_initcall(slt_cpu1_memcpyL2_init);
arch_initcall(slt_memcpyL2_loop_count_init);
