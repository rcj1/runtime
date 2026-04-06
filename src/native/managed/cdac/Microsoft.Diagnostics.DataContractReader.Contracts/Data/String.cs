// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.DataContractReader.Data;

internal sealed class String : IData<String>
{
    static String IData<String>.Create(Target target, TargetPointer address)
        => new String(target, address);

    public String(Target target, TargetPointer address)
    {
        Target.TypeInfo type = target.GetTypeInfo(DataType.String);

        BufferOffset = (uint)type.Fields[nameof(BufferOffset)].Offset;
        FirstChar = address + BufferOffset;
        StringLength = target.Read<uint>(address + (ulong)type.Fields[nameof(StringLength)].Offset);
    }

    public TargetPointer FirstChar { get; init; }
    public uint StringLength { get; init; }
    public uint BufferOffset { get; init; }
}
