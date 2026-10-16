// SPDX-License-Identifier: GPL-2.0-only
/* Build/load compatibility test module — NOT RT Driver.
 * No process memory access, hooks, IOCTL handlers or persistence.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <generated/utsrelease.h>

static int __init a17_compat_init(void)
{
    pr_info("a17_compat_probe: started on %s\n", UTS_RELEASE);
    return 0;
}

static void __exit a17_compat_exit(void)
{
    pr_info("a17_compat_probe: removed\n");
}

module_init(a17_compat_init);
module_exit(a17_compat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung A17 kernel ABI test only; not RT Driver");
MODULE_VERSION("0.2");
