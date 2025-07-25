// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using ILLink.Shared.DataFlow;
using ILLink.Shared.TypeSystemProxy;

// This is needed due to NativeAOT which doesn't enable nullable globally yet
#nullable enable

namespace ILLink.Shared.TrimAnalysis
{
    /// <summary>
    /// This is the System.RuntimeTypeHandle equivalent to a <see cref="SystemTypeValue"/> node.
    /// </summary>
    internal sealed record RuntimeTypeHandleValue : SingleValue
    {
        public RuntimeTypeHandleValue(in TypeProxy representedType)
        {
            RepresentedType = representedType;
        }

        public readonly TypeProxy RepresentedType;

        public override SingleValue DeepCopy() => this; // This value is immutable

        public override string ToString() => this.ValueToString(RepresentedType);
    }
}
