# Faulty Oops

The snippet below contains the kernel output after running `echo "hello_world" > /dev/faulty` in the QEMU instance (line numbers added for reference):
```
[1] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[2] Mem abort info:
[3]   ESR = 0x0000000096000045
[4]   EC = 0x25: DABT (current EL), IL = 32 bits
[5]   SET = 0, FnV = 0
[6]   EA = 0, S1PTW = 0
[7]   FSC = 0x05: level 1 translation fault
[8] Data abort info:
[9]   ISV = 0, ISS = 0x00000045
[10]   CM = 0, WnR = 1
[11] user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b5d000
[12] [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
[13] Internal error: Oops: 0000000096000045 [#1] SMP
[14] Modules linked in: scull(O) hello(O) faulty(O)
[15] CPU: 0 PID: 159 Comm: sh Tainted: G           O       6.1.44 #1
[16] Hardware name: linux,dummy-virt (DT)
[17] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[18] pc : faulty_write+0x10/0x20 [faulty]
[19] lr : vfs_write+0xc8/0x390
[20] sp : ffffffc008dfbd20
[21] x29: ffffffc008dfbd80 x28: ffffff8001b34240 x27: 0000000000000000
[22] x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
[23] x23: 000000000000000c x22: 000000000000000c x21: ffffffc008dfbdc0
[24] x20: 000000557e6da990 x19: ffffff8001baf500 x18: 0000000000000000
[25] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
[26] x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
[27] x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
[28] x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
[29] x5 : 0000000000000001 x4 : ffffffc000780000 x3 : ffffffc008dfbdc0
[30] x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
[31] Call trace:
[32]  faulty_write+0x10/0x20 [faulty]
[33]  ksys_write+0x74/0x110
[34]  __arm64_sys_write+0x1c/0x30
[35]  invoke_syscall+0x54/0x130
[36]  el0_svc_common.constprop.0+0x44/0xf0
[37]  do_el0_svc+0x2c/0xc0
[38]  el0_svc+0x2c/0x90
[39]  el0t_64_sync_handler+0xf4/0x120
[40]  el0t_64_sync+0x18c/0x190
[41] Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
[42] ---[ end trace 0000000000000000 ]---
```

## Analysis
Starting with line 1, we can see that the core issue detected is a "kernel NULL pointer dereference".

Lines 2->7 provide information about the memory associated with the NULL pointer.

Lines 8->13 contain information about the data (i.e. "ISV=0" translates to "NOT an Independent Software Vendor").

The kernel modules linked to the issue are listed in line 14, which shows the 3 kernel modules we loaded as part of assignment 7:
"scull", "hello", and "faulty".

Lines 15->19 provide us with specific details related to the process ID (PID) that invoked the call to dereference a NULL pointer, which CPU the process was run on,
and the associated hardware name - in this case, that is "dummy-virt", the QEMU hardware type. Line 18, in particular, shows the name of the function "faulty_write" that
triggerd this error.

Lines 20->30 provide the memory addresses impacted by the kernel error.

Lines 31->40 lists the stack trace for the syscall to the NULL pointer, while line 41 points to the memory address of the code that caused this error.
