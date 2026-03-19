// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using Microsoft.Diagnostics.DataContractReader.Data;

namespace Microsoft.Diagnostics.DataContractReader.Contracts;

internal readonly struct ComWrappers_1 : IComWrappers
{
    private const string NativeObjectWrapperNamespace = "System.Runtime.InteropServices";
    private const string NativeObjectWrapperName = "ComWrappers+NativeObjectWrapper";
    private const string ComWrappersNamespace = "System.Runtime.InteropServices";
    private const string ComWrappersName = "ComWrappers";
    private const string NativeObjectWrapperCWTFieldName = "s_nativeObjectWrapperTable";
    private const string AllManagedObjectWrapperTableFieldName = "s_allManagedObjectWrapperTable";
    private const string ListNamespace = "System.Collections.Generic";
    private const string ListName = "List`1";
    private const string ListItemsFieldName = "_items";
    private const string ListSizeFieldName = "_size";
    private readonly Target _target;

    public ComWrappers_1(Target target)
    {
        _target = target;
    }

    public TargetPointer GetComWrappersIdentity(TargetPointer address)
    {
        Data.NativeObjectWrapperObject wrapper = _target.ProcessedData.GetOrAdd<Data.NativeObjectWrapperObject>(address);
        return wrapper.ExternalComObject;
    }

    private bool GetComWrappersCCWVTableQIAddress(TargetPointer ccw, out TargetPointer vtable, out TargetPointer qiAddress)
    {
        qiAddress = TargetPointer.Null;
        if (!_target.TryReadPointer(ccw, out vtable))
            return false;
        if (!_target.TryReadCodePointer(vtable, out TargetCodePointer qiCodePtr))
            return false;
        qiAddress = CodePointerUtils.AddressFromCodePointer(qiCodePtr, _target);
        return true;
    }

    private bool IsComWrappersCCW(TargetPointer ccw)
    {
        if (!GetComWrappersCCWVTableQIAddress(ccw, out _, out TargetPointer qiAddress))
            return false;

        TargetPointer comWrappersVtablePtrs = _target.ReadGlobalPointer(Constants.Globals.ComWrappersVtablePtrs);
        Data.ComWrappersVtablePtrs comWrappersVtableStruct = _target.ProcessedData.GetOrAdd<Data.ComWrappersVtablePtrs>(comWrappersVtablePtrs);
        return comWrappersVtableStruct.ComWrappersInterfacePointers.Contains(CodePointerUtils.CodePointerFromAddress(qiAddress, _target));
    }

    public TargetPointer GetManagedObjectWrapperFromCCW(TargetPointer ccw)
    {
        if (!IsComWrappersCCW(ccw))
            return TargetPointer.Null;
        if (!_target.TryReadPointer(ccw & _target.ReadGlobalPointer(Constants.Globals.DispatchThisPtrMask), out TargetPointer MOWWrapper))
            return TargetPointer.Null;
        return MOWWrapper;
    }

    public TargetPointer GetComWrappersObjectFromMOW(TargetPointer mow)
    {
        TargetPointer objHandle = _target.ReadPointer(mow);
        Data.ObjectHandle handle = _target.ProcessedData.GetOrAdd<Data.ObjectHandle>(objHandle);
        Data.ManagedObjectWrapperHolderObject mowHolderObject = _target.ProcessedData.GetOrAdd<Data.ManagedObjectWrapperHolderObject>(handle.Object);
        return mowHolderObject.WrappedObject;
    }

    public long GetMOWReferenceCount(TargetPointer mow)
    {
        Data.ManagedObjectWrapperLayout layout = _target.ProcessedData.GetOrAdd<Data.ManagedObjectWrapperLayout>(mow);
        return layout.RefCount;
    }

    public bool IsComWrappersRCW(TargetPointer rcw)
    {
        TargetPointer mt = _target.Contracts.Object.GetMethodTableAddress(rcw);

        // get system module
        ILoader loader = _target.Contracts.Loader;
        TargetPointer systemAssembly = loader.GetSystemAssembly();
        ModuleHandle moduleHandle = loader.GetModuleHandleFromAssemblyPtr(systemAssembly);

        // lookup by name
        IRuntimeTypeSystem rts = _target.Contracts.RuntimeTypeSystem;
        TargetPointer typeHandlePtr = rts.GetTypeByNameAndModule(NativeObjectWrapperName, NativeObjectWrapperNamespace, moduleHandle).Address;
        return mt == typeHandlePtr;
    }

    private TargetPointer GetSPCFieldDesc(string @namespace, string typeName, string fieldName)
    {
        // get system module
        ILoader loader = _target.Contracts.Loader;
        TargetPointer systemAssembly = loader.GetSystemAssembly();
        ModuleHandle moduleHandle = loader.GetModuleHandleFromAssemblyPtr(systemAssembly);

        // lookup by name
        IRuntimeTypeSystem rts = _target.Contracts.RuntimeTypeSystem;
        TypeHandle th = rts.GetTypeByNameAndModule(typeName, @namespace, moduleHandle);
        return rts.GetFieldDescByName(th, fieldName);
    }

    public TargetPointer GetComWrappersRCWForObject(TargetPointer obj)
    {
        TargetPointer fieldDescAddr = GetSPCFieldDesc(ComWrappersNamespace, ComWrappersName, NativeObjectWrapperCWTFieldName);
        TargetPointer cwtTable = _target.Contracts.RuntimeTypeSystem.GetStaticFieldAddress(fieldDescAddr);
        if (cwtTable == TargetPointer.Null)
            return TargetPointer.Null;
        IConditionalWeakTable cwt = _target.Contracts.ConditionalWeakTable;
        _ = cwt.TryGetValue(cwtTable, obj, out TargetPointer rcw);
        return rcw;
    }

    public IEnumerable<TargetPointer> GetMOWs(TargetPointer obj)
    {
        IRuntimeTypeSystem rts = _target.Contracts.RuntimeTypeSystem;
        TargetPointer fieldDescAddr = GetSPCFieldDesc(ComWrappersNamespace, ComWrappersName, AllManagedObjectWrapperTableFieldName);
        TargetPointer MOWTable = rts.GetStaticFieldAddress(fieldDescAddr);
        if (MOWTable == TargetPointer.Null)
            yield break;
        
        IConditionalWeakTable cwt = _target.Contracts.ConditionalWeakTable;
        if (cwt.TryGetValue(MOWTable, obj, out TargetPointer mowListObj))
        {
            TargetPointer itemsFieldDescAddr = GetSPCFieldDesc(ListNamespace, ListName, ListItemsFieldName);
            uint offset = rts.GetFieldDescOffset(itemsFieldDescAddr);
            TargetPointer listItemsPtr = _target.ReadPointer(mowListObj + offset);

            TargetPointer sizeFieldDescAddr = GetSPCFieldDesc(ListNamespace, ListName, ListSizeFieldName);
            uint sizeOffset = rts.GetFieldDescOffset(sizeFieldDescAddr);
            int size = _target.Read<int>(mowListObj + sizeOffset);
            
            if (size > 0 && listItemsPtr != TargetPointer.Null)
            {
                Data.Array listItemsArray = _target.ProcessedData.GetOrAdd<Data.Array>(listItemsPtr);
                for (int i = 0; i < size; i++)
                {
                    TargetPointer mow = _target.ReadPointer(listItemsArray.DataPointer + (ulong)(i * _target.PointerSize));
                    Data.ManagedObjectWrapperHolderObject mowHolderObject = _target.ProcessedData.GetOrAdd<Data.ManagedObjectWrapperHolderObject>(mow);
                    yield return mowHolderObject.Wrapper;
                }
            }
        }
        yield break;
    }
}
