	.text
	.globl	asm_co_run
	.type	asm_co_run, @function
asm_co_run:
.LFB0:
	.cfi_startproc
	pushq	%rbp
	pushq	%rbx
	movq	%rsp, %rbp
	movq	%rdi, %rbx
	movq	%rsp, 0x20(%rbx)		# rsp -> _co_ret_yield_rsp
	movq	0x10(%rbx), %rsp		# _co_stack_top -> rsp

	leaq	.co_run_lea_pt, %rax
	movq	%rax, 0x28(%rbx)

.co_run_lea_pt:
	movq	0x38(%rbx), %rax
	testq	%rax, %rax
	jne		.co_run_return

	movq	8(%rbx), %rcx
	callq	*%rcx

	movq	$0x0, 0x40(%rbx)
	movq	0x30(%rbx), %rax
	testq	%rax, %rax
	jne		.co_run_back_to_resume_pos

.co_run_return:

	movq	$0x0, 0x38(%rbx)
	movq	0x20(%rbx), %rsp			# recover rsp
	popq	%rbx
	popq	%rbp
	retq

.co_run_back_to_resume_pos:
	movq	0x20(%rbx), %rsp			# recover rsp
	movq	0x48(%rbx), %rax
	movq	$0x01, 0x38(%rbx)
	jmpq	*%rax

	.cfi_endproc
.LFE0:
	.size	asm_co_run, .-asm_co_run
	.section	.note.GNU-stack,"",@progbits
