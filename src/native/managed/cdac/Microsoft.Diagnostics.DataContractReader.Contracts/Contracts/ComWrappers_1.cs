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

    private TargetPointer GetComWrappersFieldAddress(string fieldName)
    {
        // get system module
        ILoader loader = _target.Contracts.Loader;
        TargetPointer systemAssembly = loader.GetSystemAssembly();
        ModuleHandle moduleHandle = loader.GetModuleHandleFromAssemblyPtr(systemAssembly);

        // lookup by name
        IRuntimeTypeSystem rts = _target.Contracts.RuntimeTypeSystem;
        TypeHandle th = rts.GetTypeByNameAndModule(ComWrappersName, ComWrappersNamespace, moduleHandle);
        TargetPointer fieldDescPtr = rts.GetFieldDescByName(th, NativeObjectWrapperCWTFieldName, moduleHandle);
        return rts.GetStaticAddress(fieldDescPtr);
    }

    public TargetPointer GetComWrappersRCWForObject(TargetPointer obj)
    {
        TargetPointer cwtTableAddr = GetComWrappersFieldAddress(NativeObjectWrapperCWTFieldName);
    }

    public List<TargetPointer> GetMOWList(TargetPointer obj)
    {
        TargetPointer allMOWTableAddr = GetComWrappersFieldAddress(AllManagedObjectWrapperTableFieldName);
        Data.AllManagedObjectWrapperTable allMOWTable = _target.ProcessedData.GetOrAdd<Data.AllManagedObjectWrapperTable>(allMOWTableAddr);
        List<TargetPointer> mows = new();
        foreach (TargetPointer mow in allMOWTable.MOWs)
        {
            Data.ManagedObjectWrapperHolderObject mowHolderObject = _target.ProcessedData.GetOrAdd<Data.ManagedObjectWrapperHolderObject>(mow);
            if (mowHolderObject.WrappedObject == obj)
                mows.Add(mow);
        }
        return mows;
    }
}
