	.text
	.globl	asm_co_yield
	.type	asm_co_yield, @function
asm_co_yield:
.LFB0:
	.cfi_startproc
	pushq	%rbp
	pushq	%rbx

	movq	%rdi, %rbx

	movq	$0x0, 0x38(%rbx)

	movq    0x10(%rbx),%rax
	movq    %rax,%rdx
	movq    %rdx,(%rax)
	movq    %rbx,%rdx
	movq    %rdx,0x8(%rax)
	movq    %rcx,%rdx
	movq    %rdx,0x10(%rax)
	movq    %rdx,%rdx
	movq    %rdx,0x18(%rax)
	movq    %rdi,%rdx
	movq    %rdx,0x20(%rax)
	movq    %rsi,%rdx
	movq    %rdx,0x28(%rax)
	movq    %rbp,%rdx
	movq    %rdx,0x30(%rax)
	movq    %rsp,%rdx
	movq    %rdx,0x38(%rax)
	movq    %r8,%rdx
	movq    %rdx,0x40(%rax)
	movq    %r9,%rdx
	movq    %rdx,0x48(%rax)
	movq    %r10,%rdx
	movq    %rdx,0x50(%rax)
	movq    %r11,%rdx
	movq    %rdx,0x58(%rax)
	movq    %r12,%rdx
	movq    %rdx,0x60(%rax)
	movq    %r13,%rdx
	movq    %rdx,0x68(%rax)
	movq    %r14,%rdx
	movq    %rdx,0x70(%rax)
	movq    %r15,%rdx
	movq    %rdx,0x78(%rax)

	movb	0x09(%rbx), %al
	testb	%al, %al
	jne		.co_yield_resume_pos

	movq	0x28(%rbx), %rax

	leaq	.co_yield_resume_pos, %rdx
	movq	%rdx, 0x28(%rbx)
	movb	$0x01, 0x09(%rbx)

	movq	0x20(%rbx), %rsp

# rax: first time: .co_run_lea_pt
# rax: after first resume: .co_resume_lea_pt -> .co_resume_return
	jmpq	*%rax

.co_yield_resume_pos:
	movb	$0x0, 0x09(%rbx)
	popq	%rbx
	popq	%rbp
	retq

	.cfi_endproc
.LFE0:
	.size	asm_co_yield, .-asm_co_yield
	.section	.note.GNU-stack,"",@progbits
