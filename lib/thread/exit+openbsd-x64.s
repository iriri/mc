/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl thread$exit
thread$exit:
	/* 
	  Because OpenBSD wants a valid stack whenever
	  we enter the kernel, we need to toss a preallocated
	  stack pointer into %rsp.
	 */
	movq	thread$exitstk,%rsp

	/* __get_tcb() */
	movq	$330,%rax
	syscall

	movslq	%fs:0x8,%r10	/* stksz */
	movslq	%fs:0xc,%r11	/* nslots */

	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movq	%rax,%rdi	/* base */
	subq	%r10,%rdi
	leaq	0x10(%r10, %r11, 0x8),%rsi	/* stksz + sizeof tls region */
	addq	$0xf,%rsi
	andq	$~0xf,%rsi
	syscall

	/* __threxit(0) */
	movq	$302,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
