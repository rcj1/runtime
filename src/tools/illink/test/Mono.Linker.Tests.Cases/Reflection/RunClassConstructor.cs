﻿using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using Mono.Linker.Tests.Cases.Expectations.Assertions;
using Mono.Linker.Tests.Cases.Expectations.Metadata;

namespace Mono.Linker.Tests.Cases.Reflection
{
    [SetupCSharpCompilerToUse("csc")]
    [ExpectedNoWarnings]
    public class RunClassConstructor
    {
        public static void Main()
        {
            TestRunClassConstructor();
            TestNonKeptStaticConstructor();
            TestNull();
            TestNoValue();
            TestDataFlowType();
            TestNonPublicConstructorDataFlowType();
            TestPublicConstructorDataFlowType();
            TestIfElseUsingRuntimeTypeHandle(1);
            TestIfElseUsingType(1);
            TestNullableValueType();
            TestNullableEnum();
        }

        [Kept]
        static void TestRunClassConstructor()
        {
            RuntimeHelpers.RunClassConstructor(typeof(OnlyUsedViaReflection).TypeHandle);
        }

        [Kept]
        static void TestNonKeptStaticConstructor()
        {
            var a = new NonKeptStaticConstructorClass();
        }

        [Kept]
        static void TestNull()
        {
            Type type = null;
            RuntimeHelpers.RunClassConstructor(type.TypeHandle);
        }

        [Kept]
        static void TestNoValue()
        {
            Type t = null;
            Type noValue = Type.GetTypeFromHandle(t.TypeHandle);
            RuntimeHelpers.RunClassConstructor(noValue.TypeHandle);
        }

        [Kept]
        static Type FindType()
        {
            return null;
        }

        [Kept]
        [ExpectedWarning("IL2059", nameof(RuntimeHelpers) + "." + nameof(RuntimeHelpers.RunClassConstructor))]

        static void TestDataFlowType()
        {
            Type type = FindType();
            RuntimeHelpers.RunClassConstructor(type.TypeHandle);
        }

        [Kept]
        [return: DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.NonPublicConstructors)]
        [return: KeptAttributeAttribute(typeof(DynamicallyAccessedMembersAttribute))]
        static Type FindTypeWithNonPublicConstructors()
        {
            return null;
        }

        [Kept]
        static void TestNonPublicConstructorDataFlowType()
        {
            Type type = FindTypeWithNonPublicConstructors();
            RuntimeHelpers.RunClassConstructor(type.TypeHandle);
        }

        [Kept]
        [return: KeptAttributeAttribute(typeof(DynamicallyAccessedMembersAttribute))]
        [return: DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicConstructors)]
        static Type FindTypeWithPublicConstructors()
        {
            return null;
        }

        [Kept]
        [ExpectedWarning("IL2059", nameof(RuntimeHelpers) + "." + nameof(RuntimeHelpers.RunClassConstructor))]
        static void TestPublicConstructorDataFlowType()
        {
            Type type = FindTypeWithPublicConstructors();
            RuntimeHelpers.RunClassConstructor(type.TypeHandle);
        }

        [Kept]
        [ExpectedWarning("IL2059", nameof(RuntimeHelpers) + "." + nameof(RuntimeHelpers.RunClassConstructor))]
        static void TestIfElseUsingRuntimeTypeHandle(int i)
        {
            RuntimeTypeHandle myType;
            if (i == 1)
            {
                myType = typeof(IfClass).TypeHandle;
            }
            else if (i == 2)
            {
                myType = FindType().TypeHandle;
            }
            else
            {
                myType = typeof(ElseClass).TypeHandle;
            }
            RuntimeHelpers.RunClassConstructor(myType);
        }

        [Kept]
        static void TestIfElseUsingType(int i)
        {
            Type myType;
            if (i == 1)
            {
                myType = typeof(IfClass2);
            }
            else if (i == 2)
            {
                myType = null;
            }
            else
            {
                myType = typeof(ElseClass2);
            }
            RuntimeHelpers.RunClassConstructor(myType.TypeHandle);
        }

        [Kept]
        static void TestNullableValueType()
        {
            RuntimeHelpers.RunClassConstructor(typeof(int?).TypeHandle);
        }

        [Kept]
        [KeptMember("value__")]
        enum MyEnum { }

        [Kept]
        static void TestNullableEnum()
        {
            RuntimeHelpers.RunClassConstructor(typeof(Nullable<MyEnum>).TypeHandle);
        }

        [Kept]
        [KeptMember(".cctor()")]
        class OnlyUsedViaReflection
        {
            [Kept]
            static int i = 5;
        }

        [Kept]
        [KeptMember(".ctor()")]
        class NonKeptStaticConstructorClass
        {
            static int i = 5;
        }

        [Kept]
        [KeptMember(".cctor()")]
        class IfClass
        {
            public IfClass()
            { }
            private IfClass(int foo)
            { }
        }

        [Kept]
        [KeptMember(".cctor()")]
        class ElseClass
        {
            [Kept]
            static ElseClass()
            { }
            public ElseClass(int foo)
            { }
        }
        [Kept]
        [KeptMember(".cctor()")]
        class IfClass2
        {
            public IfClass2()
            { }
            private IfClass2(int foo)
            { }
        }

        [Kept]
        [KeptMember(".cctor()")]
        class ElseClass2
        {
            [Kept]
            static ElseClass2()
            { }
            public ElseClass2(int foo)
            { }
        }
    }
}
