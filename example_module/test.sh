#!/bin/bash

# $ bash example_module/test.sh
# $ insmod example_module/rewrite.ko target_addr=0xffffffff812d1fad new_insn=0x48,0xc1,0xf8,0x02
# $ bash example_module/test.sh

set -e

truncate -s 0 /tmp/disas.txt

FUNC_NAME=${1:-"ttwu_do_activate.constprop.0"}

TEXT_OFFSET=$(     readelf -W --segments /proc/kcore |\
                   grep '^  LOAD' |\
                   awk '{ print $2; }' |\
                   head -1 | tail -1 )
TEXT_VA_START=$(   readelf -W --segments /proc/kcore |\
                   grep '^  LOAD' |\
                   awk '{ print $3; }' |\
                   head -1 | tail -1 )
TEXT_VA=0x$(       grep " $FUNC_NAME\$" /proc/kallsyms |\
                   awk '{ print $1; }' )
TEXT_VA2=0x$(      grep " $FUNC_NAME\$" -A1 /proc/kallsyms |\
                   tail -n1 |\
                   awk '{ print $1; }' )

dd if=/proc/kcore of=/tmp/insns.bin bs=1 \
skip=$((TEXT_VA-TEXT_VA_START+TEXT_OFFSET)) \
count=$((TEXT_VA2-TEXT_VA)) status=none

objdump -b binary -m i386:x86-64 -D /tmp/insns.bin --adjust-vma=$TEXT_VA > /tmp/disas.txt

cat /tmp/disas.txt
rm -f /tmp/insn.bin
