// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clean-room, separately named diagnostic driver for Samsung A17.
 * It reports the ABI revision and explicitly rejects unsupported
 * original RT operations. NOT the original or functional RT Driver.
 */
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include "rt_abi.h"

static int rt_a17_open(struct inode *inode, struct file *file)
{
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
    /* no_llseek was removed before Linux 6.12. */
    return nonseekable_open(inode, file);
}

static long rt_a17_ioctl(struct file *file, unsigned int cmd,
                         unsigned long arg)
{
    const struct rt_a17_info info = {
        .abi = RT_A17_ABI_VERSION,
        .capabilities = 0,
        .original_ioctls_implemented = 0,
        .reserved = 0,
    };

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
    if (cmd == RT_A17_INFO)
        return copy_to_user((void __user *)arg, &info, sizeof(info))
            ? -EFAULT : 0;

    switch (cmd) {
    case RT_OP_INIT_KEY:
    case RT_OP_READ_MEM:
    case RT_OP_WRITE_MEM:
    case RT_OP_MODULE_BASE:
    case RT_OP_GYRO_CONFIG:
    case RT_OP_BP_GET_HITS:
    case RT_OP_BP_CONTROL:
        return -EOPNOTSUPP;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations rt_a17_fops = {
    .owner = THIS_MODULE,
    .open = rt_a17_open,
    .unlocked_ioctl = rt_a17_ioctl,
};

static struct miscdevice rt_a17_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "rt_a17_diag",
    .fops = &rt_a17_fops,
    .mode = 0600,
};

static int __init rt_a17_init(void)
{
    int ret = misc_register(&rt_a17_device);
    if (!ret)
        pr_info("rt_a17_diag: ABI research interface registered\n");
    return ret;
}

static void __exit rt_a17_exit(void)
{
    misc_deregister(&rt_a17_device);
}
module_init(rt_a17_init);
module_exit(rt_a17_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A17 DWARF RT ABI diagnostics only; no memory access");
MODULE_VERSION("0.2");
