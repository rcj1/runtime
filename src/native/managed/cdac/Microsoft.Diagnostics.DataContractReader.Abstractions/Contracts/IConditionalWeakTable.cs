// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;

namespace Microsoft.Diagnostics.DataContractReader.Contracts;

public interface IConditionalWeakTable : IContract
{
    static string IContract.Name { get; } = nameof(ConditionalWeakTable);
    TargetPointer TryGetValue(TargetPointer conditionalWeakTable, TargetPointer key) => throw new NotImplementedException();
}

public readonly struct ConditionalWeakTable : IConditionalWeakTable
{
    // Everything throws NotImplementedException
}
