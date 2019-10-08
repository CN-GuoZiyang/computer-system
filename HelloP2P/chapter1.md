### <center>第一章 概述</center>

#### 1.1 Hello简介

hello.c文件以ascii码的形式将C语言代码存在磁盘或固态硬盘上。C预处理器（cpp）首先将hello.c文件翻译成ascii码的预处理文件hello.i。在预处理文件中包含着一些头文件的路径以及宏定义等。接着，C编译器（cc1）将hello.i翻译成ascii存储等汇编语言文件hello.s。然后，汇编器（as）将hello.s翻译成一个二进制形式的可重定位目标文件hello.o，可重定位目标文件hello.o中包含有二进制的代码和数据。最后，链接器程序（ld）将hello.o与其它.o文件（如果有的话）以及系统的目标文件链接起来，生成二进制的可执行目标文件hello。用户通过壳（shell）输入运行hello的命令，shell解析该命令之后，fork出一个子进程，在该子进程中调用execve以运行hello。

内核调用系统函数mmap函数将磁盘（硬盘）文件映射到进程的虚拟内存空间。接着进程开始逐步对这片映射空间进行访问，引发缺页异常，逐步将文件中内容拷贝到主存。CPU将虚拟内存地址发送给内存管理单元（MMU），MMU向主存中常驻的页表请求对应的物理地址，并将该物理地址发送给主存，主存取出数据返回给CPU。翻译后备缓冲器（TLB）加速了这一过程。由于数据量较大，页表分为四级，通常只有一级页表常驻主存，剩下的都存在磁盘上。处理器开始逐句逐句取指、译码、执行，并且在多进程的情况下进行分时管理。如果需要请求磁盘数据，处理器会首先向最高级缓存请求，如果命中直接返回，如果不命中则向下级缓存请求直到命中为止，接着再逐级写回（必要时驱逐）。如遇到要对磁盘或者输入输出流进行操作，则此时产生了一个异常，操作系统向hello进程发送一个信号，使其暂时挂起，执行异常处理程序，并进入内核模式，执行上下文切换操作切换至其它进程执行，直到对磁盘或者IO的操作结束后，操作系统又切换回hello进程继续执行。

hello进程结束之后，shell将对hello进程执行回收操作，如果shell（bash）在hello进程结束之前已终止，则hello进程成为一个僵尸进程，不久之后将由操作系统的pid为1的进程——init进程负责回收。

#### 1.2 环境与工具

硬件环境：

- Intel(R) Core(TM) i7-6700HQ CPU
- 8GB RAM
- Nvidia GTX 1060
- 128GB SSD

软件环境：

- Windows 10 Professional x64
- Ubuntu 18.04.1
- Manjaro

工具：

- edb
- gdb
- cgdb
- codeblocks
- vscode
- vmware

#### 1.3 中间结果

- hello.i：hello的预处理文件
- hello.s：hello的编译文件
- hello.o：hello的可重定位目标文件
- hello：hello的可执行目标文件（静态）
- hellodynamic：hello的可执行目标文件（动态）
- hellooelf.txt：hello.o的elf信息
- helloodump.txt：hello.o的反汇编信息
- helloelf.txt：hello的elf信息
- hellodump.txt：hello的反汇编信息
- hellosymbols.txt：hello的符号表
- pstree.txt：hello执行时的进程树

#### 1.4 本章小结

每一个程序——即使是最简单的hello，都涉及了精巧的设计和复杂的操作，从硬件到软件的全方位配合，才使得现今计算机如此高效。这些复杂精巧的设计和机制值得每一位CS学生去学习。从底层出发，才能更好地进行顶层的设计。接下来，我们就要对hello从编译到执行再到被回收的每一步进行深入的了解。