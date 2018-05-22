	.text
	.globl	asm_run_task
	.type	asm_run_task, @function
asm_run_task:
.LFB0:
	.cfi_startproc
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx

	pushq	%rdi

	movq	%rsp, %r9

	movq	(%rdi), %rdx
	movq	8(%rdi), %rax			# %rax: stack size
	leaq	(%rdx, %rax, 1), %rsp	# move rsp to the stack top

#run under usr stack:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	movq	%rbp, 64(%rdi)		# save usr stack %rbp
	pushq	%r9					# push original rsp in usr stack
	subq	$8, %rsp
	movq	%rdi, (%rsp)

	movq	32(%rdi), %r9

	leaq	0x0(%rip), %r8
	nop							###1

	movq	(%rsp), %rdi

	movq	48(%rdi), %rcx
	andq	$1, %rcx
	testq	%rcx, %rcx
	jne		.ASM_RUN_TASK_YIELD_OVER

	movq	%r8, 56(%rdi)
	movq	%rsp, 40(%rdi)
	call	*16(%rdi)				# call task function

	movq	(%rsp), %rdi
	orq		$4, 48(%rdi)

.ASM_RUN_TASK_YIELD_OVER:

	andq	$-2, 48(%rdi)

	addq	$8, %rsp
	popq	%r9					# restore original rsp
	popq	%rbx
	popq	%rbp
#
	movq	%r9, %rsp

	popq	%rdi
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	asm_run_task, .-asm_run_task
	.ident	"GCC: (Ubuntu 5.4.0-6ubuntu1~16.04.4) 5.4.0 20160609"
	.section	.note.GNU-stack,"",@progbits
