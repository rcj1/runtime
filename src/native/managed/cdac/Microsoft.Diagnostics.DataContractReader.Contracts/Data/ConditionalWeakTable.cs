// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.DataContractReader.Data;

internal sealed class ConditionalWeakTable : IData<ConditionalWeakTable>
{
    static ConditionalWeakTable IData<ConditionalWeakTable>.Create(Target target, TargetPointer address)
        => new ConditionalWeakTable(target, address);

    public ConditionalWeakTable(Target target, TargetPointer address)
    {
        Target.TypeInfo type = target.GetTypeInfo(DataType.ConditionalWeakTable);
        Buckets = address + (ulong)type.Fields[nameof(Buckets)].Offset;
        Entries = address + (ulong)type.Fields[nameof(Entries)].Offset;
    }

    public TargetPointer Buckets { get; init; }
    public TargetPointer Entries { get; init; }
}
