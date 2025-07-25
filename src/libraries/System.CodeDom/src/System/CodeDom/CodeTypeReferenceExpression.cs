// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.CodeDom
{
    public class CodeTypeReferenceExpression : CodeExpression
    {
        public CodeTypeReferenceExpression() { }

        public CodeTypeReferenceExpression(CodeTypeReference type)
        {
            Type = type;
        }

        public CodeTypeReferenceExpression(string type)
        {
            Type = new CodeTypeReference(type);
        }

        public CodeTypeReferenceExpression(Type type)
        {
            Type = new CodeTypeReference(type);
        }

        public CodeTypeReference Type
        {
            get => field ??= new CodeTypeReference("");
            set => field = value;
        }
    }
}
