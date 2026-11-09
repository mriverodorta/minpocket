from pathlib import Path
import sys
root=Path(sys.argv[1])
f=root/'boot/image-board.c'
s=f.read_text()
anchor='\tdebug("   ramdisk start = 0x%08lx, ramdisk end = 0x%08lx\\n",'
assert s.count(anchor)==1
s=s.replace(anchor,'\tif (minpocket_extend_initrd(rd_start, rd_end))\n\t\treturn -EINVAL;\n'+anchor)
s='extern int minpocket_extend_initrd(unsigned long *, unsigned long *);\n'+s
f.write_text(s)
