#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/text-patching.h>

static int __init my_init(void)
{
    // Replace this with the actual address of the instruction to patch
    void *target = (void *)0xffffffff812d1fad;

    // Example: sar $0x2, %rax = 48 c1 f8 02
    unsigned char new_insn[] = { 0x48, 0xc1, 0xf8, 0x02 };

    pr_info("Patching instruction at %px\n", target);
    text_poke(target, new_insn, sizeof(new_insn));

    return 0;
}

static void __exit my_exit(void)
{
    // Optional: restore original instruction if known
}

module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
