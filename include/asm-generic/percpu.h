#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>

#define __GENERIC_PER_CPU
#ifdef CONFIG_SMP

extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu_offset(x) (__per_cpu_offset[x])

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name
#define DEFINE_PER_CPU_LOCKED(type, name) \
    __attribute__((__section__(".data.percpu"))) __DEFINE_SPINLOCK(per_cpu_lock__##name##_locked); \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name##_locked

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) per_cpu(var, smp_processor_id())
#define __raw_get_cpu_var(var) per_cpu(var, raw_smp_processor_id())

#define per_cpu_lock(var, cpu) \
	(*RELOC_HIDE(&per_cpu_lock__##var##_locked, __per_cpu_offset[cpu]))
#define per_cpu_var_locked(var, cpu) \
		(*RELOC_HIDE(&per_cpu__##var##_locked, __per_cpu_offset[cpu]))
#define __get_cpu_lock(var, cpu) \
		per_cpu_lock(var, cpu)
#define __get_cpu_var_locked(var, cpu) \
		per_cpu_var_locked(var, cpu)

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for_each_possible_cpu(__i)				\
		memcpy((pcpudst)+__per_cpu_offset[__i],		\
		       (src), (size));				\
} while (0)
#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) per_cpu__##name
#define DEFINE_PER_CPU_LOCKED(type, name) \
    __DEFINE_SPINLOCK(per_cpu_lock__##name##_locked); \
    __typeof__(type) per_cpu__##name##_locked

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu__##var))
#define per_cpu_var_locked(var, cpu)			(*((void)(cpu), &per_cpu__##var##_locked))
#define __get_cpu_var(var)			per_cpu__##var
#define __raw_get_cpu_var(var)			per_cpu__##var
#define __get_cpu_lock(var, cpu)		per_cpu_lock__##var##_locked
#define __get_cpu_var_locked(var, cpu)		per_cpu__##var##_locked

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name
#define DECLARE_PER_CPU_LOCKED(type, name) \
    extern spinlock_t per_cpu_lock__##name##_locked; \
    extern __typeof__(type) per_cpu__##name##_locked

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)
#define EXPORT_PER_CPU_LOCKED_SYMBOL(var) EXPORT_SYMBOL(per_cpu_lock__##var##_locked); EXPORT_SYMBOL(per_cpu__##var##_locked)
#define EXPORT_PER_CPU_LOCKED_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu_lock__##var##_locked); EXPORT_SYMBOL_GPL(per_cpu__##var##_locked)

#endif /* _ASM_GENERIC_PERCPU_H_ */
