# CLR ABI

This document describes the .NET Common Language Runtime (CLR) software conventions (or ABI, "Application Binary Interface"). It focuses on the ABI for the x64 (aka, AMD64), ARM (aka, ARM32 or Thumb-2), and ARM64 processor architectures. Documentation for the x86 ABI is somewhat scant, but information on the basics of the calling convention is included at the bottom of this document.

It describes requirements that the Just-In-Time (JIT) compiler imposes on the VM and vice-versa.

A note on the JIT codebases: JIT32 refers to the original JIT codebase that originally generated x86 code and was later ported to generate ARM code. JIT64 refers to the legacy .NET Framework codebase that supports AMD64. The RyuJIT compiler evolved from JIT32, and now supports all platforms and architectures. See [this post](https://devblogs.microsoft.com/dotnet/the-ryujit-transition-is-complete) for more RyuJIT history.

NativeAOT refers to a runtime that is optimized for ahead-of-time compilation (AOT). The NativeAOT ABI differs in a few details for simplicity and consistency across platforms.

# Getting started

Read everything in the documented Windows and non-Windows ABI documentation. The CLR follows those basic conventions. This document only describes things that are CLR-specific, or exceptions from those documents.

## Windows ABI documentation

AMD64: See [x64 Software Conventions](https://learn.microsoft.com/cpp/build/x64-software-conventions).

ARM: See [Overview of ARM32 ABI Conventions](https://learn.microsoft.com/cpp/build/overview-of-arm-abi-conventions).

ARM64: See [Overview of ARM64 ABI conventions](https://learn.microsoft.com/cpp/build/arm64-windows-abi-conventions).

## Non-Windows ABI documentation

Arm corporation ABI documentation (for ARM32 and ARM64) is [here](https://developer.arm.com/architectures/system-architectures/software-standards/abi) and [here](https://github.com/ARM-software/abi-aa).
Apple's ARM64 calling convention differences can be found [here](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms).

The Linux System V x86_64 ABI is documented in [System V Application Binary Interface / AMD64 Architecture Processor Supplement](https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf), with document source material [here](https://gitlab.com/x86-psABIs/x86-64-ABI).

The LoongArch64 ABI documentation is [here](https://github.com/loongson/LoongArch-Documentation/blob/main/docs/LoongArch-ELF-ABI-EN.adoc)

The RISC-V ABIs Specification: [latest release](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/releases/latest), [latest draft](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/releases), [document source repo](https://github.com/riscv-non-isa/riscv-elf-psabi-doc).

# General Unwind/Frame Layout

For all non-x86 platforms, all methods must have unwind information so the garbage collector (GC) can unwind them (unlike native code in which a leaf method may be omitted).

ARM and ARM64: Managed methods must always push LR on the stack, and create a minimal frame, so that the method can be properly hijacked using return address hijacking.

## Frame pointer chains

A frame pointer chain exists when the frame pointer register points to a location on the stack containing the address of the saved previous frame pointer value (from a caller of the current function). This chaining is required is certain scenarios, such as:
1. gdb debugger stack walking on Linux.
2. ETW event trace stack walking.

There are two considerations:
1. Reserving the frame pointer register for stack walking, and not using it for other purposes, such as general-purpose code generation, and
2. Creating a frame chain.

Note that even if a function is not added to the frame chain, as long as the function does not modify the frame pointer, the existing frame chain is still viable, although that function will not appear when walking the chain. The JIT may have reasons to create and use a frame pointer register even if a frame chain is not created, such as to access main function local variables within an exception handling funclet.

The frame pointer register is, for each architecture: ARM: r11, ARM64: x29, x86: EBP, x64: RBP.

The JIT creates frame chains most of the time for all platforms _except_ Windows x64. Very simple functions may not get added to the frame chain, with the intent to improve performance by reducing frame setup cost (the heuristics for this choice are in `Compiler::rpMustCreateEBPFrame()`). For Windows x64, unwinding will always be done using the generated unwind codes, and not simple frame chain traversal.

Some additional links:
- See [ARM64 JIT frame layout](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/arm64-jit-frame-layout.md) for documentation on that architecture's frame design.
- The CoreCLR change to always create RBP chains on Unix x64 is [here](https://github.com/dotnet/coreclr/pull/4019) (issue with discussion [here](https://github.com/dotnet/runtime/issues/4651)).

# Special/extra parameters

## The `this` pointer

The managed `this` pointer is treated like a new kind of argument not covered by the native ABI, so we chose to always pass it as the first argument in (AMD64) `RCX` or (ARM, ARM64) `R0`.

AMD64-only: Up to .NET Framework 4.5, the managed `this` pointer was treated just like the native `this` pointer (meaning it was the second argument when the call used a return buffer and was passed in RDX instead of RCX). Starting with .NET Framework 4.5, it is always the first argument.

## Varargs

Varargs refers to passing or receiving a variable number of arguments for a call.

C# varargs, using the `params` keyword, are at the IL level just normal calls with a fixed number of parameters.

Managed varargs (using C#'s pseudo-documented "...", `__arglist`, etc.) are implemented almost exactly like C++ varargs. The biggest difference is that the JIT adds a "vararg cookie" after the optional return buffer and the optional `this` pointer, but before any other user arguments. The callee must spill this cookie and all subsequent arguments into their home location, as they may be addressed via pointer arithmetic starting with the cookie as a base. The cookie happens to be to a pointer to a signature that the runtime can parse to (1) report any GC pointers within the variable portion of the arguments or (2) type-check (and properly walk over) any arguments extracted via ArgIterator. This is marked by `IMAGE_CEE_CS_CALLCONV_VARARG`, which should not be confused with `IMAGE_CEE_CS_CALLCONV_NATIVEVARARG`, which really is exactly native varargs (no cookie) and should only appear in PInvoke IL stubs, which properly handle pinning and other GC magic.

On AMD64, just like native, any floating point arguments passed in floating point registers (including the fixed arguments) will be shadowed (i.e. duplicated) in the integer registers.

On ARM and ARM64, just like native, nothing is put in the floating point registers.

However, unlike native varargs, all floating point arguments are not promoted to double (`R8`), and instead retain their original type (`R4` or `R8`) (although this does not preclude an IL generator like managed C++ from explicitly injecting an upcast at the call-site and adjusting the call-site-sig appropriately). This leads to unexpected behavior when native C++ is ported to C# or even just managed via the different flavors of managed C++.

Managed varargs are supported on Windows only.

Managed/native varargs are supported on Windows only. Support for managed/native varargs on non-Windows platforms is tracked by [this issue](https://github.com/dotnet/runtime/issues/82081).

## Generics

*Shared generics*. In cases where the code address does not uniquely identify a generic instantiation of a method, then a 'generic instantiation parameter' is required. Often the `this` pointer can serve dual-purpose as the instantiation parameter. When the `this` pointer is not the generic parameter, the generic parameter is passed as an additional argument. On ARM and AMD64, it is passed after the optional return buffer and the optional `this` pointer, but before any user arguments. On ARM64 and RISC-V, the generic parameter is passed after the optional `this` pointer, but before any user arguments. On x86, if all arguments of the function including `this` pointer fit into argument registers (ECX and EDX) and we still have argument registers available, we store the hidden argument in the next available argument register. Otherwise it is passed as the last stack argument. For generic methods (where there is a type parameter directly on the method, as compared to the type), the generic parameter currently is a MethodDesc pointer (I believe an InstantiatedMethodDesc). For static methods (where there is no `this` pointer) the generic parameter is a MethodTable pointer/TypeHandle.

Sometimes the VM asks the JIT to report and keep alive the generics parameter. In this case, it must be saved on the stack someplace and kept alive via normal GC reporting (if it was the `this` pointer, as compared to a MethodDesc or MethodTable) for the entire method except the prolog and epilog. Also note that the code to home it, must be in the range of code reported as the prolog in the GC info (which probably isn't the same as the range of code reported as the prolog in the unwind info).

There is no defined/enforced/declared ordering between the generic parameter and the varargs cookie because the runtime does not support that combination. There are chunks of code in the VM and JITs that would appear to support that, but other places assert and disallow it, so nothing is tested, and I would assume there are bugs and differences (i.e. one JIT using a different ordering than the other JIT or the VM).

### Example
```
call(["this" pointer] [return buffer pointer] [generics context|varargs cookie] [userargs]*)
```

## Async

Async calling convention is additive to other calling conventions when supported. The set of scenarios is constrained to regular static/virtual calls and does not, for example, support PInvokes or varargs. At the minimum ordinary static calls, calls with `this` parameter or generic hidden parameters are supported.

Async calling convention adds an extra `Continuation` parameter and an extra return, which sematically takes precedence when not `null`. A non-null `Continuation` upon return signals that the computation is not complete and the formal result is not ready. A non-null argument means that the function is resuming and should extract the state from the `Continuation` and continue execution (while ignoring all other arguments).

The `Continuation` is a managed object and needs to be tracked accordingly. The GC info includes the continuation result as live at Async call sites.

### Returning `Continuation`
To return `Continuation` we use a volatile/calee-trash register that cannot be used to return the actual result.

| arch | `REG_ASYNC_CONTINUATION_RET` |
| ------------- | ------------- |
| x86  | ecx  |
| x64  | rcx  |
| arm | r2  |
| arm64  | x2  |
| risc-v  | a2  |

### Passing `Continuation` argument
The `Continuation` parameter is passed at the same position as generic instantiation parameter or immediately after, if both present. For x86 the argument order is reversed.

```
call(["this" pointer] [return buffer pointer] [generics context] [continuation] [userargs])   // not x86

call(["this" pointer] [return buffer pointer] [userargs] [continuation] [generics context])   // x86
```

## AMD64-only: by-value value types

Just like native, AMD64 has implicit-byrefs. Any structure (value type in IL parlance) that is not 1, 2, 4, or 8 bytes in size (i.e., 3, 5, 6, 7, or >= 9 bytes in size) that is declared to be passed by value, is instead passed by reference. For JIT generated code, it follows the native ABI where the passed-in reference is a pointer to a compiler generated temp local on the stack. However, there are some cases within remoting or reflection where apparently stackalloc is too hard, and so they pass in pointers within the GC heap, thus the JITed code must report these implicit byref parameters as interior pointers (BYREFs in JIT parlance), in case the callee is one of these reflection paths. Similarly, all writes must use checked write barriers.

The AMD64 native calling conventions (Windows 64 and System V) require return buffer address to be returned by callee in RAX. JIT also follows this rule.

## RISC-V only: structs passed/returned according to hardware floating-point calling convention

Passing/returning structs according to hardware floating-point calling convention like native is currently [supported only up to 16 bytes](https://github.com/dotnet/runtime/issues/107386), ones larger than that differ from the standard ABI and are passed/returned according to integer calling convention (by implicit reference).

## Return buffers

Since .NET 10, return buffers must always be allocated on the stack by the caller. After the call, the caller is responsible for copying the return buffer to the final destination using write barriers if necessary. The JIT can assume that the return buffer is always on the stack and may optimize accordingly, such as by omitting write barriers when writing GC pointers to the return buffer. In addition, the buffer is allowed to be used for temporary storage within the method since its content must not be aliased or cross-thread visible.

ARM64-only: When a method returns a structure that is larger than 16 bytes the caller reserves a return buffer of sufficient size and alignment to hold the result. The address of the buffer is passed as an argument to the method in `R8` (defined in the JIT as `REG_ARG_RET_BUFF`). The callee isn't required to preserve the value stored in `R8`.

## Hidden parameters

*Stub dispatch* - when a virtual call uses a VSD stub, rather than back-patching the calling code (or disassembling it), the JIT must place the address of the stub used to load the call target, the "stub indirection cell", in (x86) `EAX` / (AMD64) `R11` / (ARM) `R4` / (ARM NativeAOT ABI) `R12` / (ARM64) `R11`. In the JIT, this is encapsulated in the `VirtualStubParamInfo` class.

*Calli Pinvoke* - The VM wants the address of the PInvoke in (AMD64) `R10` / (ARM) `R12` / (ARM64) `R14` (In the JIT: `REG_PINVOKE_TARGET_PARAM`), and the signature (the pinvoke cookie) in (AMD64) `R11` / (ARM) `R4` / (ARM64) `R15` (in the JIT: `REG_PINVOKE_COOKIE_PARAM`).

*Normal PInvoke* - The VM shares IL stubs based on signatures, but wants the right method to show up in call stack and exceptions, so the MethodDesc for the exact PInvoke is passed in the (x86) `EAX` / (AMD64) `R10` / (ARM, ARM64) `R12` (in the JIT: `REG_SECRET_STUB_PARAM`). Then in the IL stub, when the JIT gets `CORJIT_FLG_PUBLISH_SECRET_PARAM`, it must move the register into a compiler temp. The value is returned for the intrinsic `NI_System_StubHelpers_GetStubContext`.

## Small primitive returns

Primitive value types smaller than 32-bits are widened to 32-bits: signed small types are sign extended and unsigned small types are zero extended. This can be different from the standard calling conventions that may leave the state of unused bits in the return register undefined.

## Small primitive arguments

Small primitive arguments have undefined upper bits. This can be different from the standard calling conventions that may require normalization (e.g. on ARM32 and Apple ARM64).

On RISC-V small primitive arguments are extended according to standard calling conventions.

# PInvokes

The convention is that any method with an InlinedCallFrame (either an IL stub or a normal method with an inlined PInvoke) saves/restores all non-volatile integer registers in its prolog/epilog respectively. This is done so that the InlinedCallFrame can just contain a return address, a stack pointer and a frame pointer. Then using just those three it can start a full stack walk using the normal RtlVirtualUnwind.

When encountering a PInvoke, the JIT will query the VM if the GC transition should be suppressed. Suppression of the GC transition is indicated by the addition of an attribute on the PInvoke definition. If the VM indicates the GC transition is to be suppressed, the PInvoke frame will be omitted in either the IL stub or inlined scenario and a GC Poll will be inserted near the unmanaged call site. If an enclosing function contains more than one inlined PInvoke but not all have requested a suppression of the GC transition a PInvoke frame will still be constructed for the other inlined PInvokes.

For AMD64, a method with an InlinedCallFrame must use RBP as the frame register.

For ARM and ARM64, we will also always use a frame pointer (R11). That is partially due to the frame chaining requirement. However, the VM also requires it for PInvokes with InlinedCallFrames.

For ARM, the VM also has a dependency on `REG_SAVED_LOCALLOC_SP`.

All these dependencies show up in the implementation of `InlinedCallFrame::UpdateRegDisplay`.

JIT32 only generates one epilog (and causes all returns to branch to it) when there are PInvokes/InlinedCallFrame in the current method.

## Per-frame PInvoke initialization

The InlinedCallFrame is initialized once at the head of IL stubs and once in each path that does an inlined PInvoke.

In JIT64 this happens in blocks that actually contain calls, but pushing it out of loops that have landing pads, and then looking for dominator blocks. For IL stubs and methods with EH, we give up and place the initialization in the first block.

In RyuJIT/JIT32 (ARM), all methods are treated like JIT64's IL stubs (meaning the per-frame initialization happens once just after the prolog).

The JIT generates a call to `CORINFO_HELP_INIT_PINVOKE_FRAME` passing the address of the InlinedCallFrame and either NULL or the secret parameter for IL stubs. `JIT_InitPInvokeFrame` initializes the InlinedCallFrame and sets it to point to the current Frame chain top. Then it returns the current thread's native Thread object.

On AMD64, the JIT generates code to save RSP and RBP into the InlinedCallFrame.

For IL stubs only, the per-frame initialization includes setting `Thread->m_pFrame` to the InlinedCallFrame (effectively 'pushing' the Frame).

## Per-call-site PInvoke work

The below is performed when the GC transition is not suppressed.

1. For direct calls, the JITed code sets `InlinedCallFrame->m_pDatum` to the MethodDesc of the call target.
    * For JIT64, indirect calls within IL stubs sets it to the secret parameter (this seems redundant, but it might have changed since the per-frame initialization?).
    * For JIT32 (ARM) indirect calls, it sets this member to the size of the pushed arguments, according to the comments. The implementation however always passed 0.
2. For JIT64/AMD64 only: Next for non-IL stubs, the InlinedCallFrame is 'pushed' by setting `Thread->m_pFrame` to point to the InlinedCallFrame (recall that the per-frame initialization already set `InlinedCallFrame->m_pNext` to point to the previous top). For IL stubs this step is accomplished in the per-frame initialization.
3. The Frame is made active by setting `InlinedCallFrame->m_pCallerReturnAddress`.
4. The code then toggles the GC mode by setting `Thread->m_fPreemptiveGCDisabled = 0`.
5. Starting now, no GC pointers may be live in registers. RyuJit LSRA meets this requirement by adding special refPosition `RefTypeKillGCRefs` before unmanaged calls and special helpers.
6. Then comes the actual call/PInvoke.
7. The GC mode is set back by setting `Thread->m_fPreemptiveGCDisabled = 1`.
8. Then we check to see if `g_TrapReturningThreads` is set (non-zero). If it is, we call `CORINFO_HELP_STOP_FOR_GC`.
    * For ARM, this helper call preserves the return register(s): `R0`, `R1`, `S0`, and `D0`.
    * For AMD64, the generated code must manually preserve the return value of the PInvoke by moving it to a non-volatile register or a stack location.
9. Starting now, GC pointers may once again be live in registers.
10. Clear the `InlinedCallFrame->m_pCallerReturnAddress` back to 0.
11. For JIT64/AMD64 only: For non-IL stubs 'pop' the Frame chain by resetting `Thread->m_pFrame` back to `InlinedCallFrame.m_pNext`.

Saving/restoring all the non-volatile registers helps by preventing any registers that are unused in the current frame from accidentally having a live GC pointer value from a parent frame. The argument and return registers are 'safe' because they cannot be GC refs. Any refs should have been pinned elsewhere and instead passed as native pointers.

For IL stubs, the Frame chain isn't popped at the call site, so instead it must be popped right before the epilog and right before any jmp calls. It looks like we do not support tail calls from PInvoke IL stubs?

# Exception handling

This section describes the conventions the JIT needs to follow when generating code to implement managed exception handling (EH). The JIT and VM must agree on these conventions for a correct implementation.

## Funclets

For all platforms, managed EH handlers (finally, fault, filter, filter-handler, and catch) are extracted into their own 'funclets'. To the OS they are treated just like first class functions (separate PDATA and XDATA (`RUNTIME_FUNCTION` entry), etc.). The CLR currently treats them just like part of the parent function in many ways. The main function and all funclets must be allocated in a single code allocation (see hot cold splitting). They 'share' GC info. Only the main function prolog can be hot patched.

The only way to enter a handler funclet is via a call. In the case of an exception, the call is from the VM's EH subsystem as part of exception dispatch/unwind. In the non-exceptional case, this is called local unwind or a non-local exit. In C# this is accomplished by simply falling-through/out of a try body or an explicit goto. In IL this is always accomplished via a LEAVE opcode, within a try body, targeting an IL offset outside the try body. In such cases the call is from the JITed code of the parent function.

## Cloned finallys

RyuJIT attempts to speed the normal control flow by 'inlining' a called finally along the 'normal' control flow (i.e., leaving a try body in a non-exceptional manner via C# fall-through). This optimization is supported on all architectures.

## Invoking Finallys/Non-local exits

In order to have proper forward progress and `Thread.Abort` semantics, there are restrictions on where a call-to-finally can be, and what the call site must look like. The return address can **NOT** be in the corresponding try body (otherwise the VM would think the finally protects itself). The return address **MUST** be within any outer protected region (so exceptions from the finally body are properly handled).

RyuJIT creates something similar to a jump island: a block of code outside the try body that calls the finally and then branches to the final target of the leave/non-local-exit. This jump island is then marked in the EH tables as if it were a cloned finally. The cloned finally clause prevents a Thread.Abort from firing before entering the handler. By having the return address outside of the try body we satisfy the other constraint.

## ThreadAbortException considerations

There are three kinds of thread abort: (1) rude thread abort, that cannot be stopped, and doesn't run (all?) handlers, (2) calls to the `Thread.Abort()` api, and (3) asynchronous thread abort, injected from another thread.

Note that ThreadAbortException is fully available in the desktop framework, and is heavily used in ASP.NET, for example. However, it is not supported in .NET Core, CoreCLR, or the Windows 8 "modern app profile". Nonetheless, the JIT generates ThreadAbort-compatible code on all platforms.

For non-rude thread abort, the VM walks the stack, running any catch handler that catches ThreadAbortException (or a parent, like System.Exception, or System.Object), and running finallys. There is one very particular characteristic of ThreadAbortException: if a catch handler has caught ThreadAbortException, and the handler returns from handling the exception without calling Thread.ResetAbort(), then the VM *automatically re-raises ThreadAbortException*. To do so, it uses the resume address that the catch handler returned as the effective address where the re-raise is considered to have been raised. This is the address of the label that is specified by a LEAVE opcode within the catch handler. There are cases where the JIT must insert synthetic "step blocks" such that this label is within an appropriate enclosing "try" region, to ensure that the re-raise can be caught by an enclosing catch handler.

For example:

```cs
try { // try 1
    try { // try 2
        System.Threading.Thread.CurrentThread.Abort();
    } catch (System.Threading.ThreadAbortException) { // catch 2
        ...
        LEAVE L;
    }
} catch (System.Exception) { // catch 1
     ...
}
L:
```

In this case, if the address returned in catch 2 corresponding to label L is outside try 1, then the ThreadAbortException re-raised by the VM will not be caught by catch 1, as is expected. The JIT needs to insert a block such that this is the effective code generation:

```cs
try { // try 1
    try { // try 2
        System.Threading.Thread.CurrentThread.Abort();
    } catch (System.Threading.ThreadAbortException) { // catch 2
        ...
        LEAVE L';
    }
    L': LEAVE L;
} catch (System.Exception) { // catch 1
     ...
}
L:
```

Similarly, the automatic re-raise address for a ThreadAbortException can't be within a finally handler, or the VM will abort the re-raise and swallow the exception. This can happen due to call-to-finally thunks marked as "cloned finally", as described above. For example (this is pseudo-assembly-code, not C#):

```cs
try { // try 1
    try { // try 2
        System.Threading.Thread.CurrentThread.Abort();
    } catch (System.Threading.ThreadAbortException) { // catch 2
        ...
        LEAVE L;
    }
} finally { // finally 1
     ...
}
L:
```

This would generate something like:

```asm
	// beginning of 'try 1'
	// beginning of 'try 2'
	System.Threading.Thread.CurrentThread.Abort();
	// end of 'try 2'
	// beginning of call-to-finally 'cloned finally' region
L1:	call finally1
	nop
	// end of call-to-finally 'cloned finally' region
	// end of 'try 1'
	// function epilog
	ret

Catch2:
	// do something
	lea rax, &L1; // load up resume address
	ret

Finally1:
	// do something
	ret
```

Note that the JIT must already insert a "step" block so the finally will be called. However, this isn't sufficient to support ThreadAbortException processing, because "L1" is marked as "cloned finally". In this case, the JIT must insert another step block that is within "try 1" but outside the cloned finally block, that will allow for correct re-raise semantics. For example:

```asm
	// beginning of 'try 1'
	// beginning of 'try 2'
	System.Threading.Thread.CurrentThread.Abort();
	// end of 'try 2'
L1':	nop
	// beginning of call-to-finally 'cloned finally' region
L1:	call finally1
	nop
	// end of call-to-finally 'cloned finally' region
	// end of 'try 1'
	// function epilog
	ret

Catch2:
	// do something
	lea rax, &L1'; // load up resume address
	ret

Finally1:
	// do something
	ret
```

Note that JIT64 does not implement this properly. The C# compiler used to always insert all necessary "step" blocks. The Roslyn C# compiler at one point did not, but then was changed to once again insert them.

## Funclet parameters

Catch, Filter, and Filter-handlers get an Exception object (GC ref) as an argument (`REG_EXCEPTION_OBJECT`). On AMD64 it is passed in RCX (Windows ABI) or RSI (Unix ABI). On ARM and ARM64 this is the first argument and passed in R0.

## Funclet Return Values

The filter funclet returns a simple boolean value in the normal return register (x86: `EAX`, AMD64: `RAX`, ARM/ARM64: `R0`). Non-zero indicates to the VM/EH subsystem that the corresponding filter-handler will handle the exception (i.e. begin the second pass). Zero indicates to the VM/EH subsystem that the exception is **not** handled, and it should continue looking for another filter or catch.

The catch and filter-handler funclets return a code address in the normal return register that indicates where the VM should resume execution after unwinding the stack and cleaning up from the exception. This address should be somewhere in the parent funclet (or main function if the catch or filter-handler is not nested within any other funclet). Because an IL 'leave' opcode can exit out of arbitrary nesting of funclets and try bodies, the JIT is often required to inject step blocks. These are intermediate branch target(s) that then branch to the next outermost target until the real target can be directly reached via the native ABI constraints. These step blocks can also invoke finallys (see *Invoking Finallys/Non-local exits*).

Finally and fault funclets do not have a return value.

## Register values and exception handling

Exception handling imposes certain restrictions on the usage of registers in functions with exception handling.

CoreCLR and "desktop" CLR behave the same way. Windows and non-Windows implementations of the CLR both follow these rules.

Some definitions:

*Non-volatile* (aka *callee-saved* or *preserved*) registers are those defined by the ABI that a function call preserves. Non-volatile registers include the frame pointer and the stack pointer, among others.

*Volatile* (aka *caller-saved* or *trashed*) registers are those defined by the ABI that a function call does not preserve, and thus might have a different value when the function returns.

### Registers on entry to a funclet

When an exception occurs, the VM is invoked to do some processing. If the exception is within a "try" region, it eventually calls a corresponding handler (which also includes calling filters). The exception location within a function might be where a "throw" instruction executes, the point of a processor exception like null pointer dereference or divide by zero, or the point of a call where the callee threw an exception but did not catch it.

The VM sets the frame register to be the same as the parent function. This allows the funclets to access local variables using frame-relative addresses.

For filter funclets, all other register values that existed at the exception point in the corresponding "try" region are trashed on entry to the funclet. That is, the only registers that have known values are those of the funclet parameters and the frame register.

For other funclets, all non-volatile registers are restored to their values at the exception point. The JIT codegen [does not take advantage of it currently](https://github.com/dotnet/runtime/pull/114630#issuecomment-2810210759).

### Registers on return from a funclet

When a funclet finishes execution, and the VM returns execution to the function (or an enclosing funclet, if there is EH clause nesting), the non-volatile registers are restored to the values they held at the exception point. Note that the volatile registers have been trashed.

Any register value changes made in the funclet are lost. If a funclet wants to make a variable change known to the main function (or the funclet that contains the "try" region), that variable change needs to be made to the shared main function stack frame. This not a fundamental limitation. If necessary, the runtime can be updated to preserve non-volatile register changes made in funclets.

Funclets are not required to preserve non-volatile registers.

# EH Info, GC Info, and Hot & Cold Splitting

All GC info offsets and EH info offsets treat the function and funclets as if it was one big method body. Thus all offsets are relative to the start of the main method. Funclets are assumed to always be at the end of (after) all of the main function code. Thus if the main function has any cold code, all funclets must be cold. Or conversely, if there is any hot funclet code, all of the main method must be hot.

## EH clause ordering

EH clauses must be sorted inner-to-outer, first-to-last based on IL offset of the try start/try end pair. The only exceptions are cloned finallys, which always appear at the end.

## How EH affects GC info/reporting

Because a main function body will **always** be on the stack when one of its funclets is on the stack, the GC info must be careful not to double-report. JIT64 accomplished this by having all named locals appear in the parent method frame, anything shared between the function and funclets was homed to the stack, and only the parent function reported stack locals (funclets might report local registers). JIT32 and RyuJIT (for AMD64, ARM, and ARM64) take the opposite direction. The leaf-most funclet is responsible for reporting everything that might be live out of a funclet (in the case of a filter, this might resume back in the original method body). This is accomplished with the GC header flag WantsReportOnlyLeaf (JIT32 and RyuJIT set it, JIT64 doesn't) and the VM tracking if it has already seen a funclet for a given frame. Once JIT64 is fully retired, we should be able to remove this flag from GC info.

There is one "corner case" in the VM implementation of WantsReportOnlyLeaf model that has implications for the code the JIT is allowed to generate. Consider this function with nested exception handling:

```cs
public void runtest() {
    try {
        try {
            throw new UserException3(ThreadId);	// 1
        }
        catch (UserException3 e){
            Console.WriteLine("Exception3 was caught");
            throw new UserException4(ThreadId);
        }
    }
    catch (UserException4 e) { // 2
        Console.WriteLine("Exception4 was caught");
    }
}
```

When the inner "throw new UserException4" is executed, the exception handling first pass finds that the outer catch handler will handle the exception. The exception handling second pass unwinds stack frames back to the "runtest" frame, and then executes the catch handler. There is a period of time during which the original catch handler ("catch (UserException3 e)") is no longer on the stack, but before the new catch handler is executed. During this time, a GC might occur. In this case, the VM needs to make sure to report GC roots properly for the "runtest" function. The inner catch has been unwound, so we can't report that. We don't want to report at "// 1", which is still on the stack, because that effectively is "going backwards" in execution, and doesn't properly represent what object references are live. We need to report live object references at the next location where execution will occur. This is the "// 2" location. However, we can't report the first location of the catch funclet, as that will be non-interruptible. The VM instead looks forward for the first interruptible point in that handler, and reports live references that the JIT reports for that location. This will be the first location after the handler prolog. There are several implications of this implementation for the JIT. It requires that:

1. Methods which have EH clauses are fully interruptible.
2. All catch funclets have an interruptible point immediately after the prolog.
3. The first interruptible point in the catch funclet reports the following live objects on the stack
    * Only objects that are shared with parent method i.e. no additional stack object which is live only in catch funclet and not live in parent method.
    * All shared objects which are referenced in catch funclet and any subsequent control flow are reported live.

## Filter GC semantics

Filters are invoked in the 1st pass of EH processing and as such execution might resume back at the faulting address, or in the filter-handler, or someplace else. Because the VM must allow GC's to occur during and after a filter invocation, but before the EH subsystem knows where it will resume, we need to keep everything alive at both the faulting address **and** within the filter. This is accomplished by 3 means: (1) the VM's stackwalker and GCInfoDecoder report as live both the filter frame and its corresponding parent frame, (2) the JIT encodes all stack slots that are live within the filter as being pinned, and (3) the JIT reports as live (and possible zero-initializes) anything live-out of the filter. Because of (1) it is likely that a stack variable that is live within the filter and the try body will be double reported. During the mark phase of the GC double reporting is not a problem. The problem only arises if the object is relocated: if the same location is reported twice, the GC will try to relocate the address stored at that location twice. Thus we prevent the object from being relocated by pinning it, which leads us to why we must do (2). (3) is done so that after the filter returns, we can still safely incur a GC before executing the filter-handler or any outer handler within the same frame. For the same reason, control must exit a filter region via its final block (in other words, a filter region must terminate with the instruction that leaves the filter region, and the program may not exit the filter region via other paths).

## Clauses covering the same try region

Several consecutive clauses may cover the same `try` block. A clause covering the same region as the previous one is marked by the `COR_ILEXCEPTION_CLAUSE_SAMETRY` flag. When exception ex1 is thrown while running handler for another exception ex2 and the exception ex2 escapes the ex1's handler frame, this enables the runtime to skip clauses that cover the same `try` block as the clause that handled the ex1.
This flag is used by the NativeAOT and also a new exception handling mechanism in CoreCLR. The NativeAOT doesn't store that flag in the encoded clause data, but rather injects a dummy clause between the clauses with same `try` block. CoreCLR keeps that flag as part of the runtime representation of the clause data. The current CoreCLR exception handling doesn't use it, but [a new exception handling mechanism](https://github.com/dotnet/runtime/issues/77568) that's being developed is taking advantage of it.

## GC Interruptibility and EH

The VM assumes that anytime a thread is stopped, it must be at a GC safe point, or the current frame is non-resumable (i.e. a throw that will never be caught in the same frame). Thus effectively all methods with EH must be fully interruptible (or at a minimum all try bodies). Currently the GC info appears to support mixing of partially interruptible and fully-interruptible regions within the same method, but no JIT uses this, so use at your own risk.

The debugger always wants to stop at GC safe points, and thus debuggable code should be fully interruptible to maximize the places where the debugger can safely stop. If the JIT creates non-interruptible regions within fully interruptible code, the code should ensure that each sequence point begins on an interruptible instruction.

AMD64/JIT64 only: The JIT will add an interruptible NOP if needed.

## Security Object

The security object is a GC pointer and must be reported as such, and kept alive the duration of the method.

## GS Cookie

The GS Cookie is not a GC object, but still needs to be reported. It can only have one lifetime due to how it is encoded/reported in the GC info. Since the GS Cookie ceases being valid once we pop the stack, the epilog cannot be part of the live range. Since we only get one live range that means there cannot be any code (except funclets) after the epilog in methods with a GS cookie.

## NOPs and other Padding

### AMD64 padding info

The unwind callbacks don't know if the current frame is a leaf or a return address. Consequently, the JIT must ensure that the return address of a call is in the same region as the call. Specifically, the JIT must add a NOP (or some other instruction) after any call that otherwise would directly precede the start of a try body, the end of a try body, or the end of a method.

The OS has an optimization in the unwinder such that if an unwind results in a PC being within (or at the start of) an epilog, it assumes that frame is unimportant and unwinds again. Since the CLR considers every frame important, it does not want this double-unwind behavior and requires the JIT to place a NOP (or other instruction) between any call and any epilog.

### ARM and ARM64 padding info

The OS unwinder uses the `RUNTIME_FUNCTION` extents to determine which function or funclet to unwind out of. The net result is that a call (bl opcode) to `IL_Throw` cannot be the last thing. So similar to AMD64 the JIT must inject an opcode (a breakpoint in this case) when the `bl IL_Throw` would otherwise be the last opcode of a function or funclet, the last opcode before the end of the hot section, or (this might be an x86-ism leaking into ARM) the last before a "special throw block".

# Profiler Hooks

If the JIT gets passed `CORJIT_FLG_PROF_ENTERLEAVE`, then the JIT might need to insert native entry/exit/tail call probes. To determine for sure, the JIT must call GetProfilingHandle. This API returns as out parameters, the true dynamic boolean indicating if the JIT should actually insert the probes and a parameter to pass to the callbacks (typed as void*), with an optional indirection (used for NGEN). This parameter is always the first argument to all of the call-outs (thus placed in the usual first argument register `RCX` (AMD64) or `R0` (ARM, ARM64)).

Outside of the prolog (in a GC interruptible location), the JIT injects a call to `CORINFO_HELP_PROF_FCN_ENTER`. For AMD64,  on Windows all argument registers will be homed into their caller-allocated stack locations (similar to varargs), on Unix all argument registers will be stored in the inner structure. For ARM and ARM64, all arguments are prespilled (again similar to varargs).

After computing the return value and storing it in the correct register, but before any epilog code (including before a possible GS cookie check), the JIT injects a call to `CORINFO_HELP_PROF_FCN_LEAVE`. For AMD64 this call must preserve the return register: `RAX` or `XMM0` on Windows and `RAX` and `RDX` or `XMM0` and `XMM1` on Unix. For ARM, the return value will be moved from `R0` to `R2` (if it was in `R0`), `R1`, `R2`, and `S0/D0` must be preserved by the callee (longs will be `R2`, `R1` - note the unusual ordering of the registers, floats in `S0`, doubles in `D0`, smaller integrals in `R2`).

TODO: describe ARM64 profile leave conventions.

Before the argument setup (but after any argument side-effects) for any tail calls or jump calls, the JIT injects a call to `CORINFO_HELP_PROF_FCN_TAILCALL`. Note that it is NOT called for self-recursive tail calls turned into loops.

For ARM tail calls, the JIT actually loads the outgoing arguments first, and then just before the profiler call-out, spills the argument in `R0` to another non-volatile register, makes the call (passing the callback parameter in `R0`), and then restores `R0`.

For AMD64, all probes receive a second parameter (passed in `RDX` according to the default argument rules) which is the address of the start of the arguments' home location (equivalent to the value of the caller's stack pointer).

TODO: describe ARM64 tail call convention.

On Linux/x86 the profiling hooks are declared with the ```__cdecl``` attribute.  In cdecl (which stands for C declaration), subroutine arguments are passed on the stack. Integer values and memory addresses are returned in the EAX register, floating point values in the ST0 x87 register. Registers EAX, ECX, and EDX are caller-saved, and the rest are callee-saved. The x87 floating point registers ST0 to ST7 must be empty (popped or freed) when calling a new function, and ST1 to ST7 must be empty on exiting a function. ST0 must also be empty when not used for returning a value. Returned values of managed-code are formed before the leave/tailcall profiling hooks, so they should be saved in these hooks and restored on returning from them. The instruction ```ret``` for assembler implementations of profiling hooks should be without a parameter.

JIT32 only generates one epilog (and causes all returns to branch to it) when there are profiler hooks.

# Synchronized Methods

JIT32/RyuJIT only generates one epilog (and causes all returns to branch to it) when a method is synchronized. See `Compiler::fgAddSyncMethodEnterExit()`. The user code is wrapped in a try/finally. Outside/before the try body, the code initializes a boolean to false. `CORINFO_HELP_MON_ENTER` or `CORINFO_HELP_MON_ENTER_STATIC` are called, passing the lock object (the `this` pointer for instance methods or the Type object for static methods) and the address of the boolean. If the lock is acquired, the boolean is set to true (as an 'atomic' operation in the sense that a Thread.Abort/EH/GC/etc. cannot interrupt the Thread when the boolean does not match the acquired state of the lock). JIT32/RyuJIT follows the exact same logic and arguments for placing the call to `CORINFO_HELP_MON_EXIT` /  `CORINFO_HELP_MON_EXIT_STATIC` in the finally.

# Rejit

For AMD64 to support profiler attach scenarios, the JIT can be required to ensure every generated method is hot patchable (see `CORJIT_FLG_PROF_REJIT_NOPS`). The way we do this is to ensure that the first 5 bytes of code are non-interruptible and there is no branch target within those bytes (includes calls/returns). Thus the VM can stop all threads (like for a GC) and safely replace those 5 bytes with a branch to a new version of the method (presumably instrumented by a profiler). The JIT adds NOPs or increases the size of the prolog reported in the GC info to accomplish these 2 requirements.

In a function with exception handling, only the main function is affected; the funclet prologs are not made hot patchable.

# Edit and Continue

Edit and Continue (EnC) is a special flavor of un-optimized code. The debugger has to be able to reliably remap a method state (instruction pointer and local variables) from original method code to edited method code. This puts constraints on the method stack layout performed by the JIT. The key constraint is that the addresses of the existing locals must stay the same after the edit. This constraint is required because the address of the local could have been stored in the method state.

In the current design, the JIT does not have access to the previous versions of the method and so it has to assume the worst case. EnC is designed for simplicity, not for performance of the generated code.

EnC is currently enabled on x86, x64 and ARM64 only, but the same principles would apply if it is ever enabled on other platforms.

The following sections describe the various Edit and Continue code conventions that must be followed.

## EnC flag in GCInfo

The JIT records the fact that it has followed conventions for EnC code in GC Info. On x64/ARM64, this flag is implied by recording the size of the stack frame region preserved between EnC edits (`GcInfoEncoder::SetSizeOfEditAndContinuePreservedArea`). For x64 the size of this region is increased to include `RSI` and `RDI`, so that `rep stos` can be used for block initialization and block moves. ARM64 saves only the FP and LR registers when EnC is enabled and does not use other callee saved registers.

To successfully perform EnC transitions the runtime needs to know the size of the stack frames it is transitioning between. For x64 code the size can be extracted from unwind codes. This is not possible for arm64 code as the frames are set up in such a way that the unwind codes do not allow to retrieve this value. Therefore, on ARM64 the GC info contains also the size of the fixed stack frame to be used for EnC purposes.

## Allocating local variables backward

This is required to preserve addresses of the existing locals when an EnC edit appends new ones. In other words, the first local must be allocated at the highest stack address. Special care has to be taken to deal with alignment. The total size of the method frame can either grow (more locals added) or shrink (fewer temps needed) after the edit. The VM zeros out newly added locals.

## Fixed set of callee-saved registers

This eliminates need to deal with the different sets in the VM, and makes preservation of local addresses easier. There are plenty of volatile registers and so lack of non-volatile registers does not heavily impact quality of non-optimized code.
x64 currently saves RBP, RSI and RDI while ARM64 saves just FP and LR.

## EnC is supported for methods with EH

However, EnC remap is not supported inside funclets. The stack layout of funclets does not matter for EnC.

## Localloc

Localloc is allowed in EnC code, but remap is disallowed after the method has executed a localloc instruction. VM uses the invariants above (`RSP == RBP` on x64, `FP + 16 == SP + stack size` on ARM64) to detect whether localloc was executed by the method.

## Security object

This does not require any special handling by the JIT on x64/arm64. (Different from x86). The security object is copied over by the VM during remap if necessary. Location of security object is found via GC info.

## Synchronized methods

The extra state created by the JIT for synchronized methods (lock taken flag) must be preserved during remap. The JIT stores this state in the preserved region, and increases the size of the preserved region reported in GC info accordingly.

## Generics

EnC is supported for adding and editing generic methods and methods on generic types and generic methods on non-generic types.

# System V x86_64 support

This section relates mostly to calling conventions on System V systems (such as Ubuntu Linux and Mac OS X).
The general rules outlined in the System V x86_64 ABI documentation are followed with a few exceptions, described below:

1. The hidden argument for by-value passed structs is always after the `this` parameter (if there is one). This is a difference with the System V ABI and affects only the internal JIT calling conventions. For PInvoke calls the hidden argument is always the first parameter since there is no `this` parameter in this case (except for the `CallConvMemberFunction` case).
2. Managed structs that have no fields are always passed by-value on the stack.
3. The JIT proactively generates frame register frames (with `RBP` as a frame register) in order to aid the native OS tooling for stack unwinding and the like.
4. All the other internal VM contracts for PInvoke, EH, and generic support remains in place. Please see the relevant sections above for more details. Note, however, that the registers used are different on System V due to the different calling convention. For example, the integer argument registers are, in order, RDI, RSI, RDX, RCX, R8, and R9. Thus, where the first argument (typically, the `this` pointer) on Windows AMD64 goes in RCX, on System V it goes in RDI, and so forth.
5. Structs with explicit layout are always passed by value on the stack.
6. The following table describes register usage according to the System V x86_64 ABI

```
| Register      | Usage                                   | Preserved across  |
|               |                                         | function calls    |
|---------------|-----------------------------------------|-------------------|
| %rax          | temporary register; with variable argu- | No                |
|               | ments passes information about the      |                   |
|               | number of SSE registers used;           |                   |
|               | 1st return argument                     |                   |
| %rbx          | callee-saved register; optionally used  | Yes               |
|               | as base pointer                         |                   |
| %rcx          | used to pass 4st integer argument to    | No                |
|               | to functions                            |                   |
| %rdx          | used to pass 3rd argument to functions  | No                |
|               | 2nd return register                     |                   |
| %rsp          | stack pointer                           | Yes               |
| %rbp          | callee-saved register; optionally used  | Yes               |
|               | as frame pointer                        |                   |
| %rsi          | used to pass 2nd argument to functions  | No                |
| %rdi          | used to pass 1st argument to functions  | No                |
| %r8           | used to pass 5th argument to functions  | No                |
| %r9           | used to pass 6th argument to functions  | No                |
| %r10          | temporary register, used for passing a  | No                |
|               | function's static chain pointer         |                   |
| %r11          | temporary register                      | No                |
| %r12-%r15     | callee-saved registers                  | Yes               |
| %xmm0-%xmm1   | used to pass and return floating point  | No                |
|               | arguments                               |                   |
| %xmm2-%xmm7   | used to pass floating point arguments   | No                |
| %xmm8-%xmm31  | temporary registers                     | No                |
```

# Calling convention specifics for x86

Unlike the other architectures that RyuJIT supports, the managed x86 calling convention is different than the default native calling convention. This is true for both Windows and Unix x86.

The standard managed calling convention is a variation on the Windows x86 fastcall convention. It differs primarily in the order in which arguments are pushed on the stack.

The only values that can be passed in registers are managed and unmanaged pointers, object references, and the built-in integer types int8, unsigned int8, int16, unsigned int16, int32, unsigned it32, native int, native unsigned int, and enums and value types with only one 4-byte integer primitive-type field. Enums are passed as their underlying type. All floating-point values and 8-byte integer values are passed on the stack. When the return type is a value type that cannot be passed in a register, the caller shall create a buffer to hold the result and pass the address of this buffer as a hidden parameter.

Arguments are passed in left-to-right order, starting with the `this` pointer (for instance and virtual methods), followed by the return buffer pointer if needed, followed by the user-specified argument values. The first of these that can be placed in a register is put into ECX, the next in EDX, and all subsequent ones are passed on the stack. This is in contrast with the x86 native calling conventions, which push arguments onto the stack in right-to-left order.

The return value is handled as follows:

1. Floating-point values are returned on the top of the hardware FP stack.
2. Integers up to 32 bits long are returned in EAX.
3. 64-bit integers are passed with EAX holding the least significant 32 bits and EDX holding the most significant 32 bits.
4. All other cases require the use of a return buffer, through which the value is returned. See [Return buffers](#return-buffers).

# Control Flow Guard (CFG) support on Windows

Control Flow Guard (CFG) is a security mitigation available in Windows.
When CFG is enabled, the operating system maintains data structures that can be used to verify whether an address is to be considered a valid indirect call target.
This mechanism is exposed through two different helper functions, each with different characteristics.

The first mechanism is a validator that takes the target address as an argument and fails fast if the address is not an expected indirect call target; otherwise, it does nothing and returns.
The second mechanism is a dispatcher that takes the target address in a non-standard register; on successful validation of the address, it jumps directly to the target function.
Windows makes the dispatcher available only on ARM64 and x64, while the validator is available on all platforms.
However, the JIT supports CFG only on ARM64 and x64, with CFG by default being disabled for these platforms.
The expected use of the CFG feature is for NativeAOT scenarios that are running in constrained environments where CFG is required.

The helpers are exposed to the JIT as standard JIT helpers `CORINFO_HELP_VALIDATE_INDIRECT_CALL` and `CORINFO_HELP_DISPATCH_INDIRECT_CALL`.

To use the validator the JIT expands indirect calls into a call to the validator followed by a call to the validated address.
For the dispatcher the JIT will transform calls to pass the target along but otherwise set up the call as normal.

Note that "indirect call" here refers to any call that is not to an immediate (in the instruction stream) address.
For example, even direct calls may emit indirect call instructions in JIT codegen due to e.g. tiering or if they have not been compiled yet; these are expanded with the CFG mechanism as well.

The next sections describe the calling convention that the JIT expects from these helpers.

## CFG details for ARM64

On ARM64, `CORINFO_HELP_VALIDATE_INDIRECT_CALL` takes the call address in `x15`.
In addition to the usual registers it preserves all float registers, `x0`-`x8` and `x15`.

`CORINFO_HELP_DISPATCH_INDIRECT_CALL` takes the call address in `x9`.
The JIT does not use the dispatch helper by default due to worse branch predictor performance.
Therefore it will expand all indirect calls via the validation helper and a manual call.

## CFG details for x64

On x64, `CORINFO_HELP_VALIDATE_INDIRECT_CALL` takes the call address in `rcx`.
In addition to the usual registers it also preserves all float registers, `rcx`, and `r10`; furthermore, shadow stack space is not required to be allocated.

`CORINFO_HELP_DISPATCH_INDIRECT_CALL` takes the call address in `rax` and it reserves the right to use and trash `r10` and `r11`.
The JIT uses the dispatch helper on x64 whenever possible as it is expected that the code size benefits outweighs the less accurate branch prediction.
However, note that the use of `r11` in the dispatcher makes it incompatible with VSD calls where the JIT must fall back to the validator and a manual call.

# Notes on Memset/Memcpy

Generally, `memset` and `memcpy` do not provide any guarantees of atomicity. This implies that they should only be used when the memory being modified by `memset`/`memcpy` is not observable by any other thread (including GC), or when there are no atomicity requirements according to our [Memory Model](../../specs/Memory-model.md). It's especially important when we modify heap containing managed pointers - those must be updated atomically, e.g. using pointer-sized `mov` instruction (managed pointers are always aligned) - see [Atomic Memory Access](../../specs/Memory-model.md#Atomic-memory-accesses). It's worth noting that by "update" it's implied "set to zero", otherwise, we need a write barrier.

Examples:

```cs
struct MyStruct
{
	long a;
	string b;
}

void Test1(ref MyStruct m)
{
	// We're not allowed to use memset here
	m = default;
}

MyStruct Test2()
{
	// We can use memset here
	return default;
}
```
