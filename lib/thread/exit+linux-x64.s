/*
const thread.exit : (-> void)
*/
.globl thread$exit
thread$exit:
	subq	$0x10,%rsp

	/* arch_prctl(op, addr) */
	movq	$158,%rax	/* arch_prctl */
	movq	$0x1003,%rdi	/* Archgetfs */
	movq	%rsp,%rsi	/* addr */
	syscall

	movslq	%fs:0x8,%r10	/* stksz */
	movslq	%fs:0xc,%r11	/* nslots */

	/* munmap(base, size) */
	movq	$11,%rax	/* munmap */
	movq	(%rsp),%rdi	/* base */
	subq	%r10,%rdi
	leaq	0x10(%r10, %r11, 0x8),%rsi	/* stksz + sizeof tls region */
	addq	$0xf,%rsi
	andq	$~0xf,%rsi
	syscall

	/* thread_exit(0) */
	movq	$60,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
