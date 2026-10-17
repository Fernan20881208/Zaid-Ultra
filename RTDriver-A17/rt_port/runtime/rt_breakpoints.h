/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_BREAKPOINTS_H
#define RT_A17_BREAKPOINTS_H
#include <linux/fs.h>
#include <linux/uaccess.h>

int rt_bp_open(struct file *file);
int rt_bp_release(struct file *file);
long rt_bp_ioctl(struct file *file, unsigned int cmd, void __user *arg);
#endif
