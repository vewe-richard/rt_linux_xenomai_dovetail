# Step 1 Conflict Log — Dovetail patches applied on QCOM 6.6.119

Branch: `dovetail-integration`
Date: 2026-05-15
Total patches: 171

## Conflicts Encountered

### 0085-x86-fpu-dovetail-enable-alternate-scheduling.patch

**Error type**: `sha1 information is lacking or useless (arch/x86/kernel/process_64.c)`

**Files affected**:
- `arch/x86/include/asm/dovetail.h` — clean
- `arch/x86/include/asm/fpu/api.h` — clean
- `arch/x86/include/asm/fpu/sched.h` — clean
- `arch/x86/include/asm/fpu/types.h` — clean
- `arch/x86/kernel/fpu/core.c` — clean (offset 3 lines)
- `arch/x86/kernel/fpu/signal.c` — 3 hunks rejected
- `arch/x86/kernel/fpu/xstate.c` — clean (offset 4 lines)
- `arch/x86/kernel/process_64.c` — clean

**Problem in signal.c**: QCOM tree has different context in 3 functions. Patch changes `fpregs_lock()` to `flags = fpregs_lock()` and `fpregs_unlock()` to `fpregs_unlock(flags)`, adds `unsigned long flags;` declarations.

**Resolution**: Manually applied 3 rejected hunks:
1. `copy_fpstate_to_sigframe()` — change fpregs_lock/unlock calls (flags already declared)
2. `restore_fpregs_from_user()` — add `unsigned long flags;` + change fpregs_lock call
3. `__fpu_restore_sig()` — add `unsigned long flags;` declaration

### 0132-x86-dovetail-Allow-multiple-HV-guest-extensions-to-b.patch

**Error type**: `sha1 information is lacking or useless (arch/x86/include/asm/idtentry.h)`

**Files affected**:
- `arch/x86/include/asm/idtentry.h` — 1 hunk rejected
- All other files clean

**Problem**: QCOM tree has an extra `#define DEFINE_FREDENTRY_DEBUG` between `DEFINE_IDTENTRY_DEBUG_USER` and `#else`. Patch expects to insert `sysvec_install` macro right after `#define DEFINE_IDTENTRY_DEBUG_USER`.

**Resolution**: Inserted `sysvec_install` macro after the QCOM-added `#define DEFINE_FREDENTRY_DEBUG` / `#endif` block, before `#else /* !__ASSEMBLY__ */`.

### 0150-net-dovetail-add-infrastructure-for-oob-device-I-O.patch

**Error type**: `sha1 information is lacking or useless (include/linux/netdevice.h)`

**Files affected**:
- `include/linux/netdevice.h` — clean (offset 9/32 lines)
- `net/core/page_pool.c` — 1 hunk rejected
- All other files clean

**Problem**: QCOM tree changed `ring_qsize = pool->p.pool_size` to `ring_qsize = min(pool->p.pool_size, 16384)`. Patch expects the original line to insert oob pool size check after.

**Resolution**: Added the `else if (page_pool_is_oob(pool))` check after the QCOM-specific `ring_qsize = min(...)` line.

## Summary

- 3 patches needed manual intervention out of 171
- All conflicts were context-based (QCOM code differences, not semantic conflicts)
- No ARM64-specific conflicts encountered (QCOM's arm64 code was compatible with dovetail patches)
- 3-way merge (`--3way`) auto-resolved many patches, especially in `net/core/`, `net/socket.c`, and `security/selinux/hooks.c`
