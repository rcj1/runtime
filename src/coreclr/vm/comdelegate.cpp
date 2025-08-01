// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
//
// File: COMDelegate.cpp
//

// This module contains the implementation of the native methods for the
// Delegate class.
//


#include "common.h"
#include "comdelegate.h"
#include "invokeutil.h"
#include "excep.h"
#include "class.h"
#include "field.h"
#include "dllimportcallback.h"
#include "dllimport.h"
#include "eeconfig.h"
#include "cgensys.h"
#include "asmconstants.h"
#include "virtualcallstub.h"
#include "typestring.h"
#ifdef FEATURE_COMINTEROP
#include "comcallablewrapper.h"
#endif // FEATURE_COMINTEROP

#define DELEGATE_MARKER_UNMANAGEDFPTR -1


#ifndef DACCESS_COMPILE

#if defined(TARGET_X86)

// Return an encoded shuffle entry describing a general register or stack offset that needs to be shuffled.
static UINT16 ShuffleOfs(INT ofs, UINT stackSizeDelta = 0)
{
    STANDARD_VM_CONTRACT;

    if (TransitionBlock::IsStackArgumentOffset(ofs))
    {
        ofs = (ofs - TransitionBlock::GetOffsetOfReturnAddress()) + stackSizeDelta;

        if (ofs >= ShuffleEntry::REGMASK)
        {
            // method takes too many stack args
            COMPlusThrow(kNotSupportedException);
        }
    }
    else
    {
        ofs -= TransitionBlock::GetOffsetOfArgumentRegisters();
        ofs |= ShuffleEntry::REGMASK;
    }

    return static_cast<UINT16>(ofs);
}
#endif

#ifdef FEATURE_PORTABLE_SHUFFLE_THUNKS

// Iterator for extracting shuffle entries for argument desribed by an ArgLocDesc.
// Used when calculating shuffle array entries in GenerateShuffleArray below.
class ShuffleIterator
{
    // Argument location description
    ArgLocDesc* m_argLocDesc;

#if defined(UNIX_AMD64_ABI)
    // Current eightByte used for struct arguments in registers
    int m_currentEightByte;
#endif
    // Current general purpose register index (relative to the ArgLocDesc::m_idxGenReg)
    int m_currentGenRegIndex;
    // Current floating point register index (relative to the ArgLocDesc::m_idxFloatReg)
    int m_currentFloatRegIndex;
    // Current byte stack index (relative to the ArgLocDesc::m_byteStackIndex)
    int m_currentByteStackIndex;

#if defined(UNIX_AMD64_ABI)
    // Get next shuffle offset for struct passed in registers. There has to be at least one offset left.
    UINT16 GetNextOfsInStruct()
    {
        EEClass* eeClass = m_argLocDesc->m_eeClass;
        _ASSERTE(eeClass != NULL);

        if (m_currentEightByte < eeClass->GetNumberEightBytes())
        {
            SystemVClassificationType eightByte = eeClass->GetEightByteClassification(m_currentEightByte);
            unsigned int eightByteSize = eeClass->GetEightByteSize(m_currentEightByte);

            m_currentEightByte++;

            int index;
            UINT16 mask = ShuffleEntry::REGMASK;

            if (eightByte == SystemVClassificationTypeSSE)
            {
                _ASSERTE(m_currentFloatRegIndex < m_argLocDesc->m_cFloatReg);
                index = m_argLocDesc->m_idxFloatReg + m_currentFloatRegIndex;
                m_currentFloatRegIndex++;

                mask |= ShuffleEntry::FPREGMASK;
                if (eightByteSize == 4)
                {
                    mask |= ShuffleEntry::FPSINGLEMASK;
                }
            }
            else
            {
                _ASSERTE(m_currentGenRegIndex < m_argLocDesc->m_cGenReg);
                index = m_argLocDesc->m_idxGenReg + m_currentGenRegIndex;
                m_currentGenRegIndex++;
            }

            return (UINT16)index | mask;
        }

        // There are no more offsets to get, the caller should not have called us
        _ASSERTE(false);
        return 0;
    }
#endif // UNIX_AMD64_ABI

public:

    // Construct the iterator for the ArgLocDesc
    ShuffleIterator(ArgLocDesc* argLocDesc)
    :
        m_argLocDesc(argLocDesc),
#if defined(UNIX_AMD64_ABI)
        m_currentEightByte(0),
#endif
        m_currentGenRegIndex(0),
        m_currentFloatRegIndex(0),
        m_currentByteStackIndex(0)
    {
    }

    // Check if there are more offsets to shuffle
    bool HasNextOfs()
    {
        return (m_currentGenRegIndex < m_argLocDesc->m_cGenReg) ||
               (m_currentFloatRegIndex < m_argLocDesc->m_cFloatReg) ||
               (m_currentByteStackIndex < m_argLocDesc->m_byteStackSize);
    }

    // Get next offset to shuffle. There has to be at least one offset left.
    // It returns an offset encoded properly for a ShuffleEntry offset.
    // - For floating register arguments it returns regNum | ShuffleEntry::REGMASK | ShuffleEntry::FPREGMASK.
    // - For register arguments it returns regNum | ShuffleEntry::REGMASK.
    // - For stack arguments it returns stack offset index in stack slots for most architectures. For macOS-arm64,
    //     it returns an encoded stack offset, see below.
    int GetNextOfs()
    {
        int index;

#if defined(UNIX_AMD64_ABI)

        // Check if the argLocDesc is for a struct in registers
        EEClass* eeClass = m_argLocDesc->m_eeClass;
        if (m_argLocDesc->m_eeClass != 0)
        {
            index = GetNextOfsInStruct();
            _ASSERT((index & ShuffleEntry::REGMASK) != 0);
            return index;
        }
#endif // UNIX_AMD64_ABI

        // Shuffle float registers first
        if (m_currentFloatRegIndex < m_argLocDesc->m_cFloatReg)
        {
#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
            if ((m_argLocDesc->m_structFields.flags & FpStruct::IntFloat) && (m_currentGenRegIndex < m_argLocDesc->m_cGenReg))
            {
                // the first field is integer so just skip this.
            }
            else
#endif
            {
                index = m_argLocDesc->m_idxFloatReg + m_currentFloatRegIndex;
                m_currentFloatRegIndex++;

                return index | ShuffleEntry::REGMASK | ShuffleEntry::FPREGMASK;
            }
        }

        // Shuffle any registers first (the order matters since otherwise we could end up shuffling a stack slot
        // over a register we later need to shuffle down as well).
        if (m_currentGenRegIndex < m_argLocDesc->m_cGenReg)
        {
#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
            if (7 < (m_currentGenRegIndex + m_argLocDesc->m_idxGenReg))
            {
                m_currentGenRegIndex++;
                index = m_currentByteStackIndex;
                m_currentByteStackIndex += TARGET_POINTER_SIZE;
                return index;
            }
#endif
            index = m_argLocDesc->m_idxGenReg + m_currentGenRegIndex;
            m_currentGenRegIndex++;

            return index | ShuffleEntry::REGMASK;
        }

        // If we get here we must have at least one stack slot left to shuffle (this method should only be called
        // when AnythingToShuffle(pArg) == true).
        if (m_currentByteStackIndex < m_argLocDesc->m_byteStackSize)
        {
            const unsigned byteIndex = m_argLocDesc->m_byteStackIndex + m_currentByteStackIndex;

#if !defined(TARGET_APPLE) || !defined(TARGET_ARM64)
            index = byteIndex / TARGET_POINTER_SIZE;
            m_currentByteStackIndex += TARGET_POINTER_SIZE;

            // Delegates cannot handle overly large argument stacks due to shuffle entry encoding limitations.
            if (index >= ShuffleEntry::REGMASK)
            {
                COMPlusThrow(kNotSupportedException);
            }

            // Only Apple Silicon ABI currently supports unaligned stack argument shuffling
            _ASSERTE(byteIndex == unsigned(index * TARGET_POINTER_SIZE));
            return index;
#else
            // Tha Apple Silicon ABI does not consume an entire stack slot for every argument
            // Arguments smaller than TARGET_POINTER_SIZE are always aligned to their argument size
            // But may not begin at the beginning of a stack slot
            //
            // The argument location description has been updated to describe the stack offest and
            // size in bytes.  We will use it as our source of truth.
            //
            // The ShuffleEntries will be implemented by the Arm64 StubLinkerCPU::EmitLoadStoreRegImm
            // using the 12-bit scaled immediate stack offset. The load/stores can be implemented as 1/2/4/8
            // bytes each (natural binary sizes).
            //
            // Each offset is encode as a log2 size and a 12-bit unsigned scaled offset.
            // We only emit offsets of these natural binary sizes
            //
            // We choose the offset based on the ABI stack alignment requirements
            // - Small integers are shuffled based on their size
            // - HFA are shuffled based on their element size
            // - Others are shuffled in full 8 byte chunks.
            int bytesRemaining = m_argLocDesc->m_byteStackSize - m_currentByteStackIndex;
            int log2Size = 3;

            // If isHFA, shuffle based on field size
            // otherwise shuffle based on stack size
            switch(m_argLocDesc->m_hfaFieldSize ? m_argLocDesc->m_hfaFieldSize : m_argLocDesc->m_byteStackSize)
            {
                case 1:
                    log2Size = 0;
                    break;
                case 2:
                    log2Size = 1;
                    break;
                case 4:
                    log2Size = 2;
                    break;
                case 3: // Unsupported Size
                case 5: // Unsupported Size
                case 6: // Unsupported Size
                case 7: // Unsupported Size
                    _ASSERTE(false);
                    break;
                default: // Should be a multiple of 8 (TARGET_POINTER_SIZE)
                    _ASSERTE(bytesRemaining >= TARGET_POINTER_SIZE);
                    break;
            }

            m_currentByteStackIndex += (1 << log2Size);

            // Delegates cannot handle overly large argument stacks due to shuffle entry encoding limitations.
            // Arm64 current implementation only supports 12 bit unsigned scaled offset
            if ((byteIndex >> log2Size) > 0xfff)
            {
                COMPlusThrow(kNotSupportedException);
            }

            _ASSERTE((byteIndex & ((1 << log2Size) - 1)) == 0);

            return (byteIndex >> log2Size) | (log2Size << 12);
#endif
        }

        // There are no more offsets to get, the caller should not have called us
        _ASSERTE(false);
        return 0;
    }
};


// Return an index of argument slot. First indices are reserved for general purpose registers,
// the following ones for float registers and then the rest for stack slots.
// This index is independent of how many registers are actually used to pass arguments.
static UINT16 GetNormalizedArgumentSlotIndex(UINT16 offset)
{
    UINT16 index;

    if (offset & ShuffleEntry::FPREGMASK)
    {
        index = NUM_ARGUMENT_REGISTERS + (offset & ShuffleEntry::OFSREGMASK);
    }
    else if (offset & ShuffleEntry::REGMASK)
    {
        index = offset & ShuffleEntry::OFSREGMASK;
    }
    else
    {
        // stack slot
        index = NUM_ARGUMENT_REGISTERS
#ifdef NUM_FLOAT_ARGUMENT_REGISTERS
                + NUM_FLOAT_ARGUMENT_REGISTERS
#endif
                + (offset & ShuffleEntry::OFSMASK);
    }

    return index;
}

// Node of a directed graph where nodes represent registers / stack slots
// and edges represent moves of data.
struct ShuffleGraphNode
{
    static const UINT16 NoNode = 0xffff;
    // Previous node (represents source of data for the register / stack of the current node)
    UINT16 prev;
    // Offset of the register / stack slot
    UINT16 ofs;
    // Set to true for nodes that are source of data for a destination node
    UINT8 isSource;
    // Nodes that are marked are either already processed or don't participate in the shuffling
    UINT8 isMarked;
};

BOOL AddNextShuffleEntryToArray(ArgLocDesc sArgSrc, ArgLocDesc sArgDst, SArray<ShuffleEntry> * pShuffleEntryArray, ShuffleComputationType shuffleType)
{
#if defined(TARGET_RISCV64) || defined(TARGET_LOONGARCH64)
    if (sArgSrc.m_structFields.flags != sArgDst.m_structFields.flags)
    {
        _ASSERT(sArgSrc.m_structFields.flags == FpStruct::UseIntCallConv
            || sArgDst.m_structFields.flags == FpStruct::UseIntCallConv);
        // StubLinkerCPU::EmitShuffleThunk supports shuffles only within the integer calling convention (floating-point
        // arguments may be passed in registers but these are not shuffled then). Transferring arguments between
        // calling conventions is handled by IL stubs.
        return FALSE;
    }
#endif // TARGET_RISCV64 || TARGET_LOONGARCH64

    ShuffleEntry entry;
    ZeroMemory(&entry, sizeof(entry));

    ShuffleIterator iteratorSrc(&sArgSrc);
    ShuffleIterator iteratorDst(&sArgDst);

    // Shuffle each slot in the argument (register or stack slot) from source to destination.
    while (iteratorSrc.HasNextOfs())
    {
        // We should have slots to shuffle in the destination at the same time as the source.
        _ASSERTE(iteratorDst.HasNextOfs());

        // Locate the next slot to shuffle in the source and destination and encode the transfer into a
        // shuffle entry.
        const int srcOffset = iteratorSrc.GetNextOfs();
        const int dstOffset = iteratorDst.GetNextOfs();

        // Only emit this entry if it's not a no-op (i.e. the source and destination locations are
        // different).
        if (srcOffset != dstOffset)
        {
            entry.srcofs = (UINT16)srcOffset;
            entry.dstofs = (UINT16)dstOffset;

            if (shuffleType == ShuffleComputationType::InstantiatingStub)
            {
                // Instantiating Stub shuffles only support general register to register moves. More complex cases are handled by IL stubs
                if (!(entry.srcofs & ShuffleEntry::REGMASK) || !(entry.dstofs & ShuffleEntry::REGMASK))
                {
                    return FALSE;
                }
                if ((entry.srcofs == ShuffleEntry::HELPERREG) || (entry.dstofs == ShuffleEntry::HELPERREG))
                {
                    return FALSE;
                }
            }

            pShuffleEntryArray->Append(entry);
        }
    }

    // We should have run out of slots to shuffle in the destination at the same time as the source.
    _ASSERTE(!iteratorDst.HasNextOfs());

    return TRUE;
}

BOOL GenerateShuffleArrayPortable(MethodDesc* pMethodSrc, MethodDesc *pMethodDst, SArray<ShuffleEntry> * pShuffleEntryArray, ShuffleComputationType shuffleType)
{
    STANDARD_VM_CONTRACT;

    ShuffleEntry entry;
    ZeroMemory(&entry, sizeof(entry));

    MetaSig sSigSrc(pMethodSrc);
    MetaSig sSigDst(pMethodDst);

    // Initialize helpers that determine how each argument for the source and destination signatures is placed
    // in registers or on the stack.
    ArgIterator sArgPlacerSrc(&sSigSrc);
    ArgIterator sArgPlacerDst(&sSigDst);

    if (shuffleType == ShuffleComputationType::InstantiatingStub)
    {
        // Instantiating Stub shuffles only support register to register moves. More complex cases are handled by IL stubs
        UINT stackSizeSrc = sArgPlacerSrc.SizeOfArgStack();
        UINT stackSizeDst = sArgPlacerDst.SizeOfArgStack();
        if (stackSizeSrc != stackSizeDst)
            return FALSE;
    }

    INT ofsSrc;
    INT ofsDst;
    ArgLocDesc sArgSrc;
    ArgLocDesc sArgDst;

    unsigned int argSlots = NUM_ARGUMENT_REGISTERS
#ifdef NUM_FLOAT_ARGUMENT_REGISTERS
                    + NUM_FLOAT_ARGUMENT_REGISTERS
#endif
                    + sArgPlacerSrc.SizeOfArgStack() / sizeof(size_t);

    // If the target method in non-static (this happens for open instance delegates), we need to account for
    // the implicit this parameter.
    if (sSigDst.HasThis())
    {
        if (shuffleType == ShuffleComputationType::DelegateShuffleThunk)
        {
            // The this pointer is an implicit argument for the destination signature. But on the source side it's
            // just another regular argument and needs to be iterated over by sArgPlacerSrc and the MetaSig.
            sArgPlacerSrc.GetArgLoc(sArgPlacerSrc.GetNextOffset(), &sArgSrc);
            sArgPlacerSrc.GetThisLoc(&sArgDst);
        }
        else if (shuffleType == ShuffleComputationType::InstantiatingStub)
        {
            _ASSERTE(sSigSrc.HasThis()); // Instantiating stubs should have the same HasThis flag
            sArgPlacerDst.GetThisLoc(&sArgDst);
            sArgPlacerSrc.GetThisLoc(&sArgSrc);
        }
        else
        {
            _ASSERTE(FALSE); // Unknown shuffle type being generated
        }

        if (!AddNextShuffleEntryToArray(sArgSrc, sArgDst, pShuffleEntryArray, shuffleType))
            return FALSE;
    }

    // Handle any return buffer argument.
    _ASSERTE(!!sArgPlacerDst.HasRetBuffArg() == !!sArgPlacerSrc.HasRetBuffArg());
    if (sArgPlacerDst.HasRetBuffArg())
    {
        // The return buffer argument is implicit in both signatures.

#if !defined(TARGET_ARM64) || !defined(CALLDESCR_RETBUFFARGREG)
        // The ifdef above disables this code if the ret buff arg is always in the same register, which
        // means that we don't need to do any shuffling for it.

        sArgPlacerSrc.GetRetBuffArgLoc(&sArgSrc);
        sArgPlacerDst.GetRetBuffArgLoc(&sArgDst);

        if (!AddNextShuffleEntryToArray(sArgSrc, sArgDst, pShuffleEntryArray, shuffleType))
            return FALSE;
#endif // !defined(TARGET_ARM64) || !defined(CALLDESCR_RETBUFFARGREG)
    }

    // Iterate all the regular arguments. mapping source registers and stack locations to the corresponding
    // destination locations.
    while ((ofsSrc = sArgPlacerSrc.GetNextOffset()) != TransitionBlock::InvalidOffset)
    {
        ofsDst = sArgPlacerDst.GetNextOffset();

        // Find the argument location mapping for both source and destination signature. A single argument can
        // occupy a floating point register, a general purpose register, a pair of registers of any kind or
        // a stack slot.
        sArgPlacerSrc.GetArgLoc(ofsSrc, &sArgSrc);
        sArgPlacerDst.GetArgLoc(ofsDst, &sArgDst);

        if (!AddNextShuffleEntryToArray(sArgSrc, sArgDst, pShuffleEntryArray, shuffleType))
            return FALSE;
    }

    if (shuffleType == ShuffleComputationType::InstantiatingStub
#if defined(UNIX_AMD64_ABI)
        || true
#endif // UNIX_AMD64_ABI
      )
    {
        // The Unix AMD64 ABI can cause a struct to be passed on stack for the source and in registers for the destination.
        // That can cause some arguments that are passed on stack for the destination to be passed in registers in the source.
        // An extreme example of that is e.g.:
        //   void fn(int, int, int, int, int, struct {int, double}, double, double, double, double, double, double, double, double, double, double)
        // For this signature, the shuffle needs to move slots as follows (please note the "forward" movement of xmm registers):
        //   RDI->RSI, RDX->RCX, R8->RDX, R9->R8, stack[0]->R9, xmm0->xmm1, xmm1->xmm2, ... xmm6->xmm7, xmm7->stack[0], stack[1]->xmm0, stack[2]->stack[1], stack[3]->stack[2]
        // To prevent overwriting of slots before they are moved, we need to perform the shuffling in correct order

        NewArrayHolder<ShuffleGraphNode> pGraphNodes = new ShuffleGraphNode[argSlots];

        // Initialize the graph array
        for (unsigned int i = 0; i < argSlots; i++)
        {
            pGraphNodes[i].prev = ShuffleGraphNode::NoNode;
            pGraphNodes[i].isMarked = true;
            pGraphNodes[i].isSource = false;
        }

        // Build the directed graph representing register and stack slot shuffling.
        // The links are directed from destination to source.
        // During the build also set isSource flag for nodes that are sources of data.
        // The ones that don't have the isSource flag set are beginnings of non-cyclic
        // segments of the graph.
        for (unsigned int i = 0; i < pShuffleEntryArray->GetCount(); i++)
        {
            ShuffleEntry entry = (*pShuffleEntryArray)[i];

            UINT16 srcIndex = GetNormalizedArgumentSlotIndex(entry.srcofs);
            UINT16 dstIndex = GetNormalizedArgumentSlotIndex(entry.dstofs);

            _ASSERTE((srcIndex >= 0) && (srcIndex < argSlots));
            _ASSERTE((dstIndex >= 0) && (dstIndex < argSlots));

            // Unmark the node to indicate that it was not processed yet
            pGraphNodes[srcIndex].isMarked = false;
            // The node contains a register / stack slot that is a source from which we move data to a destination one
            pGraphNodes[srcIndex].isSource = true;
            pGraphNodes[srcIndex].ofs = entry.srcofs;

            // Unmark the node to indicate that it was not processed yet
            pGraphNodes[dstIndex].isMarked = false;
            // Link to the previous node in the graph (source of data for the current node)
            pGraphNodes[dstIndex].prev = srcIndex;
            pGraphNodes[dstIndex].ofs = entry.dstofs;
        }

        // Now that we've built the graph, clear the array, we will regenerate it from the graph ensuring a proper order of shuffling
        pShuffleEntryArray->Clear();

        // Add all non-cyclic subgraphs to the target shuffle array and mark their nodes as visited
        for (unsigned int startIndex = 0; startIndex < argSlots; startIndex++)
        {
            unsigned int index = startIndex;

            if (!pGraphNodes[index].isMarked && !pGraphNodes[index].isSource)
            {
                // This node is not a source, that means it is an end of shuffle chain
                // Generate shuffle array entries for all nodes in the chain in a correct
                // order.
                UINT16 dstOfs = ShuffleEntry::SENTINEL;

                do
                {
                    _ASSERTE(index < argSlots);
                    pGraphNodes[index].isMarked = true;
                    if (dstOfs != ShuffleEntry::SENTINEL)
                    {
                        entry.srcofs = pGraphNodes[index].ofs;
                        entry.dstofs = dstOfs;
                        pShuffleEntryArray->Append(entry);
                    }

                    dstOfs = pGraphNodes[index].ofs;
                    index = pGraphNodes[index].prev;
                }
                while (index != ShuffleGraphNode::NoNode);
            }
        }

        // Process all cycles in the graph
        for (unsigned int startIndex = 0; startIndex < argSlots; startIndex++)
        {
            unsigned int index = startIndex;

            if (!pGraphNodes[index].isMarked)
            {
                if (shuffleType == ShuffleComputationType::InstantiatingStub)
                {
                    // Use of the helper reg isn't supported for these stubs.
                    return FALSE;
                }
                // This node is part of a new cycle as all non-cyclic parts of the graphs were already visited

                // Move the first node register / stack slot to a helper reg
                UINT16 dstOfs = ShuffleEntry::HELPERREG;

                do
                {
                    _ASSERTE(index < argSlots);
                    pGraphNodes[index].isMarked = true;

                    entry.srcofs = pGraphNodes[index].ofs;
                    entry.dstofs = dstOfs;
                    pShuffleEntryArray->Append(entry);

                    dstOfs = pGraphNodes[index].ofs;
                    index = pGraphNodes[index].prev;
                }
                while (index != startIndex);

                // Move helper reg to the last node register / stack slot
                entry.srcofs = ShuffleEntry::HELPERREG;
                entry.dstofs = dstOfs;
                pShuffleEntryArray->Append(entry);
            }
        }
    }

    entry.srcofs = ShuffleEntry::SENTINEL;
    entry.stacksizedelta = 0;
    pShuffleEntryArray->Append(entry);

    return TRUE;
}
#endif // FEATURE_PORTABLE_SHUFFLE_THUNKS

BOOL GenerateShuffleArray(MethodDesc* pInvoke, MethodDesc *pTargetMeth, SArray<ShuffleEntry> * pShuffleEntryArray)
{
    STANDARD_VM_CONTRACT;

#ifdef FEATURE_PORTABLE_SHUFFLE_THUNKS
    // Portable default implementation
    if (!GenerateShuffleArrayPortable(pInvoke, pTargetMeth, pShuffleEntryArray, ShuffleComputationType::DelegateShuffleThunk))
        return FALSE;
#elif defined(TARGET_X86)
    ShuffleEntry entry;
    ZeroMemory(&entry, sizeof(entry));

    // Must create independent msigs to prevent the argiterators from
    // interfering with other.
    MetaSig sSigSrc(pInvoke);
    MetaSig sSigDst(pTargetMeth);

    _ASSERTE(sSigSrc.HasThis());

    ArgIterator sArgPlacerSrc(&sSigSrc);
    ArgIterator sArgPlacerDst(&sSigDst);

    UINT stackSizeSrc = sArgPlacerSrc.SizeOfArgStack();
    UINT stackSizeDst = sArgPlacerDst.SizeOfArgStack();

    if (stackSizeDst > stackSizeSrc)
    {
        // we can drop arguments but we can never make them up - this is definitely not allowed
        COMPlusThrow(kVerificationException);
    }

    UINT stackSizeDelta;

#ifdef UNIX_X86_ABI
    // Stack does not shrink as UNIX_X86_ABI uses CDECL (instead of STDCALL).
    stackSizeDelta = 0;
#else
    stackSizeDelta = stackSizeSrc - stackSizeDst;
#endif

    INT ofsSrc, ofsDst;

    // if the function is non static we need to place the 'this' first
    if (!pTargetMeth->IsStatic())
    {
        entry.srcofs = ShuffleOfs(sArgPlacerSrc.GetNextOffset());
        entry.dstofs = ShuffleEntry::REGMASK | 4;
        pShuffleEntryArray->Append(entry);
    }
    else if (sArgPlacerSrc.HasRetBuffArg())
    {
        // the first register is used for 'this'
        entry.srcofs = ShuffleOfs(sArgPlacerSrc.GetRetBuffArgOffset());
        entry.dstofs = ShuffleOfs(sArgPlacerDst.GetRetBuffArgOffset(), stackSizeDelta);
        if (entry.srcofs != entry.dstofs)
            pShuffleEntryArray->Append(entry);
    }

    while (TransitionBlock::InvalidOffset != (ofsSrc = sArgPlacerSrc.GetNextOffset()))
    {
        ofsDst = sArgPlacerDst.GetNextOffset();

        int cbSize = sArgPlacerDst.GetArgSize();

        do
        {
            entry.srcofs = ShuffleOfs(ofsSrc);
            entry.dstofs = ShuffleOfs(ofsDst, stackSizeDelta);

            ofsSrc += TARGET_POINTER_SIZE;
            ofsDst += TARGET_POINTER_SIZE;

            if (entry.srcofs != entry.dstofs)
                pShuffleEntryArray->Append(entry);

            cbSize -= TARGET_POINTER_SIZE;
        }
        while (cbSize > 0);
    }

    if (stackSizeDelta != 0)
    {
        // Emit code to move the return address
        entry.srcofs = 0;     // retaddress is assumed to be at esp
        entry.dstofs = static_cast<UINT16>(stackSizeDelta);
        pShuffleEntryArray->Append(entry);
    }

    entry.srcofs = ShuffleEntry::SENTINEL;
    entry.stacksizedelta = static_cast<UINT16>(stackSizeDelta);
    pShuffleEntryArray->Append(entry);

#else
#error Unsupported architecture
#endif

    if (LoggingOn(LF_STUBS, LL_INFO1000000))
    {
        LOGALWAYS(("GenerateShuffleArray: %u entries for %s.%s -> %s.%s:\n",
            pShuffleEntryArray->GetCount(),
            pInvoke->GetMethodTable()->GetDebugClassName(), pInvoke->GetName(),
            pTargetMeth->GetMethodTable()->GetDebugClassName(), pTargetMeth->GetName()));

        for (COUNT_T i = 0; i < pShuffleEntryArray->GetCount(); ++i)
        {
            ShuffleEntry entry = (*pShuffleEntryArray)[i];
            if (entry.srcofs == ShuffleEntry::SENTINEL)
            {
                LOGALWAYS(("    [%u] sentinel, stack size delta %u\n", i, entry.stacksizedelta));
                _ASSERTE(i == pShuffleEntryArray->GetCount() - 1);
                break;
            }

            struct ShuffleInfo { const char* type; int offset; };
            auto getShuffleInfo = [](UINT16 offset) -> ShuffleInfo {
                if (offset == ShuffleEntry::HELPERREG)
                {
                    return { "helper register" };
                }
                else if (offset & ShuffleEntry::REGMASK)
                {
                    const char* type = (offset & ShuffleEntry::FPREGMASK)
                        ? ((offset & ShuffleEntry::FPSINGLEMASK) ? "single-FP register" : "FP register")
                        : "integer register";
                    return { type, offset & ShuffleEntry::OFSREGMASK };
                }
                else
                {
                    return {"stack slot", offset & ShuffleEntry::OFSMASK };
                }
            };
            ShuffleInfo src = getShuffleInfo(entry.srcofs);
            ShuffleInfo dst = getShuffleInfo(entry.dstofs);
            LOGALWAYS(("    [%u] %s %i -> %s %i\n", i, src.type, src.offset, dst.type, dst.offset));
        }
    }
    return TRUE;
}

static ShuffleThunkCache* s_pShuffleThunkCache = NULL;

// One time init.
void COMDelegate::Init()
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    s_pShuffleThunkCache = new ShuffleThunkCache(SystemDomain::GetGlobalLoaderAllocator()->GetStubHeap());
}

#ifdef FEATURE_COMINTEROP
CLRToCOMCallInfo * COMDelegate::PopulateCLRToCOMCallInfo(MethodTable * pDelMT)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    DelegateEEClass * pClass = (DelegateEEClass *)pDelMT->GetClass();

    // set up the CLRToCOMCallInfo if it does not exist already
    if (pClass->m_pCLRToCOMCallInfo == NULL)
    {
        LoaderHeap *pHeap = pDelMT->GetLoaderAllocator()->GetHighFrequencyHeap();
        CLRToCOMCallInfo *pTemp = (CLRToCOMCallInfo *)(void *)pHeap->AllocMem(S_SIZE_T(sizeof(CLRToCOMCallInfo)));

        pTemp->m_cachedComSlot = ComMethodTable::GetNumExtraSlots(ifVtable);
        pTemp->InitStackArgumentSize();

        InterlockedCompareExchangeT(&pClass->m_pCLRToCOMCallInfo, pTemp, NULL);
    }

    pClass->m_pCLRToCOMCallInfo->m_pInterfaceMT = pDelMT;

    return pClass->m_pCLRToCOMCallInfo;
}
#endif // FEATURE_COMINTEROP

// We need a LoaderHeap that lives at least as long as the DelegateEEClass, but ideally no longer
LoaderHeap *DelegateEEClass::GetStubHeap()
{
    return GetInvokeMethod()->GetLoaderAllocator()->GetStubHeap();
}

#if defined(TARGET_RISCV64) || defined(TARGET_LOONGARCH64)
static Stub* CreateILDelegateShuffleThunk(MethodDesc* pDelegateMD, bool callTargetWithThis)
{
    SigTypeContext typeContext(pDelegateMD);
    MetaSig sig(pDelegateMD);
    if (LoggingOn(LF_STUBS, LL_INFO1000000))
    {
        SString delegateName;
        pDelegateMD->GetFullMethodInfo(delegateName);
        LOGALWAYS(("CreateILDelegateShuffleThunk %s (%i args, callTargetWithThis:%i)\n",
            delegateName.GetUTF8(), sig.NumFixedArgs(), callTargetWithThis));
    }
    _ASSERTE(sig.HasThis());

    Module* pModule = sig.GetModule();
    Signature signature = pDelegateMD->GetSignature();

    ILStubLinkerFlags flags = ILSTUB_LINKER_FLAG_STUB_HAS_THIS;
    ILStubLinker stubLinker(pModule, signature, &typeContext, pDelegateMD, flags);
    ILCodeStream *pCode = stubLinker.NewCodeStream(ILStubLinker::kDispatch);

    for (unsigned i = 0; i < sig.NumFixedArgs(); ++i)
        pCode->EmitLDARG(i);

    pCode->EmitLoadThis();
    pCode->EmitLDFLD(pCode->GetToken(CoreLibBinder::GetField(FIELD__DELEGATE__METHOD_PTR_AUX)));
    pCode->EmitCALLI(TOKEN_ILSTUB_TARGET_SIG, sig.NumFixedArgs(), sig.IsReturnTypeVoid() ? 0 : 1);
    pCode->EmitRET();

    MethodDesc* pStubMD = ILStubCache::CreateAndLinkNewILStubMethodDesc(
        pDelegateMD->GetLoaderAllocator(), pDelegateMD->GetMethodTable(), ILSTUB_DELEGATE_SHUFFLE_THUNK,
        pModule, signature.GetRawSig(), signature.GetRawSigLen(), &typeContext, &stubLinker);

    // Build target signature
    SigBuilder sigBuilder(signature.GetRawSigLen());
    sigBuilder.AppendByte(callTargetWithThis
        ? IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS
        : IMAGE_CEE_CS_CALLCONV_DEFAULT);

    unsigned numFixedArgs = sig.NumFixedArgs() - callTargetWithThis;
    sigBuilder.AppendData(numFixedArgs);

    SigPointer pReturn = sig.GetReturnProps();
    pReturn.ConvertToInternalExactlyOne(pModule, &typeContext, &sigBuilder);

    sig.SkipArg(); // skip delegate object
    if (callTargetWithThis)
        sig.SkipArg();

    SigPointer pArgs = sig.GetArgProps();
    for (unsigned i = 0; i < numFixedArgs; ++i)
        pArgs.ConvertToInternalExactlyOne(pModule, &typeContext, &sigBuilder);

    DWORD cbTargetSig;
    PCCOR_SIGNATURE pTargetSig = (PCCOR_SIGNATURE)sigBuilder.GetSignature(&cbTargetSig);
    ILStubResolver* pResolver = pStubMD->AsDynamicMethodDesc()->GetILStubResolver();
    pResolver->SetStubTargetMethodSig(pTargetSig, cbTargetSig);

    return Stub::NewStub(JitILStub(pStubMD), NEWSTUB_FL_SHUFFLE_THUNK);
}
#endif // TARGET_RISCV64 || TARGET_LOONGARCH64

static PCODE SetupShuffleThunk(MethodTable * pDelMT, MethodDesc *pTargetMeth)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
        INJECT_FAULT(COMPlusThrowOM());
    }
    CONTRACTL_END;

    bool isInstRetBuff = (!pTargetMeth->IsStatic() && pTargetMeth->HasRetBuffArg() && IsRetBuffPassedAsFirstArg());
    DelegateEEClass * pClass = (DelegateEEClass *)pDelMT->GetClass();

    // Look for a thunk cached on the delegate class first. Note we need a different thunk for instance methods with a
    // hidden return buffer argument because the extra argument switches place with the target when coming from the caller.
    Stub* pShuffleThunk = isInstRetBuff ? pClass->m_pInstRetBuffCallStub : pClass->m_pStaticCallStub;
    if (pShuffleThunk)
        return pShuffleThunk->GetEntryPoint();

    GCX_PREEMP();

    MethodDesc *pMD = pClass->GetInvokeMethod();

    // We haven't already setup a shuffle thunk, go do it now (which will cache the result automatically).
    StackSArray<ShuffleEntry> rShuffleEntryArray;
    if (GenerateShuffleArray(pMD, pTargetMeth, &rShuffleEntryArray))
    {
        ShuffleThunkCache* pShuffleThunkCache = s_pShuffleThunkCache;

        LoaderAllocator* pLoaderAllocator = pDelMT->GetLoaderAllocator();
        if (pLoaderAllocator->IsCollectible())
        {
            pShuffleThunkCache = ((AssemblyLoaderAllocator*)pLoaderAllocator)->GetShuffleThunkCache();
        }

        pShuffleThunk = pShuffleThunkCache->Canonicalize((const BYTE *)&rShuffleEntryArray[0], "DelegateShuffleThunk");
    }
    else
    {
#if defined(TARGET_RISCV64) || defined(TARGET_LOONGARCH64)
        pShuffleThunk = CreateILDelegateShuffleThunk(pMD, isInstRetBuff);
#else
        _ASSERTE(FALSE);
        return (PCODE)NULL;
#endif // TARGET_RISCV64 || TARGET_LOONGARCH64
    }

    if (!pShuffleThunk)
    {
        COMPlusThrowOM();
    }

    // Cache the shuffle thunk
    Stub** ppThunk = isInstRetBuff ? &pClass->m_pInstRetBuffCallStub : &pClass->m_pStaticCallStub;
    Stub* pExistingThunk = InterlockedCompareExchangeT(ppThunk, pShuffleThunk, NULL);
    if (pExistingThunk != NULL)
    {
        if (pShuffleThunk->HasExternalEntryPoint()) // IL thunk
        {
            pShuffleThunk->DecRef();
        }
        else
        {
            ExecutableWriterHolder<Stub> shuffleThunkWriterHolder(pShuffleThunk, sizeof(Stub));
            shuffleThunkWriterHolder.GetRW()->DecRef();
        }
        pShuffleThunk = pExistingThunk;
    }

    return pShuffleThunk->GetEntryPoint();
}

extern "C" PCODE CID_VirtualOpenDelegateDispatch(TransitionBlock * pTransitionBlock);

static PCODE GetVirtualCallStub(MethodDesc *method, TypeHandle scopeType)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
        INJECT_FAULT(COMPlusThrowOM()); // from MetaSig::SizeOfArgStack
    }
    CONTRACTL_END;

    //TODO: depending on what we decide for generics method we may want to move this check to better places
    if (method->IsGenericMethodDefinition() || method->HasMethodInstantiation())
    {
        COMPlusThrow(kNotSupportedException);
    }

    INTERFACE_DISPATCH_CACHED_OR_VSD(
        return (PCODE)CID_VirtualOpenDelegateDispatch;
        ,
        // need to grab a virtual dispatch stub
        // method can be on a canonical MethodTable, we need to allocate the stub on the loader allocator associated with the exact type instantiation.
        VirtualCallStubManager *pVirtualStubManager = scopeType.GetMethodTable()->GetLoaderAllocator()->GetVirtualCallStubManager();
        PCODE pTargetCall = pVirtualStubManager->GetCallStub(scopeType, method);
        _ASSERTE(pTargetCall);
        return pTargetCall;
        );
}

extern "C" BOOL QCALLTYPE Delegate_BindToMethodName(QCall::ObjectHandleOnStack d, QCall::ObjectHandleOnStack target,
    QCall::TypeHandle pMethodType, LPCUTF8 pszMethodName, DelegateBindingFlags flags)
{
    QCALL_CONTRACT;

    MethodDesc *pMatchingMethod = NULL;

    BEGIN_QCALL;

    GCX_COOP();

    struct
    {
        DELEGATEREF refThis;
        OBJECTREF target;
    } gc;

    gc.refThis    = (DELEGATEREF) d.Get();
    gc.target     = target.Get();

    GCPROTECT_BEGIN(gc);

    TypeHandle methodType = pMethodType.AsTypeHandle();

    TypeHandle targetType((gc.target != NULL) ? gc.target->GetMethodTable() : NULL);
    // get the invoke of the delegate
    MethodTable * pDelegateType = gc.refThis->GetMethodTable();
    MethodDesc* pInvokeMeth = COMDelegate::FindDelegateInvokeMethod(pDelegateType);
    _ASSERTE(pInvokeMeth);

    //
    // now loop through the methods looking for a match
    //

    // pick a proper compare function
    typedef int (__cdecl *UTF8StringCompareFuncPtr)(const char *, const char *);
    UTF8StringCompareFuncPtr StrCompFunc = (flags & DBF_CaselessMatching) ? stricmpUTF8 : strcmp;

    // search the type hierarchy
    MethodTable *pMTOrig = methodType.GetMethodTable()->GetCanonicalMethodTable();
    for (MethodTable *pMT = pMTOrig; pMT != NULL; pMT = pMT->GetParentMethodTable())
    {
        MethodTable::MethodIterator it(pMT);
        it.MoveToEnd();
        for (; it.IsValid() && (pMT == pMTOrig || !it.IsVirtual()); it.Prev())
        {
            MethodDesc *pCurMethod = it.GetDeclMethodDesc();

            // We can't match generic methods (since no instantiation information has been provided).
            if (pCurMethod->IsGenericMethodDefinition())
                continue;

            if ((pCurMethod != NULL) && (StrCompFunc(pszMethodName, pCurMethod->GetName()) == 0))
            {
                // found a matching string, get an associated method desc if needed
                // Use unboxing stubs for instance and virtual methods on value types.
                // If this is a open delegate to an instance method BindToMethod will rebind it to the non-unboxing method.
                // Open delegate
                //   Static: never use unboxing stub
                //     BindToMethodInfo/Name will bind to the non-unboxing stub. BindToMethod will reinforce that.
                //   Instance: We only support binding to an unboxed value type reference here, so we must never use an unboxing stub
                //     BindToMethodInfo/Name will bind to the unboxing stub. BindToMethod will rebind to the non-unboxing stub.
                //   Virtual: trivial (not allowed)
                // Closed delegate
                //   Static: never use unboxing stub
                //     BindToMethodInfo/Name will bind to the non-unboxing stub.
                //   Instance: always use unboxing stub
                //     BindToMethodInfo/Name will bind to the unboxing stub.
                //   Virtual: always use unboxing stub
                //     BindToMethodInfo/Name will bind to the unboxing stub.

                pCurMethod =
                    MethodDesc::FindOrCreateAssociatedMethodDesc(pCurMethod,
                                                                 methodType.GetMethodTable(),
                                                                 (!pCurMethod->IsStatic() && pCurMethod->GetMethodTable()->IsValueType()),
                                                                 pCurMethod->GetMethodInstantiation(),
                                                                 false /* do not allow code with a shared-code calling convention to be returned */,
                                                                 true /* Ensure that methods on generic interfaces are returned as instantiated method descs */);
                bool fIsOpenDelegate;
                if (!COMDelegate::IsMethodDescCompatible((gc.target == NULL) ? TypeHandle() : gc.target->GetTypeHandle(),
                                                        methodType,
                                                        pCurMethod,
                                                        gc.refThis->GetTypeHandle(),
                                                        pInvokeMeth,
                                                        flags,
                                                        &fIsOpenDelegate))
                {
                    // Signature doesn't match, skip.
                    continue;
                }

                // Found the target that matches the signature and satisfies security transparency rules
                // Initialize the delegate to point to the target method.
                COMDelegate::BindToMethod(&gc.refThis,
                             &gc.target,
                             pCurMethod,
                             methodType.GetMethodTable(),
                             fIsOpenDelegate);

                pMatchingMethod = pCurMethod;
                goto done;
            }
        }
    }
    done:
        ;

    GCPROTECT_END();

    END_QCALL;

    return (pMatchingMethod != NULL);
}

extern "C" BOOL QCALLTYPE Delegate_BindToMethodInfo(QCall::ObjectHandleOnStack d, QCall::ObjectHandleOnStack target,
    MethodDesc * method, QCall::TypeHandle pMethodType, DelegateBindingFlags flags)
{
    QCALL_CONTRACT;

    BOOL result = TRUE;

    BEGIN_QCALL;

    GCX_COOP();

    struct
    {
        DELEGATEREF refThis;
        OBJECTREF refFirstArg;
    } gc;

    gc.refThis          = (DELEGATEREF) d.Get();
    gc.refFirstArg      = target.Get();

    GCPROTECT_BEGIN(gc);

    MethodTable *pMethMT = pMethodType.AsTypeHandle().GetMethodTable();

    // Assert to track down VS#458689.
    _ASSERTE(gc.refThis != gc.refFirstArg);

    // A generic method had better be instantiated (we can't dispatch to an uninstantiated one).
    if (method->IsGenericMethodDefinition())
        COMPlusThrow(kArgumentException, W("Arg_DlgtTargMeth"));

    // get the invoke of the delegate
    MethodTable * pDelegateType = gc.refThis->GetMethodTable();
    MethodDesc* pInvokeMeth = COMDelegate::FindDelegateInvokeMethod(pDelegateType);
    _ASSERTE(pInvokeMeth);

    // See the comment in BindToMethodName
    method =
        MethodDesc::FindOrCreateAssociatedMethodDesc(method,
                                                     pMethMT,
                                                     (!method->IsStatic() && pMethMT->IsValueType()),
                                                     method->GetMethodInstantiation(),
                                                     false /* do not allow code with a shared-code calling convention to be returned */,
                                                     true /* Ensure that methods on generic interfaces are returned as instantiated method descs */);

    bool fIsOpenDelegate;
    if (COMDelegate::IsMethodDescCompatible((gc.refFirstArg == NULL) ? TypeHandle() : gc.refFirstArg->GetTypeHandle(),
                                            TypeHandle(pMethMT),
                                            method,
                                            gc.refThis->GetTypeHandle(),
                                            pInvokeMeth,
                                            flags,
                                            &fIsOpenDelegate))
    {
        // Initialize the delegate to point to the target method.
        COMDelegate::BindToMethod(&gc.refThis,
                     &gc.refFirstArg,
                     method,
                     pMethMT,
                     fIsOpenDelegate);
    }
    else
        result = FALSE;

    GCPROTECT_END();

    END_QCALL;

    return result;
}

// This method is called (in the late bound case only) once a target method has been decided on. All the consistency checks
// (signature matching etc.) have been done at this point, this method will simply initialize the delegate, with any required
// wrapping. The delegate returned will be ready for invocation immediately.
void COMDelegate::BindToMethod(DELEGATEREF   *pRefThis,
                               OBJECTREF     *pRefFirstArg,
                               MethodDesc    *pTargetMethod,
                               MethodTable   *pExactMethodType,
                               BOOL           fIsOpenDelegate)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
        PRECONDITION(CheckPointer(pRefThis));
        PRECONDITION(CheckPointer(pRefFirstArg, NULL_OK));
        PRECONDITION(CheckPointer(pTargetMethod));
        PRECONDITION(CheckPointer(pExactMethodType));
    }
    CONTRACTL_END;

    // The delegate may be put into a wrapper delegate if our target method requires it. This local
    // will always hold the real (un-wrapped) delegate.
    DELEGATEREF refRealDelegate = NULL;
    GCPROTECT_BEGIN(refRealDelegate);

    // If needed, convert the delegate into a wrapper and get the real delegate within that.
    if (NeedsWrapperDelegate(pTargetMethod))
        refRealDelegate = CreateWrapperDelegate(*pRefThis, pTargetMethod);
    else
        refRealDelegate = *pRefThis;

    pTargetMethod->EnsureActive();

    if (fIsOpenDelegate)
    {
        _ASSERTE(pRefFirstArg == NULL || *pRefFirstArg == NULL);

        // Open delegates use themselves as the target (which handily allows their shuffle thunks to locate additional data at
        // invocation time).
        refRealDelegate->SetTarget(refRealDelegate);

        // We need to shuffle arguments for open delegates since the first argument on the calling side is not meaningful to the
        // callee.
        MethodTable * pDelegateMT = (*pRefThis)->GetMethodTable();
        PCODE pEntryPoint = SetupShuffleThunk(pDelegateMT, pTargetMethod);

        // Indicate that the delegate will jump to the shuffle thunk rather than directly to the target method.
        refRealDelegate->SetMethodPtr(pEntryPoint);

        // Use stub dispatch for all virtuals.
        // <TODO> Investigate not using this for non-interface virtuals. </TODO>
        // The virtual dispatch stub doesn't work on unboxed value type objects which don't have MT pointers.
        // Since open instance delegates on value type methods require unboxed objects we cannot use the
        // virtual dispatch stub for them. On the other hand, virtual methods on value types don't need
        // to be dispatched because value types cannot be derived. So we treat them like non-virtual methods.
        if (pTargetMethod->IsVirtual() && !pTargetMethod->GetMethodTable()->IsValueType())
        {
            // Since this is an open delegate over a virtual method we cannot virtualize the call target now. So the shuffle thunk
            // needs to jump to another stub (this time provided by the VirtualStubManager) that will virtualize the call at
            // runtime.
            PCODE pTargetCall = GetVirtualCallStub(pTargetMethod, TypeHandle(pExactMethodType));
            refRealDelegate->SetMethodPtrAux(pTargetCall);
            refRealDelegate->SetInvocationCount((INT_PTR)(void *)pTargetMethod);
        }
        else
        {
            // <TODO> If VSD isn't compiled in this gives the wrong result for virtuals (we need run time virtualization). </TODO>
            // Reflection or the code in BindToMethodName will pass us the unboxing stub for non-static methods on value types. But
            // for open invocation on value type methods the actual reference will be passed so we need the unboxed method desc
            // instead.
            if (pTargetMethod->IsUnboxingStub())
            {
                // We want a MethodDesc which is not an unboxing stub, but is an instantiating stub if needed.
                pTargetMethod = MethodDesc::FindOrCreateAssociatedMethodDesc(
                                                        pTargetMethod,
                                                        pExactMethodType,
                                                        FALSE /* don't want unboxing entry point */,
                                                        pTargetMethod->GetMethodInstantiation(),
                                                        FALSE /* don't want MD that requires inst. arguments */,
                                                        true /* Ensure that methods on generic interfaces are returned as instantiated method descs */);
            }

            // The method must not require any extra hidden instantiation arguments.
            _ASSERTE(!pTargetMethod->RequiresInstArg());

            // Note that it is important to cache pTargetCode in local variable to avoid GC hole.
            // GetMultiCallableAddrOfCode() can trigger GC.
            PCODE pTargetCode = pTargetMethod->GetMultiCallableAddrOfCode();
            refRealDelegate->SetMethodPtrAux(pTargetCode);
        }
    }
    else
    {
        PCODE pTargetCode = (PCODE)NULL;

        // For virtual methods we can (and should) virtualize the call now (so we don't have to insert a thunk to do so at runtime).
        // <TODO>
        // Remove the following if we decide we won't cope with this case on late bound.
        // We can get virtual delegates closed over null through this code path, so be careful to handle that case (no need to
        // virtualize since we're just going to throw NullRefException at invocation time).
        // </TODO>
        if (pTargetMethod->IsVirtual() &&
            *pRefFirstArg != NULL &&
            pTargetMethod->GetMethodTable() != (*pRefFirstArg)->GetMethodTable())
            pTargetCode = pTargetMethod->GetMultiCallableAddrOfVirtualizedCode(pRefFirstArg, pTargetMethod->GetMethodTable());
        else
#ifdef HAS_THISPTR_RETBUF_PRECODE
        if (pTargetMethod->IsStatic() && pTargetMethod->HasRetBuffArg() && IsRetBuffPassedAsFirstArg())
            pTargetCode = pTargetMethod->GetLoaderAllocator()->GetFuncPtrStubs()->GetFuncPtrStub(pTargetMethod, PRECODE_THISPTR_RETBUF);
        else
#endif // HAS_THISPTR_RETBUF_PRECODE
            pTargetCode = pTargetMethod->GetMultiCallableAddrOfCode();
        _ASSERTE(pTargetCode);

        refRealDelegate->SetTarget(*pRefFirstArg);
        refRealDelegate->SetMethodPtr(pTargetCode);
    }

    LoaderAllocator *pLoaderAllocator = pTargetMethod->GetLoaderAllocator();

    if (pLoaderAllocator->IsCollectible())
        refRealDelegate->SetMethodBase(pLoaderAllocator->GetExposedObject());

    GCPROTECT_END();
}

// Marshals a delegate to a unmanaged callback.
LPVOID COMDelegate::ConvertToCallback(OBJECTREF pDelegateObj)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;

        INJECT_FAULT(COMPlusThrowOM());
    }
    CONTRACTL_END;

    if (!pDelegateObj)
        return NULL;

    DELEGATEREF pDelegate = (DELEGATEREF) pDelegateObj;

    PCODE pCode;
    GCPROTECT_BEGIN(pDelegate);

    MethodTable* pMT = pDelegate->GetMethodTable();
    DelegateEEClass* pClass = (DelegateEEClass*)(pMT->GetClass());

    if (pMT->HasInstantiation())
        COMPlusThrowArgumentException(W("delegate"), W("Argument_NeedNonGenericType"));

    // If we are a delegate originally created from an unmanaged function pointer, we will simply return
    // that function pointer.
    if (DELEGATE_MARKER_UNMANAGEDFPTR == pDelegate->GetInvocationCount())
    {
        pCode = pDelegate->GetMethodPtrAux();
    }
    else
    {
        UMEntryThunkData*   pUMEntryThunk   = NULL;
        SyncBlock*          pSyncBlock      = pDelegate->GetSyncBlock();

        InteropSyncBlockInfo* pInteropInfo = pSyncBlock->GetInteropInfo();

        pUMEntryThunk = pInteropInfo->GetUMEntryThunk();

        if (!pUMEntryThunk)
        {

            UMThunkMarshInfo *pUMThunkMarshInfo = pClass->m_pUMThunkMarshInfo;
            MethodDesc *pInvokeMeth = FindDelegateInvokeMethod(pMT);

            if (!pUMThunkMarshInfo)
            {
                GCX_PREEMP();

                pUMThunkMarshInfo = (UMThunkMarshInfo*)(void*)pMT->GetLoaderAllocator()->GetLowFrequencyHeap()->AllocMem(S_SIZE_T(sizeof(UMThunkMarshInfo)));
                pUMThunkMarshInfo->LoadTimeInit(pInvokeMeth);

                if (InterlockedCompareExchangeT(&(pClass->m_pUMThunkMarshInfo),
                                                        pUMThunkMarshInfo,
                                                        NULL ) != NULL)
                {
                    pMT->GetLoaderAllocator()->GetLowFrequencyHeap()->BackoutMem(pUMThunkMarshInfo, sizeof(UMThunkMarshInfo));
                    pUMThunkMarshInfo = pClass->m_pUMThunkMarshInfo;
                }
            }

            _ASSERTE(pUMThunkMarshInfo != NULL);
            _ASSERTE(pUMThunkMarshInfo == pClass->m_pUMThunkMarshInfo);

            pUMEntryThunk = UMEntryThunkData::CreateUMEntryThunk();
            Holder<UMEntryThunkData *, DoNothing, UMEntryThunkData::FreeUMEntryThunk> umHolder;
            umHolder.Assign(pUMEntryThunk);

            // multicast. go thru Invoke
            OBJECTHANDLE objhnd = GetAppDomain()->CreateLongWeakHandle(pDelegate);
            _ASSERTE(objhnd != NULL);

            // This target should not ever be used. We are storing it in the thunk for better diagnostics of "call on collected delegate" crashes.
            PCODE pManagedTargetForDiagnostics = (pDelegate->GetMethodPtrAux() != (PCODE)NULL) ? pDelegate->GetMethodPtrAux() : pDelegate->GetMethodPtr();

            // MethodDesc is passed in for profiling to know the method desc of target
            pUMEntryThunk->LoadTimeInit(
                pManagedTargetForDiagnostics,
                objhnd,
                pUMThunkMarshInfo, pInvokeMeth);

            if (!pInteropInfo->SetUMEntryThunk(pUMEntryThunk))
            {
                pUMEntryThunk = pInteropInfo->GetUMEntryThunk();
            }
            else
            {
                umHolder.SuppressRelease();
            }

            _ASSERTE(pUMEntryThunk != NULL);
            _ASSERTE(pUMEntryThunk == pInteropInfo->GetUMEntryThunk());

        }
        pCode = (PCODE)pUMEntryThunk->GetCode();
    }

    GCPROTECT_END();
    return (LPVOID)pCode;
}

// Marshals an unmanaged callback to Delegate
//static
OBJECTREF COMDelegate::ConvertToDelegate(LPVOID pCallback, MethodTable* pMT)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
        PRECONDITION(pCallback != NULL);
        PRECONDITION(pMT != NULL);
    }
    CONTRACTL_END;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Check if this callback was originally a managed method passed out to unmanaged code.
    //

    UMEntryThunk* pUMEntryThunk = NULL;

    auto stubKind = RangeSectionStubManager::GetStubKind((PCODE)pCallback);
    if (stubKind == STUB_CODE_BLOCK_STUBPRECODE)
    {
        Precode* pPrecode = Precode::GetPrecodeFromEntryPoint((PCODE)pCallback);
        if (pPrecode->GetType() == PRECODE_UMENTRY_THUNK)
        {
            pUMEntryThunk = pPrecode->AsUMEntryThunk();
        }
    }


    // Lookup the callsite in the hash, if found, we can map this call back to its managed function.
    // Otherwise, we'll treat this as an unmanaged callsite.
    // Make sure that the pointer doesn't have the value of 1 which is our hash table deleted item marker.
    OBJECTHANDLE DelegateHnd = (pUMEntryThunk != NULL)
        ? pUMEntryThunk->GetData()->GetObjectHandle()
        : (OBJECTHANDLE)NULL;

    if (DelegateHnd != (OBJECTHANDLE)NULL)
    {
        // Found a managed callsite
        return ObjectFromHandle(DelegateHnd);
    }

    // Validate the MethodTable is a delegate type
    // See Marshal.GetDelegateForFunctionPointer() for exception details.
    if (!pMT->IsDelegate())
        COMPlusThrowArgumentException(W("t"), W("Arg_MustBeDelegate"));

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // This is an unmanaged callsite. We need to create a new delegate.
    //
    // The delegate's invoke method will point to a call thunk.
    // The call thunk will internally shuffle the args, set up a DelegateTransitionFrame, marshal the args,
    //  call the UM Function located at m_pAuxField, unmarshal the args, and return.
    // Invoke -> CallThunk -> ShuffleThunk -> Frame -> Marshal -> Call AuxField -> UnMarshal

    DelegateEEClass*    pClass      = (DelegateEEClass*)pMT->GetClass();
    MethodDesc*         pMD         = FindDelegateInvokeMethod(pMT);

    _ASSERTE(pClass != NULL);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Get or create the marshaling stub information
    //

    PCODE pMarshalStub = pClass->m_pMarshalStub;
    if (pMarshalStub == (PCODE)NULL)
    {
        GCX_PREEMP();

        pMarshalStub = GetStubForInteropMethod(pMD);

        // Save this new stub on the DelegateEEClass.
        InterlockedCompareExchangeT<PCODE>(&pClass->m_pMarshalStub, pMarshalStub, (PCODE)NULL);

        pMarshalStub = pClass->m_pMarshalStub;
    }

    // The IL marshaling stub performs the function of the shuffle thunk - it simply omits 'this' in
    // the call to unmanaged code. The stub recovers the unmanaged target from the delegate instance.

    _ASSERTE(pMarshalStub != (PCODE)NULL);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Wire up the stubs to the new delegate instance.
    //

    LOG((LF_INTEROP, LL_INFO10000, "Created delegate for function pointer: entrypoint: %p\n", pMarshalStub));

    // Create the new delegate
    DELEGATEREF delObj = (DELEGATEREF) pMT->Allocate();

    {
        // delObj is not protected
        GCX_NOTRIGGER();

        // Wire up the unmanaged call stub to the delegate.
        delObj->SetTarget(delObj);              // We are the "this" object

        // For X86, we save the entry point in the delegate's method pointer and the UM Callsite in the aux pointer.
        delObj->SetMethodPtr(pMarshalStub);
        delObj->SetMethodPtrAux((PCODE)pCallback);

        // Also, mark this delegate as an unmanaged function pointer wrapper.
        delObj->SetInvocationCount(DELEGATE_MARKER_UNMANAGEDFPTR);
    }

    return delObj;
}

void COMDelegate::ValidateDelegatePInvoke(MethodDesc* pMD)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;

        PRECONDITION(CheckPointer(pMD));
    }
    CONTRACTL_END;

    if (pMD->IsSynchronized())
        COMPlusThrow(kTypeLoadException, IDS_EE_NOSYNCHRONIZED);

    if (pMD->MethodDesc::IsVarArg())
        COMPlusThrow(kNotSupportedException, IDS_EE_VARARG_NOT_SUPPORTED);
}

// static
PCODE COMDelegate::GetStubForILStub(EEImplMethodDesc* pDelegateMD, MethodDesc** ppStubMD, DWORD dwStubFlags)
{
    CONTRACT(PCODE)
    {
        STANDARD_VM_CHECK;

        PRECONDITION(CheckPointer(pDelegateMD));
        POSTCONDITION(RETVAL != NULL);
    }
    CONTRACT_END;

    ValidateDelegatePInvoke(pDelegateMD);

    dwStubFlags |= PINVOKESTUB_FL_DELEGATE;

    RETURN PInvoke::GetStubForILStub(pDelegateMD, ppStubMD, dwStubFlags);
}



// static
MethodDesc* COMDelegate::GetILStubMethodDesc(EEImplMethodDesc* pDelegateMD, DWORD dwStubFlags)
{
    STANDARD_VM_CONTRACT;

    MethodTable *pMT = pDelegateMD->GetMethodTable();

    dwStubFlags |= PINVOKESTUB_FL_DELEGATE;

    PInvokeStaticSigInfo sigInfo(pDelegateMD);
    return PInvoke::CreateCLRToNativeILStub(&sigInfo, dwStubFlags, pDelegateMD);
}

extern "C" void QCALLTYPE Delegate_InitializeVirtualCallStub(QCall::ObjectHandleOnStack d, PCODE method)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    GCX_COOP();

    MethodDesc *pMeth = MethodTable::GetMethodDescForSlotAddress((PCODE)method);
    _ASSERTE(pMeth);
    _ASSERTE(!pMeth->IsStatic() && pMeth->IsVirtual());
    PCODE target = GetVirtualCallStub(pMeth, TypeHandle(pMeth->GetMethodTable()));

    DELEGATEREF refThis = (DELEGATEREF)d.Get();
    refThis->SetMethodPtrAux(target);
    refThis->SetInvocationCount((INT_PTR)(void*)pMeth);

    END_QCALL;
}

extern "C" PCODE QCALLTYPE Delegate_AdjustTarget(QCall::ObjectHandleOnStack target, PCODE method)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    GCX_COOP();

    _ASSERTE(method);

    MethodTable* pMTTarg = target.Get()->GetMethodTable();

    MethodDesc *pMeth = NonVirtualEntry2MethodDesc(method);
    _ASSERTE(pMeth);
    _ASSERTE(!pMeth->IsStatic());

    // close delegates
    MethodTable* pMTMeth = pMeth->GetMethodTable();

    MethodDesc *pCorrectedMethod = pMeth;

    // Use the Unboxing stub for value class methods, since the value
    // class is constructed using the boxed instance.
    if (pCorrectedMethod->GetMethodTable()->IsValueType() && !pCorrectedMethod->IsUnboxingStub())
    {
        // those should have been ruled out at jit time (code:COMDelegate::GetDelegateCtor)
        _ASSERTE((pMTMeth != g_pValueTypeClass) && (pMTMeth != g_pObjectClass));
        pCorrectedMethod->CheckRestore();
        pCorrectedMethod = pMTTarg->GetBoxedEntryPointMD(pCorrectedMethod);
        _ASSERTE(pCorrectedMethod != NULL);
    }

    if (pMeth != pCorrectedMethod)
    {
        method = pCorrectedMethod->GetMultiCallableAddrOfCode();
    }

    END_QCALL;

    return method;
}

#if defined(_MSC_VER) && !defined(TARGET_UNIX)
// VC++ Compiler intrinsic.
extern "C" void * _ReturnAddress(void);
#endif // _MSC_VER && !TARGET_UNIX

uint32_t MethodDescToNumFixedArgs(MethodDesc *pMD)
{
    WRAPPER_NO_CONTRACT;

    SigParser sig = pMD->GetSigParser();

    uint32_t data;
    IfFailThrow(sig.GetCallingConvInfo(&data));
    if (data & IMAGE_CEE_CS_CALLCONV_GENERIC)
    {
        // Skip over generic argument count
        IfFailThrow(sig.GetData(&data));
    }

    // Return argument count
    IfFailThrow(sig.GetData(&data));
    return data;
}

// This is the single constructor for all Delegates. The compiler
// doesn't provide an implementation of the Delegate constructor. We
// provide that implementation through a QCall call to this method.
extern "C" void QCALLTYPE Delegate_Construct(QCall::ObjectHandleOnStack _this, QCall::ObjectHandleOnStack target, PCODE method)
{
    QCALL_CONTRACT;

    // If you modify this logic, please update DacDbiInterfaceImpl::GetDelegateType, DacDbiInterfaceImpl::GetDelegateType,
    // DacDbiInterfaceImpl::GetDelegateFunctionData, and DacDbiInterfaceImpl::GetDelegateTargetObject.

    _ASSERTE(method != (PCODE)NULL);
    BEGIN_QCALL;

    GCX_COOP();

    DELEGATEREF refThis = (DELEGATEREF) ObjectToOBJECTREF(_this.Get());
    _ASSERTE(refThis != NULL);

    GCPROTECT_BEGIN(refThis);

    // Programmers could feed garbage data to DelegateConstruct().
    // It's difficult to validate a method code pointer, but at least we'll
    // try to catch the easy garbage.
    _ASSERTE(isMemoryReadable(method, 1));

    MethodTable* pMTTarg = NULL;
    if (target.Get() != NULL)
        pMTTarg = target.Get()->GetMethodTable();

    MethodTable* pDelMT = refThis->GetMethodTable();
    MethodDesc* pMethOrig = NonVirtualEntry2MethodDesc(method);
    MethodDesc* pMeth = pMethOrig;
    _ASSERTE(pMeth != NULL);

    LOG((LF_STUBS, LL_INFO1000, "In DelegateConstruct: for delegate type %s binding to method %s::%s%s, static = %d\n",
         pDelMT->GetDebugClassName(),
         pMeth->m_pszDebugClassName, pMeth->m_pszDebugMethodName, pMeth->m_pszDebugMethodSignature, pMeth->IsStatic()));

#ifdef _DEBUG
    // Assert that everything is OK...This is not some bogus
    //  address...Very unlikely that the code below would work
    //  for a random address in memory....
    MethodTable* p = pMeth->GetMethodTable();
    _ASSERTE(p != NULL);
    _ASSERTE(p->ValidateWithPossibleAV());
#endif // _DEBUG

    if (Nullable::IsNullableType(pMeth->GetMethodTable()))
        COMPlusThrow(kNotSupportedException);

    MethodDesc* pDelegateInvoke = COMDelegate::FindDelegateInvokeMethod(pDelMT);

    UINT invokeArgCount = MethodDescToNumFixedArgs(pDelegateInvoke);
    UINT methodArgCount = MethodDescToNumFixedArgs(pMeth);
    BOOL isStatic = pMeth->IsStatic();
    if (!isStatic)
        methodArgCount++; // count 'this'

    if (COMDelegate::NeedsWrapperDelegate(pMeth))
        refThis = COMDelegate::CreateWrapperDelegate(refThis, pMeth);

    if (pMeth->GetLoaderAllocator()->IsCollectible())
        refThis->SetMethodBase(pMeth->GetLoaderAllocator()->GetExposedObject());

    // Open delegates.
    if (invokeArgCount == methodArgCount)
    {
        // set the target
        refThis->SetTarget(refThis);

        // set the shuffle thunk
        PCODE pEntryPoint = SetupShuffleThunk(pDelMT, pMeth);
        refThis->SetMethodPtr(pEntryPoint);

        // set the ptr aux according to what is needed, if virtual need to call make virtual stub dispatch
        if (!pMeth->IsStatic() && pMeth->IsVirtual() && !pMeth->GetMethodTable()->IsValueType())
        {
            PCODE pTargetCall = GetVirtualCallStub(pMeth, TypeHandle(pMeth->GetMethodTable()));
            refThis->SetMethodPtrAux(pTargetCall);
            refThis->SetInvocationCount((INT_PTR)(void *)pMeth);
        }
        else
        {
            refThis->SetMethodPtrAux(method);
        }
    }
    else
    {
        MethodTable* pMTMeth = pMeth->GetMethodTable();

        if (!pMeth->IsStatic())
        {
            if (target.Get() == NULL)
                COMPlusThrow(kArgumentException, W("Arg_DlgtNullInst"));

            if (pMTTarg != NULL)
            {
                // Use the Unboxing stub for value class methods, since the value
                // class is constructed using the boxed instance.
                //
                // <NICE> We could get the JIT to recognise all delegate creation sequences and
                // ensure the thing is always an BoxedEntryPointStub anyway </NICE>

                if (pMTMeth->IsValueType() && !pMeth->IsUnboxingStub())
                {
                    // If these are Object/ValueType.ToString().. etc,
                    // don't need an unboxing Stub.

                    if ((pMTMeth != g_pValueTypeClass)
                        && (pMTMeth != g_pObjectClass))
                    {
                        pMeth->CheckRestore();
                        pMeth = pMTTarg->GetBoxedEntryPointMD(pMeth);
                        _ASSERTE(pMeth != NULL);
                    }
                }
                // Only update the code address if we've decided to go to a different target...
                // <NICE> We should make sure the code address that the JIT provided to us is always the right one anyway,
                // so we don't have to do all this mucking about. </NICE>
                if (pMeth != pMethOrig)
                {
                    method = pMeth->GetMultiCallableAddrOfCode();
                }
            }
        }
#ifdef HAS_THISPTR_RETBUF_PRECODE
        else if (pMeth->HasRetBuffArg() && IsRetBuffPassedAsFirstArg())
        {
            method = pMeth->GetLoaderAllocator()->GetFuncPtrStubs()->GetFuncPtrStub(pMeth, PRECODE_THISPTR_RETBUF);
        }
#endif // HAS_THISPTR_RETBUF_PRECODE

        refThis->SetTarget(target.Get());
        refThis->SetMethodPtr((PCODE)(void *)method);
    }

    GCPROTECT_END();
    END_QCALL;
}

MethodDesc *COMDelegate::GetMethodDescForOpenVirtualDelegate(OBJECTREF orDelegate)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    return (MethodDesc*)((DELEGATEREF)orDelegate)->GetInvocationCount();
}

MethodDesc *COMDelegate::GetMethodDesc(OBJECTREF orDelegate)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    // If you modify this logic, please update DacDbiInterfaceImpl::GetDelegateType, DacDbiInterfaceImpl::GetDelegateType,
    // DacDbiInterfaceImpl::GetDelegateFunctionData, and DacDbiInterfaceImpl::GetDelegateTargetObject.

    MethodDesc *pMethodHandle = NULL;

    DELEGATEREF thisDel = (DELEGATEREF) orDelegate;
    DELEGATEREF innerDel = NULL;

    INT_PTR count = thisDel->GetInvocationCount();
    if (count != 0)
    {
        // this is one of the following:
        // - multicast - _invocationList is Array && _invocationCount != 0
        // - unamanaged ftn ptr - _invocationList == NULL && _invocationCount == -1
        // - wrapper delegate - _invocationList is Delegate && _invocationCount != NULL
        // - virtual delegate - _invocationList == null && _invocationCount == (target MethodDesc)
        //                    or _invocationList points to a LoaderAllocator/DynamicResolver (inner open virtual delegate of a Wrapper Delegate)
        // in the wrapper delegate case we want to unwrap and return the method desc of the inner delegate
        // in the other cases we return the method desc for the invoke
        innerDel = (DELEGATEREF) thisDel->GetInvocationList();
        bool fOpenVirtualDelegate = false;

        if (innerDel != NULL)
        {
            MethodTable *pMT = innerDel->GetMethodTable();
            if (pMT->IsDelegate())
                return GetMethodDesc(innerDel);
            if (!pMT->IsArray())
            {
                // must be a virtual one
                fOpenVirtualDelegate = true;
            }
        }
        else
        {
            if (count != DELEGATE_MARKER_UNMANAGEDFPTR)
            {
                // must be a virtual one
                fOpenVirtualDelegate = true;
            }
        }

        if (fOpenVirtualDelegate)
            pMethodHandle = GetMethodDescForOpenVirtualDelegate(thisDel);
        else
            pMethodHandle = FindDelegateInvokeMethod(thisDel->GetMethodTable());
    }
    else
    {
        // Next, check for an open delegate
        PCODE code = thisDel->GetMethodPtrAux();

        if (code != (PCODE)NULL)
        {
            // Note that MethodTable::GetMethodDescForSlotAddress is significantly faster than Entry2MethodDesc
            pMethodHandle = MethodTable::GetMethodDescForSlotAddress(code);
        }
        else
        {
            MethodTable * pMT = NULL;

            // Must be a normal delegate
            code = thisDel->GetMethodPtr();

            pMethodHandle = NonVirtualEntry2MethodDesc(code);
        }
    }

    _ASSERTE(pMethodHandle);
    return pMethodHandle;
}

OBJECTREF COMDelegate::GetTargetObject(OBJECTREF obj)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    OBJECTREF targetObject = NULL;

    DELEGATEREF thisDel = (DELEGATEREF) obj;
    OBJECTREF innerDel = NULL;

    if (thisDel->GetInvocationCount() != 0)
    {
        // this is one of the following:
        // - multicast
        // - unmanaged ftn ptr
        // - wrapper delegate
        // - virtual delegate - _invocationList == null && _invocationCount == (target MethodDesc)
        //                    or _invocationList points to a LoaderAllocator/DynamicResolver (inner open virtual delegate of a Wrapper Delegate)
        // in the wrapper delegate case we want to unwrap and return the object of the inner delegate
        innerDel = (DELEGATEREF) thisDel->GetInvocationList();
        if (innerDel != NULL)
        {
            MethodTable *pMT = innerDel->GetMethodTable();
            if (pMT->IsDelegate())
            {
                targetObject = GetTargetObject(innerDel);
            }
        }
    }

    if (targetObject == NULL)
        targetObject = thisDel->GetTarget();

    return targetObject;
}

BOOL COMDelegate::IsTrueMulticastDelegate(OBJECTREF delegate)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    BOOL isMulticast = FALSE;

    size_t invocationCount = ((DELEGATEREF)delegate)->GetInvocationCount();
    if (invocationCount)
    {
        OBJECTREF invocationList = ((DELEGATEREF)delegate)->GetInvocationList();
        if (invocationList != NULL)
        {
            MethodTable *pMT = invocationList->GetMethodTable();
            isMulticast = pMT->IsArray();
        }
    }

    return isMulticast;
}

// Get the cpu stub for a delegate invoke.
Stub* COMDelegate::GetInvokeMethodStub(EEImplMethodDesc* pMD)
{
    STANDARD_VM_CONTRACT;

    MethodTable *       pDelMT = pMD->GetMethodTable();
    DelegateEEClass*    pClass = (DelegateEEClass*) pDelMT->GetClass();

    if (pMD == pClass->GetInvokeMethod())
    {
        // Validate the invoke method, which at the moment just means checking the calling convention

        MetaSig sig(pMD);

        unsigned callConv = sig.GetCallingConventionInfo();
        if (callConv != (IMAGE_CEE_CS_CALLCONV_HASTHIS | IMAGE_CEE_CS_CALLCONV_DEFAULT))
            COMPlusThrow(kInvalidProgramException);

        BOOL fReturnVal = !sig.IsReturnTypeVoid();

        SigTypeContext emptyContext;
        ILStubLinker sl(pMD->GetModule(), pMD->GetSignature(), &emptyContext, pMD, (ILStubLinkerFlags)(ILSTUB_LINKER_FLAG_STUB_HAS_THIS | ILSTUB_LINKER_FLAG_TARGET_HAS_THIS));

        ILCodeStream *pCode = sl.NewCodeStream(ILStubLinker::kDispatch);

        // This stub is only used for rare indirect cases, for example
        // when Delegate.Invoke method is wrapped into another delegate.
        // Direct invocation of delegate is expanded by JIT.
        // Emit a recursive call here to let JIT handle complex cases like
        // virtual dispatch and GC safety.

        // Load the delegate object
        pCode->EmitLoadThis();

        // Load the arguments
        for (UINT paramCount = 0; paramCount < sig.NumFixedArgs(); paramCount++)
            pCode->EmitLDARG(paramCount);

        // recursively call the delegate itself
        pCode->EmitCALL(pCode->GetToken(pMD), sig.NumFixedArgs(), fReturnVal);

        // return
        pCode->EmitRET();

        PCCOR_SIGNATURE pSig;
        DWORD cbSig;
        pMD->GetSig(&pSig,&cbSig);

        MethodDesc* pStubMD = ILStubCache::CreateAndLinkNewILStubMethodDesc(pMD->GetLoaderAllocator(),
                                                                pMD->GetMethodTable(),
                                                                ILSTUB_DELEGATE_INVOKE_METHOD,
                                                                pMD->GetModule(),
                                                                pSig, cbSig,
                                                                NULL,
                                                                &sl);

        return Stub::NewStub(JitILStub(pStubMD));
    }
    else
    {
        // We do not support asynchronous delegates in CoreCLR
        COMPlusThrow(kPlatformNotSupportedException);
    }
}

void COMDelegate::ThrowIfInvalidUnmanagedCallersOnlyUsage(MethodDesc* pMD)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        PRECONDITION(pMD != NULL);
        PRECONDITION(pMD->HasUnmanagedCallersOnlyAttribute());
    }
    CONTRACTL_END;

    if (!pMD->IsStatic())
        EX_THROW(EEResourceException, (kInvalidProgramException, W("InvalidProgram_NonStaticMethod")));

    // No generic methods
    if (pMD->HasClassOrMethodInstantiation())
        EX_THROW(EEResourceException, (kInvalidProgramException, W("InvalidProgram_GenericMethod")));

    // Arguments - Scenarios involving UnmanagedCallersOnly are handled during the jit.
    bool unmanagedCallersOnlyRequiresMarshalling = false;
    if (PInvoke::MarshalingRequired(pMD, NULL, NULL, NULL, unmanagedCallersOnlyRequiresMarshalling))
        EX_THROW(EEResourceException, (kInvalidProgramException, W("InvalidProgram_NonBlittableTypes")));
}

BOOL COMDelegate::NeedsWrapperDelegate(MethodDesc* pTargetMD)
{
    LIMITED_METHOD_CONTRACT;

#ifdef TARGET_ARM
    // For arm VSD expects r4 to contain the indirection cell. However r4 is a non-volatile register
    // and its value must be preserved. So we need to erect a frame and store indirection cell in r4 before calling
    // virtual stub dispatch. Erecting frame is already done by wrapper delegates so the Wrapper Delegate infrastructure
    //  can easliy be used for our purpose.
    // set needsWrapperDelegate flag in order to erect a frame. (Wrapper Delegate stub also loads the right value in r4)
    if (!pTargetMD->IsStatic() && pTargetMD->IsVirtual() && !pTargetMD->GetMethodTable()->IsValueType())
        return TRUE;
#endif

     return FALSE;
}



// to create a wrapper delegate wrapper we need:
// - the delegate to forward to         -> _invocationList
// - the delegate invoke MethodDesc     -> _count
// the 2 fields used for invocation will contain:
// - the delegate itself                -> _pORField
// - the wrapper stub                    -> _pFPField
DELEGATEREF COMDelegate::CreateWrapperDelegate(DELEGATEREF delegate, MethodDesc* pTargetMD)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    MethodTable *pDelegateType = delegate->GetMethodTable();
    MethodDesc *pMD = ((DelegateEEClass*)(pDelegateType->GetClass()))->GetInvokeMethod();
    // allocate the object
    struct _gc {
        DELEGATEREF refWrapperDel;
        DELEGATEREF innerDel;
    } gc;
    gc.refWrapperDel = delegate;
    gc.innerDel = NULL;

    GCPROTECT_BEGIN(gc);

    // set the proper fields
    //

    // Object reference field...
    gc.refWrapperDel->SetTarget(gc.refWrapperDel);

    // save the secure invoke stub.  GetWrapperInvoke() can trigger GC.
    PCODE tmp = GetWrapperInvoke(pMD);
    gc.refWrapperDel->SetMethodPtr(tmp);
    // save the delegate MethodDesc for the frame
    gc.refWrapperDel->SetInvocationCount((INT_PTR)pMD);

    // save the delegate to forward to
    gc.innerDel = (DELEGATEREF) pDelegateType->Allocate();
    gc.refWrapperDel->SetInvocationList(gc.innerDel);

    GCPROTECT_END();

    return gc.innerDel;
}

// This method will get the MethodInfo for a delegate
extern "C" void QCALLTYPE Delegate_FindMethodHandle(QCall::ObjectHandleOnStack d, QCall::ObjectHandleOnStack retMethodInfo)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    GCX_COOP();

    MethodDesc* pMD = COMDelegate::GetMethodDesc(d.Get());
    pMD = MethodDesc::FindOrCreateAssociatedMethodDescForReflection(pMD, TypeHandle(pMD->GetMethodTable()), pMD->GetMethodInstantiation());
    retMethodInfo.Set(pMD->AllocateStubMethodInfo());

    END_QCALL;
}

extern "C" BOOL QCALLTYPE Delegate_InternalEqualMethodHandles(QCall::ObjectHandleOnStack left, QCall::ObjectHandleOnStack right)
{
    QCALL_CONTRACT;

    BOOL fRet = FALSE;

    BEGIN_QCALL;

    GCX_COOP();

    MethodDesc* pMDLeft = COMDelegate::GetMethodDesc(left.Get());
    MethodDesc* pMDRight = COMDelegate::GetMethodDesc(right.Get());
    fRet = pMDLeft == pMDRight;

    END_QCALL;

    return fRet;
}

FCIMPL1(MethodDesc*, COMDelegate::GetInvokeMethod, MethodTable* pDelegateMT)
{
    FCALL_CONTRACT;
    _ASSERTE(pDelegateMT != NULL);

    MethodDesc* pMD = ((DelegateEEClass*)(pDelegateMT->GetClass()))->GetInvokeMethod();
    _ASSERTE(pMD != NULL);
    return pMD;
}
FCIMPLEND

FCIMPL1(PCODE, COMDelegate::GetMulticastInvoke, MethodTable* pDelegateMT)
{
    FCALL_CONTRACT;
    _ASSERTE(pDelegateMT != NULL);

    DelegateEEClass* delegateEEClass = (DelegateEEClass*)pDelegateMT->GetClass();
    return delegateEEClass->m_pMultiCastInvokeStub;
}
FCIMPLEND

extern "C" PCODE QCALLTYPE Delegate_GetMulticastInvokeSlow(MethodTable* pDelegateMT)
{
    QCALL_CONTRACT;
    _ASSERTE(pDelegateMT != NULL);

    PCODE pStub = (PCODE)NULL;

    BEGIN_QCALL;

    DelegateEEClass *delegateEEClass = (DelegateEEClass*)pDelegateMT->GetClass();
    pStub = delegateEEClass->m_pMultiCastInvokeStub;
    if (pStub == (PCODE)NULL)
    {
        MethodDesc* pMD = delegateEEClass->GetInvokeMethod();

        MetaSig sig(pMD);

        BOOL fReturnVal = !sig.IsReturnTypeVoid();

        SigTypeContext emptyContext;
        ILStubLinker sl(pMD->GetModule(), pMD->GetSignature(), &emptyContext, pMD, (ILStubLinkerFlags)(ILSTUB_LINKER_FLAG_STUB_HAS_THIS | ILSTUB_LINKER_FLAG_TARGET_HAS_THIS));

        ILCodeStream *pCode = sl.NewCodeStream(ILStubLinker::kDispatch);

        DWORD dwLoopCounterNum = pCode->NewLocal(ELEMENT_TYPE_I4);

        DWORD dwReturnValNum = -1;
        if (fReturnVal)
            dwReturnValNum = pCode->NewLocal(sig.GetRetTypeHandleNT());

        ILCodeLabel *nextDelegate = pCode->NewCodeLabel();

        // initialize counter
        pCode->EmitLDC(0);
        pCode->EmitSTLOC(dwLoopCounterNum);

        //Label_nextDelegate:
        pCode->EmitLabel(nextDelegate);

#ifdef DEBUGGING_SUPPORTED
        ILCodeLabel *invokeTraceHelper = pCode->NewCodeLabel();
        ILCodeLabel *debuggerCheckEnd = pCode->NewCodeLabel();

        // Call MulticastDebuggerTraceHelper only if we have a controller subscribing to the event
        pCode->EmitLDC((DWORD_PTR)&g_multicastDelegateTraceActiveCount);
        pCode->EmitCONV_I();
        pCode->EmitLDIND_I4();
        // g_multicastDelegateTraceActiveCount != 0
        pCode->EmitLDC(0);
        pCode->EmitCEQ();
        pCode->EmitBRFALSE(invokeTraceHelper);

        pCode->EmitLabel(debuggerCheckEnd);
#endif // DEBUGGING_SUPPORTED

        // Load next delegate from array using LoopCounter as index
        pCode->EmitLoadThis();
        pCode->EmitLDFLD(pCode->GetToken(CoreLibBinder::GetField(FIELD__MULTICAST_DELEGATE__INVOCATION_LIST)));
        pCode->EmitLDLOC(dwLoopCounterNum);
        pCode->EmitLDELEM_REF();

        // Load the arguments
        for (UINT paramCount = 0; paramCount < sig.NumFixedArgs(); paramCount++)
            pCode->EmitLDARG(paramCount);

        // call the delegate
        pCode->EmitCALL(pCode->GetToken(pMD), sig.NumFixedArgs(), fReturnVal);

        // Save return value.
        if (fReturnVal)
            pCode->EmitSTLOC(dwReturnValNum);

        // increment counter
        pCode->EmitLDLOC(dwLoopCounterNum);
        pCode->EmitLDC(1);
        pCode->EmitADD();
        pCode->EmitSTLOC(dwLoopCounterNum);

        // compare LoopCounter with InvocationCount. If less then branch to nextDelegate
        pCode->EmitLDLOC(dwLoopCounterNum);
        pCode->EmitLoadThis();
        pCode->EmitLDFLD(pCode->GetToken(CoreLibBinder::GetField(FIELD__MULTICAST_DELEGATE__INVOCATION_COUNT)));
        pCode->EmitBLT(nextDelegate);

        // load the return value. return value from the last delegate call is returned
        if (fReturnVal)
            pCode->EmitLDLOC(dwReturnValNum);

        // return
        pCode->EmitRET();

#ifdef DEBUGGING_SUPPORTED
        // Emit debugging support at the end of the method for better perf
        pCode->EmitLabel(invokeTraceHelper);

        pCode->EmitLoadThis();
        pCode->EmitLDLOC(dwLoopCounterNum);
        pCode->EmitCALL(METHOD__STUBHELPERS__MULTICAST_DEBUGGER_TRACE_HELPER, 2, 0);

        pCode->EmitBR(debuggerCheckEnd);
#endif // DEBUGGING_SUPPORTED

        PCCOR_SIGNATURE pSig;
        DWORD cbSig;
        pMD->GetSig(&pSig, &cbSig);

        MethodDesc* pStubMD = ILStubCache::CreateAndLinkNewILStubMethodDesc(pMD->GetLoaderAllocator(),
                                                               pMD->GetMethodTable(),
                                                               ILSTUB_MULTICASTDELEGATE_INVOKE,
                                                               pMD->GetModule(),
                                                               pSig, cbSig,
                                                               NULL,
                                                               &sl);
        pStub = JitILStub(pStubMD);

        InterlockedCompareExchangeT<PCODE>(&delegateEEClass->m_pMultiCastInvokeStub, pStub, (PCODE)NULL);
        pStub = delegateEEClass->m_pMultiCastInvokeStub;
    }

    END_QCALL;

    return pStub;
}

PCODE COMDelegate::GetWrapperInvoke(MethodDesc* pMD)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    MethodTable *       pDelegateMT = pMD->GetMethodTable();
    DelegateEEClass*    delegateEEClass = (DelegateEEClass*) pDelegateMT->GetClass();
    PCODE pStub = delegateEEClass->m_pWrapperDelegateInvokeStub;

    if (pStub == (PCODE)NULL)
    {
        GCX_PREEMP();

        MetaSig sig(pMD);

        BOOL fReturnVal = !sig.IsReturnTypeVoid();

        SigTypeContext emptyContext;
        ILStubLinker sl(pMD->GetModule(), pMD->GetSignature(), &emptyContext, pMD, (ILStubLinkerFlags)(ILSTUB_LINKER_FLAG_STUB_HAS_THIS | ILSTUB_LINKER_FLAG_TARGET_HAS_THIS));

        ILCodeStream *pCode = sl.NewCodeStream(ILStubLinker::kDispatch);

        // Load the "real" delegate
        pCode->EmitLoadThis();
        pCode->EmitLDFLD(pCode->GetToken(CoreLibBinder::GetField(FIELD__MULTICAST_DELEGATE__INVOCATION_LIST)));

        // Load the arguments
        UINT paramCount = 0;
        while(paramCount < sig.NumFixedArgs())
            pCode->EmitLDARG(paramCount++);

        // Call the delegate
        pCode->EmitCALL(pCode->GetToken(pMD), sig.NumFixedArgs(), fReturnVal);

        // Return
        pCode->EmitRET();

        PCCOR_SIGNATURE pSig;
        DWORD cbSig;

        pMD->GetSig(&pSig,&cbSig);

        MethodDesc* pStubMD =
            ILStubCache::CreateAndLinkNewILStubMethodDesc(pMD->GetLoaderAllocator(),
                                                          pMD->GetMethodTable(),
                                                          ILSTUB_WRAPPERDELEGATE_INVOKE,
                                                          pMD->GetModule(),
                                                          pSig, cbSig,
                                                          NULL,
                                                          &sl);

        pStub = JitILStub(pStubMD);

        InterlockedCompareExchangeT<PCODE>(&delegateEEClass->m_pWrapperDelegateInvokeStub, pStub, (PCODE)NULL);
        pStub = delegateEEClass->m_pWrapperDelegateInvokeStub;
    }
    return pStub;
}



static bool IsLocationAssignable(TypeHandle fromHandle, TypeHandle toHandle, bool relaxedMatch, bool fromHandleIsBoxed)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;
    // Identical types are obviously compatible.
    if (fromHandle == toHandle)
        return true;

    // Byref parameters can never be allowed relaxed matching since type safety will always be violated in one
    // of the two directions (in or out). Checking one of the types is enough since a byref type is never
    // compatible with a non-byref type.
    if (fromHandle.IsByRef())
        relaxedMatch = false;

    // If we allow relaxed matching then any subtype of toHandle is probably
    // compatible (definitely so if we know fromHandle is coming from a boxed
    // value such as we get from the bound argument in a closed delegate).
    if (relaxedMatch && fromHandle.CanCastTo(toHandle))
    {
        // If the fromHandle isn't boxed then we need to be careful since
        // non-object reference arguments aren't going to be compatible with
        // object reference locations (there's no implicit boxing going to happen
        // for us).
        if (!fromHandleIsBoxed)
        {
            // Check that the "objrefness" of source and destination matches. In
            // reality there are only three objref classes that would have
            // passed the CanCastTo above given a value type source (Object,
            // ValueType and Enum), but why hard code these in when we can be
            // more robust?
            if (fromHandle.IsGenericVariable())
            {
                TypeVarTypeDesc *fromHandleVar = fromHandle.AsGenericVariable();

                // We need to check whether constraints of fromHandle have been loaded, because the
                // CanCastTo operation might have made its decision without enumerating constraints
                // (e.g. when toHandle is System.Object).
                if (!fromHandleVar->ConstraintsLoaded())
                    fromHandleVar->LoadConstraints(CLASS_DEPENDENCIES_LOADED);

                if (toHandle.IsGenericVariable())
                {
                    TypeVarTypeDesc *toHandleVar = toHandle.AsGenericVariable();

                    // Constraints of toHandleVar were not touched by CanCastTo.
                    if (!toHandleVar->ConstraintsLoaded())
                        toHandleVar->LoadConstraints(CLASS_DEPENDENCIES_LOADED);

                    // Both handles are type variables. The following table lists all possible combinations.
                    //
                    // In brackets are results of IsConstrainedAsObjRef/IsConstrainedAsValueType
                    //
                    //            To:| [FALSE/FALSE]         | [FALSE/TRUE]          | [TRUE/FALSE]
                    // From:         |                       |                       |
                    // --------------------------------------------------------------------------------------
                    // [FALSE/FALSE] | ERROR                 | NEVER HAPPENS         | ERROR
                    //               | we know nothing       |                       | From may be a VT
                    // --------------------------------------------------------------------------------------
                    // [FALSE/TRUE]  | ERROR                 | OK                    | ERROR
                    //               | To may be an ObjRef   | both are VT           | mismatch
                    // --------------------------------------------------------------------------------------
                    // [TRUE/FALSE]  | OK (C# compat)        | ERROR - mismatch and  | OK
                    //               | (*)                   | no such instantiation | both are ObjRef
                    // --------------------------------------------------------------------------------------

                    if (fromHandleVar->ConstrainedAsObjRef())
                    {
                        // (*) Normally we would need to check whether toHandleVar is also constrained
                        // as ObjRef here and fail if it's not. However, the C# compiler currently
                        // allows the toHandleVar constraint to be omitted and infers it. We have to
                        // follow the same rule to avoid introducing a breaking change.
                        //
                        // Example:
                        // class Gen<T, U> where T : class, U
                        //
                        // For the sake of delegate co(ntra)variance, U is also regarded as being
                        // constrained as ObjRef even though it has no constraints.

                        if (toHandleVar->ConstrainedAsValueType())
                        {
                            // reference type / value type mismatch
                            return FALSE;
                        }
                    }
                    else
                    {
                        if (toHandleVar->ConstrainedAsValueType())
                        {
                            // If toHandleVar is constrained as value type, fromHandle must be as well.
                            _ASSERTE(fromHandleVar->ConstrainedAsValueType());
                        }
                        else
                        {
                            // It was not possible to prove that the variables are both reference types
                            // or both value types.
                            return false;
                        }
                    }
                }
                else
                {
                    // We need toHandle to be an ObjRef and fromHandle to be constrained as ObjRef,
                    // or toHandle to be a value type and fromHandle to be constrained as a value
                    // type (which must be this specific value type actually as value types are sealed).

                    // Constraints of fromHandle must ensure that it will be ObjRef if toHandle is an
                    // ObjRef, and a value type if toHandle is not an ObjRef.
                    if (CorTypeInfo::IsObjRef_NoThrow(toHandle.GetInternalCorElementType()))
                    {
                        if (!fromHandleVar->ConstrainedAsObjRef())
                            return false;
                    }
                    else
                    {
                        if (!fromHandleVar->ConstrainedAsValueType())
                            return false;
                    }
                }
            }
            else
            {
                _ASSERTE(!toHandle.IsGenericVariable());

                // The COR element types have all the information we need.
                if (CorTypeInfo::IsObjRef_NoThrow(fromHandle.GetInternalCorElementType()) !=
                    CorTypeInfo::IsObjRef_NoThrow(toHandle.GetInternalCorElementType()))
                    return false;
            }
        }

        return true;
    }
    else
    {
        // they are not compatible yet enums can go into each other if their underlying element type is the same
        if (toHandle.GetVerifierCorElementType() == fromHandle.GetVerifierCorElementType()
            && (toHandle.IsEnum() || fromHandle.IsEnum()))
            return true;

    }

    return false;
}

MethodDesc* COMDelegate::FindDelegateInvokeMethod(MethodTable *pMT)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    _ASSERTE(pMT->IsDelegate());

    MethodDesc * pMD = ((DelegateEEClass*)pMT->GetClass())->GetInvokeMethod();
    if (pMD == NULL)
        COMPlusThrowNonLocalized(kMissingMethodException, W("Invoke"));
    return pMD;
}

BOOL COMDelegate::IsDelegateInvokeMethod(MethodDesc *pMD)
{
    LIMITED_METHOD_CONTRACT;

    MethodTable *pMT = pMD->GetMethodTable();
    _ASSERTE(pMT->IsDelegate());

    return (pMD == ((DelegateEEClass *)pMT->GetClass())->GetInvokeMethod());
}

bool COMDelegate::IsMethodDescCompatible(TypeHandle   thFirstArg,
                                         TypeHandle   thExactMethodType,
                                         MethodDesc  *pTargetMethod,
                                         TypeHandle   thDelegate,
                                         MethodDesc  *pInvokeMethod,
                                         int          flags,
                                         bool        *pfIsOpenDelegate)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    // Handle easy cases first -- if there's a constraint on whether the target method is static or instance we can check that very
    // quickly.
    if (flags & DBF_StaticMethodOnly && !pTargetMethod->IsStatic())
        return false;
    if (flags & DBF_InstanceMethodOnly && pTargetMethod->IsStatic())
        return false;

    // Get signatures for the delegate invoke and target methods.
    MetaSig sigInvoke(pInvokeMethod, thDelegate);
    MetaSig sigTarget(pTargetMethod, thExactMethodType);

    // Check that there is no vararg mismatch.
    if (sigInvoke.IsVarArg() != sigTarget.IsVarArg())
        return false;

    // The relationship between the number of arguments on the delegate invoke and target methods tells us a lot about the type of
    // delegate we'll create (open or closed over the first argument). We're getting the fixed argument counts here, which are all
    // the arguments apart from any implicit 'this' pointers.
    // On the delegate invoke side (the caller) the total number of arguments is the number of fixed args to Invoke plus one if the
    // delegate is closed over an argument (i.e. that argument is provided at delegate creation time).
    // On the target method side (the callee) the total number of arguments is the number of fixed args plus one if the target is an
    // instance method.
    // These two totals should match for any compatible delegate and target method.
    UINT numFixedInvokeArgs = sigInvoke.NumFixedArgs();
    UINT numFixedTargetArgs = sigTarget.NumFixedArgs();
    UINT numTotalTargetArgs = numFixedTargetArgs + (pTargetMethod->IsStatic() ? 0 : 1);

    // Determine whether the match (if it is otherwise compatible) would result in an open or closed delegate or is just completely
    // out of whack.
    bool fIsOpenDelegate;
    if (numTotalTargetArgs == numFixedInvokeArgs)
        // All arguments provided by invoke, delegate must be open.
        fIsOpenDelegate = true;
    else if (numTotalTargetArgs == numFixedInvokeArgs + 1)
        // One too few arguments provided by invoke, delegate must be closed.
        fIsOpenDelegate = false;
    else
        // Target method cannot possibly match the invoke method.
        return false;

    // Deal with cases where the caller wants a specific type of delegate.
    if (flags & DBF_OpenDelegateOnly && !fIsOpenDelegate)
        return false;
    if (flags & DBF_ClosedDelegateOnly && fIsOpenDelegate)
        return false;

    // If the target (or first argument) is null, the delegate type would be closed and the caller explicitly doesn't want to allow
    // closing over null then filter that case now.
    if (flags & DBF_NeverCloseOverNull && thFirstArg.IsNull() && !fIsOpenDelegate)
        return false;

    // If, on the other hand, we're looking at an open delegate but the caller has provided a target it's also not a match.
    if (fIsOpenDelegate && !thFirstArg.IsNull())
        return false;

    // **********OLD COMMENT**********
    // We don't allow open delegates over virtual value type methods. That's because we currently have no way to allow the first
    // argument of the invoke method to be specified in such a way that the passed value would be both compatible with the target
    // method and type safe. Virtual methods always have an objref instance (they depend on this for the vtable lookup algorithm) so
    // we can't take a Foo& first argument like other value type methods. We also can't accept System.Object or System.ValueType in
    // the invoke signature since that's not specific enough and would allow type safety violations.
    // Someday we may invent a boxing stub which would take a Foo& passed in box it before dispatch. This is unlikely given that
    // it's a lot of work for an edge case (especially considering that open delegates over value types are always going to be
    // tightly bound to the specific value type). It would also be an odd case where merely calling a delegate would involve an
    // allocation and thus potential failure before you even entered the method.
    // So for now we simply disallow this case.
    // **********OLD COMMENT END**********
    // Actually we allow them now. We will treat them like non-virtual methods.


    // If we get here the basic shape of the signatures match up for either an open or closed delegate. Now we need to verify that
    // those signatures are type compatible. This is complicated somewhat by the matrix of delegate type to target method types
    // (open static vs closed instance etc.). Where we get the first argument type on the invoke side is controlled by open vs
    // closed: closed delegates get the type from the target, open from the first invoke method argument (which is always a fixed
    // arg). Similarly the location of the first argument type on the target method side is based on static vs instance (static from
    // the first fixed arg, instance from the type of the method).

    TypeHandle thFirstInvokeArg;
    TypeHandle thFirstTargetArg;

    // There is one edge case for an open static delegate which takes no arguments. In that case we're nearly done, just compare the
    // return types.
    if (numTotalTargetArgs == 0)
    {
        _ASSERTE(pTargetMethod->IsStatic());
        _ASSERTE(fIsOpenDelegate);

        goto CheckReturnType;
    }

    // Invoke side first...
    if (fIsOpenDelegate)
    {
        // No bound arguments, take first type from invoke signature.
        if (sigInvoke.NextArgNormalized() == ELEMENT_TYPE_END)
            return false;
        thFirstInvokeArg = sigInvoke.GetLastTypeHandleThrowing();
    }
    else
        // We have one bound argument and the type of that is what we must compare first.
        thFirstInvokeArg = thFirstArg;

    // And now the first target method argument for comparison...
    if (pTargetMethod->IsStatic())
    {
        // The first argument for a static method is the first fixed arg.
        if (sigTarget.NextArgNormalized() == ELEMENT_TYPE_END)
            return false;
        thFirstTargetArg = sigTarget.GetLastTypeHandleThrowing();

        // Delegates closed over static methods have a further constraint: the first argument of the target must be an object
        // reference type (otherwise the argument shuffling logic could get complicated).
        if (!fIsOpenDelegate)
        {
            if (thFirstTargetArg.IsGenericVariable())
            {
                // If the first argument of the target is a generic variable, it must be constrained to be an object reference.
                TypeVarTypeDesc *varFirstTargetArg = thFirstTargetArg.AsGenericVariable();
                if (!varFirstTargetArg->ConstrainedAsObjRef())
                    return false;
            }
            else
            {
                // Otherwise the code:CorElementType of the argument must be classified as an object reference.
                CorElementType etFirstTargetArg = thFirstTargetArg.GetInternalCorElementType();
                if (!CorTypeInfo::IsObjRef(etFirstTargetArg))
                    return false;
            }
        }
    }
    else
    {
        // The type of the first argument to an instance method is from the method type.
        thFirstTargetArg = thExactMethodType;

        // If the delegate is open and the target method is on a value type or primitive then the first argument of the invoke
        // method must be a reference to that type. So make promote the type we got from the reference to a ref. (We don't need to
        // do this for the closed instance case because there we got the invocation side type from the first arg passed in, i.e.
        // it's had the ref stripped from it implicitly).
        if (fIsOpenDelegate)
        {
            CorElementType etFirstTargetArg = thFirstTargetArg.GetInternalCorElementType();
            if (etFirstTargetArg <= ELEMENT_TYPE_R8 ||
                etFirstTargetArg == ELEMENT_TYPE_VALUETYPE ||
                etFirstTargetArg == ELEMENT_TYPE_I ||
                etFirstTargetArg == ELEMENT_TYPE_U)
                thFirstTargetArg = thFirstTargetArg.MakeByRef();
        }
    }

    // Now we have enough data to compare the first arguments on the invoke and target side. Skip this if we are closed over null
    // (we don't have enough type information for the match but it doesn't matter because the null matches all object reference
    // types, which our first arg must be in this case). We always relax signature matching for the first argument of an instance
    // method, since it's always allowable to call the method on a more derived type. In cases where we're closed over the first
    // argument we know that argument is boxed (because it was passed to us as an object). We provide this information to
    // IsLocationAssignable because it relaxes signature matching for some important cases (e.g. passing a value type to an argument
    // typed as Object).
    if (!thFirstInvokeArg.IsNull())
        if (!IsLocationAssignable(thFirstInvokeArg,
                                  thFirstTargetArg,
                                  !pTargetMethod->IsStatic() || flags & DBF_RelaxedSignature,
                                  !fIsOpenDelegate))
            return false;

        // Loop over the remaining fixed args, the list should be one to one at this point.
    while (TRUE)
    {
        CorElementType etInvokeArg = sigInvoke.NextArgNormalized();
        CorElementType etTargetArg = sigTarget.NextArgNormalized();
        if (etInvokeArg == ELEMENT_TYPE_END || etTargetArg == ELEMENT_TYPE_END)
        {
            // We've reached the end of one signature. We better be at the end of the other or it's not a match.
            if (etInvokeArg != etTargetArg)
                return false;
            break;
        }
        else
        {
            TypeHandle thInvokeArg = sigInvoke.GetLastTypeHandleThrowing();
            TypeHandle thTargetArg = sigTarget.GetLastTypeHandleThrowing();

            if (!IsLocationAssignable(thInvokeArg, thTargetArg, flags & DBF_RelaxedSignature, false))
                return false;
        }
    }

 CheckReturnType:

    // Almost there, just compare the return types (remember that the assignment is in the other direction here, from callee to
    // caller, so switch the order of the arguments to IsLocationAssignable).
    // If we ever relax this we have to think about how to unbox this arg in the Nullable<T> case also.
    if (!IsLocationAssignable(sigTarget.GetRetTypeHandleThrowing(),
                              sigInvoke.GetRetTypeHandleThrowing(),
                              flags & DBF_RelaxedSignature,
                              false))
        return false;

    // We must have a match.
    if (pfIsOpenDelegate)
        *pfIsOpenDelegate = fIsOpenDelegate;
    return true;
}

MethodDesc* COMDelegate::GetDelegateCtor(TypeHandle delegateType, MethodDesc *pTargetMethod, DelegateCtorArgs *pCtorData)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    MethodDesc *pRealCtor = NULL;

    MethodTable *pDelMT = delegateType.AsMethodTable();

    MethodDesc *pDelegateInvoke = COMDelegate::FindDelegateInvokeMethod(pDelMT);

    MetaSig invokeSig(pDelegateInvoke);
    MetaSig methodSig(pTargetMethod);
    UINT invokeArgCount = invokeSig.NumFixedArgs();
    UINT methodArgCount = methodSig.NumFixedArgs();
    BOOL isStatic = pTargetMethod->IsStatic();
    LoaderAllocator *pTargetMethodLoaderAllocator = pTargetMethod->GetLoaderAllocator();
    BOOL isCollectible = pTargetMethodLoaderAllocator->IsCollectible();
    // A method that may be instantiated over a collectible type, and is static will require a delegate
    // that has the _methodBase field filled in with the LoaderAllocator of the collectible assembly
    // associated with the instantiation.
    BOOL fMaybeCollectibleAndStatic = FALSE;

    // Do not allow static methods with [UnmanagedCallersOnlyAttribute] to be a delegate target.
    // A method marked UnmanagedCallersOnly is special and allowing it to be delegate target will destabilize the runtime.
    if (pTargetMethod->HasUnmanagedCallersOnlyAttribute())
    {
        COMPlusThrow(kNotSupportedException, W("NotSupported_UnmanagedCallersOnlyTarget"));
    }

    if (isStatic)
    {
        // When this method is called and the method being considered is shared, we typically
        // are passed a Wrapper method for the explicit canonical instantiation. It would be illegal
        // to actually call that method, but the jit uses it as a proxy for the real instantiated
        // method, so we can't make the methoddesc apis that report that it is the shared methoddesc
        // report that it is. Hence, this collection of checks that will detect if the methoddesc
        // being used is a normal method desc to shared code, or if it is a wrapped methoddesc
        // corresponding to the actually uncallable instantiation over __Canon.
        if (pTargetMethod->GetMethodTable()->IsSharedByGenericInstantiations())
        {
            fMaybeCollectibleAndStatic = TRUE;
        }
        else if (pTargetMethod->IsSharedByGenericMethodInstantiations())
        {
            fMaybeCollectibleAndStatic = TRUE;
        }
        else if (pTargetMethod->HasMethodInstantiation())
        {
            Instantiation instantiation = pTargetMethod->GetMethodInstantiation();
            for (DWORD iParam = 0; iParam < instantiation.GetNumArgs(); iParam++)
            {
                if (instantiation[iParam] == g_pCanonMethodTableClass)
                {
                    fMaybeCollectibleAndStatic = TRUE;
                    break;
                }
            }
        }
    }

    // If this might be collectible and is static, then we will go down the slow path. Implementing
    // yet another fast path would require a methoddesc parameter, but hopefully isn't necessary.
    if (fMaybeCollectibleAndStatic)
        return NULL;

    if (!isStatic)
        methodArgCount++; // count 'this'
    MethodDesc *pCallerMethod = (MethodDesc*)pCtorData->pMethod;

    if (NeedsWrapperDelegate(pTargetMethod))
    {
        // If we need a wrapper, go through slow path
        return NULL;
    }

    // Force the slow path for nullable so that we can give the user an error in case were the verifier is not run.
    MethodTable* pMT = pTargetMethod->GetMethodTable();
    if (!pTargetMethod->IsStatic() && Nullable::IsNullableType(pMT))
        return NULL;

#ifdef FEATURE_COMINTEROP
    // We'll always force classic COM types to go down the slow path for security checks.
    if (pMT->IsComObjectType() || pMT->IsComImport())
    {
        return NULL;
    }
#endif

    // DELEGATE KINDS TABLE
    //
    //                                  _target         _methodPtr              _methodPtrAux       _invocationList     _invocationCount
    //
    // 1- Instance closed               'this' ptr      target method           null                null                0
    // 2- Instance open non-virt        delegate        shuffle thunk           target method       null                0
    // 3- Instance open virtual         delegate        Virtual-stub dispatch   method id           null                0
    // 4- Static closed                 first arg       target method           null                null                0
    // 5- Static closed (special sig)   delegate        specialSig thunk        target method       first arg           0
    // 6- Static opened                 delegate        shuffle thunk           target method       null                0
    // 7- Wrapper                       delegate        call thunk              MethodDesc (frame)  target delegate     (arm only, VSD indirection cell address)
    //
    // Delegate invoke arg count == target method arg count - 2, 3, 6
    // Delegate invoke arg count == 1 + target method arg count - 1, 4, 5
    //
    // 1, 4     - MulticastDelegate.ctor1 (simply assign _target and _methodPtr)
    // 5        - MulticastDelegate.ctor2 (see table, takes 3 args)
    // 2, 6     - MulticastDelegate.ctor3 (take shuffle thunk)
    // 3        - MulticastDelegate.ctor4 (take shuffle thunk, retrieve MethodDesc) ???
    //
    // 7 - Needs special handling
    //
    // With collectible types, we need to fill the _methodBase field in with a value that represents the LoaderAllocator of the target method
    // if the delegate is not a closed instance delegate.
    //
    // There are two techniques that will work for this.
    // One is to simply use the slow path. We use this for unusual constructs. It is rather slow.
    //  We will use this for the secure variants
    //
    // Another is to pass a gchandle to the delegate ctor. This is fastest, but only works if we can predict the gc handle at this time.
    //  We will use this for the non secure variants
    //
    // If you modify this logic, please update DacDbiInterfaceImpl::GetDelegateType, DacDbiInterfaceImpl::GetDelegateType,
    // DacDbiInterfaceImpl::GetDelegateFunctionData, and DacDbiInterfaceImpl::GetDelegateTargetObject.


    if (invokeArgCount == methodArgCount)
    {
        // case 2, 3, 6
        //@TODO:NEWVTWORK: Might need changing.
        // The virtual dispatch stub doesn't work on unboxed value type objects which don't have MT pointers.
        // Since open virtual (delegate kind 3) delegates on value type methods require unboxed objects we cannot use the
        // virtual dispatch stub for them. On the other hand, virtual methods on value types don't need
        // to be dispatched because value types cannot be derived. So we treat them like non-virtual methods (delegate kind 2).
        if (!isStatic && pTargetMethod->IsVirtual() && !pTargetMethod->GetMethodTable()->IsValueType())
        {
            // case 3
            if (isCollectible)
                pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_COLLECTIBLE_VIRTUAL_DISPATCH);
            else
                pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_VIRTUAL_DISPATCH);
        }
        else
        {
            // case 2, 6
            if (isCollectible)
                pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_COLLECTIBLE_OPENED);
            else
                pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_OPENED);
        }

        pCtorData->pArg3 = (void*)SetupShuffleThunk(pDelMT, pTargetMethod);
        if (isCollectible)
        {
            pCtorData->pArg4 = pTargetMethodLoaderAllocator->GetLoaderAllocatorObjectHandle();
        }
    }
    else
    {
        // case 1, 4, 5
        //TODO: need to differentiate on 5
        _ASSERTE(invokeArgCount + 1 == methodArgCount);

#ifdef HAS_THISPTR_RETBUF_PRECODE
        // Force closed delegates over static methods with return buffer to go via
        // the slow path to create ThisPtrRetBufPrecode
        if (isStatic && pTargetMethod->HasRetBuffArg() && IsRetBuffPassedAsFirstArg())
            return NULL;
#endif

        // under the conditions below the delegate ctor needs to perform some heavy operation
        // to get the unboxing stub
        BOOL needsRuntimeInfo = !pTargetMethod->IsStatic() &&
                    pTargetMethod->GetMethodTable()->IsValueType() && !pTargetMethod->IsUnboxingStub();

        if (needsRuntimeInfo)
            pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_RT_CLOSED);
        else
        {
            if (!isStatic)
                pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_CLOSED);
            else
            {
                if (isCollectible)
                {
                    pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_COLLECTIBLE_CLOSED_STATIC);
                    pCtorData->pArg3 = pTargetMethodLoaderAllocator->GetLoaderAllocatorObjectHandle();
                }
                else
                {
                    pRealCtor = CoreLibBinder::GetMethod(METHOD__MULTICAST_DELEGATE__CTOR_CLOSED_STATIC);
                }
            }
        }
    }

    return pRealCtor;
}


BOOL COMDelegate::IsWrapperDelegate(DELEGATEREF dRef)
{
    CONTRACTL
    {
        MODE_ANY;
        NOTHROW;
        GC_NOTRIGGER;
    }
    CONTRACTL_END;
    DELEGATEREF innerDel = NULL;
    if (dRef->GetInvocationCount() != 0)
    {
        innerDel = (DELEGATEREF) dRef->GetInvocationList();
        if (innerDel != NULL && innerDel->GetMethodTable()->IsDelegate())
        {
            // We have a wrapper delegate
            return TRUE;
        }
    }
    return FALSE;
}

#endif // !DACCESS_COMPILE


// Decides if pcls derives from Delegate.
BOOL COMDelegate::IsDelegate(MethodTable *pMT)
{
    WRAPPER_NO_CONTRACT;
    return (pMT == g_pDelegateClass) || (pMT == g_pMulticastDelegateClass) || pMT->IsDelegate();
}
