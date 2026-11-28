/*
 * MINIX has no BPF JIT: NPF's stand/bpf_filter.c wraps libbpfjit, so give it
 * a compiler that always declines.  NPF then runs rules in the interpreter.
 */
#ifndef _NPF_MINIX_BPFJIT_H_
#define _NPF_MINIX_BPFJIT_H_

static inline bpfjit_func_t
bpfjit_generate_code(const bpf_ctx_t *bc, const struct bpf_insn *insns,
    size_t insn_count)
{
	(void)bc; (void)insns; (void)insn_count;
	return NULL;
}

static inline void
bpfjit_free_code(bpfjit_func_t func)
{
	(void)func;
}

#endif
