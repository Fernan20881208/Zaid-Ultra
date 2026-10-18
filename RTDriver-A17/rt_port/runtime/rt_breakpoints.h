/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_BREAKPOINTS_H
#define RT_A17_BREAKPOINTS_H
#include <linux/fs.h>
#include <linux/uaccess.h>

#ifdef RT_A17_MEMORY_ONLY
#include <linux/errno.h>
static inline int rt_bp_open(struct file *file) { return 0; }
static inline int rt_bp_release(struct file *file) { return 0; }
static inline long rt_bp_ioctl(struct file *file, unsigned int cmd,
			      void __user *arg)
{
	return -EOPNOTSUPP;
}
#else
int rt_bp_open(struct file *file);
int rt_bp_release(struct file *file);
long rt_bp_ioctl(struct file *file, unsigned int cmd, void __user *arg);
#endif
#endif
