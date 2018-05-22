	.text
	.globl	asm_yield_task
	.type	asm_yield_task, @function
asm_yield_task:
.LFB0:
	.cfi_startproc
# must be called from usr stack
# put code here:
	pushq	%rbx
	movq	%rdi, %rax
	movq	48(%rax), %rcx

	leaq	0x0(%rip), %r8
	nop							# jump from asm_resume_task

	addq	$72, %rdi

	movq	%rax, (%rdi)
	movq	%rdx, 24(%rdi)
	movq	%rsp, 48(%rdi)
	movq	%rbp, 56(%rdi)	# rbp of task function

	leaq	0x0(%rip), %r8
	nop						###2

	movq	48(%rax), %rcx
	andq	$2, %rcx
	testq	%rcx, %rcx
	jne		.ASM_YIELD_TASK_RESUME_POS

	orq		$1, 48(%rax)		# jump to run_task
	movq	56(%rax), %r9
	movq	%r8, 32(%rax)


	popq	%rbx
	movq	64(%rax), %rbp
	movq	40(%rax), %rsp
	jmp		*%r9				# to ###1

.ASM_YIELD_TASK_RESUME_POS:
	andq	$-3, 48(%rax)
	movq	56(%rdi), %rbp

	popq	%rbx
	ret
	.cfi_endproc
.LFE0:
	.size	asm_yield_task, .-asm_yield_task
	.ident	"GCC: (Ubuntu 5.4.0-6ubuntu1~16.04.4) 5.4.0 20160609"
	.section	.note.GNU-stack,"",@progbits
