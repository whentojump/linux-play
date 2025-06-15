#include <linux/syscalls.h>
#include <linux/debugfs.h>

static unsigned long x = 0xdeadbeef;

SYSCALL_DEFINE0(dummy)
{
	x <<= 4;
	printk(KERN_INFO "__x64_sys_dummy: 0x%lx\n", x);
	return 0;
}

SYSCALL_DEFINE0(dummy2)
{
	printk(KERN_INFO "__x64_sys_dummy2: 0x%lx\n", x);
	return 0;
}

static ssize_t dummy_debugfs_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	char c;

	if (copy_from_user(&c, buf, 1))
		return -EFAULT;

	if (c == '1') {
		x = 0xdeadbeef;
	}

	return count;
}

static const struct file_operations dummy_fops = {
	.owner = THIS_MODULE,
	.write = dummy_debugfs_write,
};

static int __init dummy_debugfs_init(void)
{
	struct dentry *dir, *file;

	dir = debugfs_create_dir("dummy", NULL);
	if (!dir) {
		pr_err("dummy: failed to create debugfs dir\n");
		return -ENOMEM;
	}

	file = debugfs_create_file("reset", 0222, dir, NULL, &dummy_fops);
	if (!file) {
		debugfs_remove(dir);
		pr_err("dummy: failed to create debugfs file\n");
		return -ENOMEM;
	}

	pr_info("dummy: debugfs interface created\n");
	return 0;
}

late_initcall(dummy_debugfs_init);
