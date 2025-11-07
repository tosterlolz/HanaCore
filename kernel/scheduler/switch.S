.section .text
.global context_switch
.type context_switch, @function
context_switch:
	# Minimal context switch: only save/restore callee-saved registers.

	# Push callee-saved registers of old task onto its stack
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	# Save old RSP value (store into *old_sp_ptr at %rdi)
	movq	%rsp, (%rdi)

	# Load new RSP value from *new_sp_ptr at %rsi
	movq	(%rsi), %rsp

	# Pop callee-saved registers for the new task
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	ret

	.size context_switch, .-context_switch
