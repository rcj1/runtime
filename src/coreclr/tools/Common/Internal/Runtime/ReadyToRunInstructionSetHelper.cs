// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// DO NOT EDIT THIS FILE! IT IS AUTOGENERATED
// FROM /src/coreclr/tools/Common/JitInterface/ThunkGenerator/InstructionSetDesc.txt
// using /src/coreclr/tools/Common/JitInterface/ThunkGenerator/gen.bat

using System;
using System.Runtime.InteropServices;
using Internal.JitInterface;
using Internal.TypeSystem;

namespace Internal.ReadyToRunConstants
{
    public static class ReadyToRunInstructionSetHelper
    {
        public static ReadyToRunInstructionSet? R2RInstructionSet(this InstructionSet instructionSet, TargetArchitecture architecture)
        {
            switch (architecture)
            {

                case TargetArchitecture.ARM64:
                    {
                        switch (instructionSet)
                        {
                            case InstructionSet.ARM64_ArmBase: return ReadyToRunInstructionSet.ArmBase;
                            case InstructionSet.ARM64_ArmBase_Arm64: return ReadyToRunInstructionSet.ArmBase;
                            case InstructionSet.ARM64_AdvSimd: return ReadyToRunInstructionSet.AdvSimd;
                            case InstructionSet.ARM64_AdvSimd_Arm64: return ReadyToRunInstructionSet.AdvSimd;
                            case InstructionSet.ARM64_Aes: return ReadyToRunInstructionSet.Aes;
                            case InstructionSet.ARM64_Aes_Arm64: return ReadyToRunInstructionSet.Aes;
                            case InstructionSet.ARM64_Crc32: return ReadyToRunInstructionSet.Crc32;
                            case InstructionSet.ARM64_Crc32_Arm64: return ReadyToRunInstructionSet.Crc32;
                            case InstructionSet.ARM64_Dp: return ReadyToRunInstructionSet.Dp;
                            case InstructionSet.ARM64_Dp_Arm64: return ReadyToRunInstructionSet.Dp;
                            case InstructionSet.ARM64_Rdm: return ReadyToRunInstructionSet.Rdm;
                            case InstructionSet.ARM64_Rdm_Arm64: return ReadyToRunInstructionSet.Rdm;
                            case InstructionSet.ARM64_Sha1: return ReadyToRunInstructionSet.Sha1;
                            case InstructionSet.ARM64_Sha1_Arm64: return ReadyToRunInstructionSet.Sha1;
                            case InstructionSet.ARM64_Sha256: return ReadyToRunInstructionSet.Sha256;
                            case InstructionSet.ARM64_Sha256_Arm64: return ReadyToRunInstructionSet.Sha256;
                            case InstructionSet.ARM64_Atomics: return ReadyToRunInstructionSet.Atomics;
                            case InstructionSet.ARM64_Vector64: return null;
                            case InstructionSet.ARM64_Vector128: return null;
                            case InstructionSet.ARM64_Dczva: return null;
                            case InstructionSet.ARM64_Rcpc: return ReadyToRunInstructionSet.Rcpc;
                            case InstructionSet.ARM64_VectorT128: return ReadyToRunInstructionSet.VectorT128;
                            case InstructionSet.ARM64_Rcpc2: return ReadyToRunInstructionSet.Rcpc2;
                            case InstructionSet.ARM64_Sve: return ReadyToRunInstructionSet.Sve;
                            case InstructionSet.ARM64_Sve_Arm64: return ReadyToRunInstructionSet.Sve;
                            case InstructionSet.ARM64_Sve2: return ReadyToRunInstructionSet.Sve2;
                            case InstructionSet.ARM64_Sve2_Arm64: return ReadyToRunInstructionSet.Sve2;

                            default: throw new Exception("Unknown instruction set");
                        }
                    }

                case TargetArchitecture.RiscV64:
                    {
                        switch (instructionSet)
                        {
                            case InstructionSet.RiscV64_RiscV64Base: return ReadyToRunInstructionSet.RiscV64Base;
                            case InstructionSet.RiscV64_Zba: return ReadyToRunInstructionSet.Zba;
                            case InstructionSet.RiscV64_Zbb: return ReadyToRunInstructionSet.Zbb;

                            default: throw new Exception("Unknown instruction set");
                        }
                    }

                case TargetArchitecture.X64:
                    {
                        switch (instructionSet)
                        {
                            case InstructionSet.X64_X86Base: return ReadyToRunInstructionSet.X86Base;
                            case InstructionSet.X64_X86Base_X64: return ReadyToRunInstructionSet.X86Base;
                            case InstructionSet.X64_SSE42: return ReadyToRunInstructionSet.Sse42;
                            case InstructionSet.X64_SSE42_X64: return ReadyToRunInstructionSet.Sse42;
                            case InstructionSet.X64_AVX: return ReadyToRunInstructionSet.Avx;
                            case InstructionSet.X64_AVX_X64: return ReadyToRunInstructionSet.Avx;
                            case InstructionSet.X64_AVX2: return ReadyToRunInstructionSet.Avx2;
                            case InstructionSet.X64_AVX2_X64: return ReadyToRunInstructionSet.Avx2;
                            case InstructionSet.X64_AVX512: return ReadyToRunInstructionSet.Evex;
                            case InstructionSet.X64_AVX512_X64: return ReadyToRunInstructionSet.Evex;
                            case InstructionSet.X64_AVX512v2: return ReadyToRunInstructionSet.Avx512Ifma;
                            case InstructionSet.X64_AVX512v2_X64: return ReadyToRunInstructionSet.Avx512Ifma;
                            case InstructionSet.X64_AVX512v3: return ReadyToRunInstructionSet.Avx512Bitalg;
                            case InstructionSet.X64_AVX512v3_X64: return ReadyToRunInstructionSet.Avx512Bitalg;
                            case InstructionSet.X64_AVX10v1: return ReadyToRunInstructionSet.Avx512Bf16;
                            case InstructionSet.X64_AVX10v1_X64: return ReadyToRunInstructionSet.Avx512Bf16;
                            case InstructionSet.X64_AVX10v2: return ReadyToRunInstructionSet.Avx10v2;
                            case InstructionSet.X64_AVX10v2_X64: return ReadyToRunInstructionSet.Avx10v2;
                            case InstructionSet.X64_APX: return ReadyToRunInstructionSet.Apx;
                            case InstructionSet.X64_AES: return ReadyToRunInstructionSet.Aes;
                            case InstructionSet.X64_AES_X64: return ReadyToRunInstructionSet.Aes;
                            case InstructionSet.X64_AES_V256: return ReadyToRunInstructionSet.Aes_V256;
                            case InstructionSet.X64_AES_V512: return ReadyToRunInstructionSet.Aes_V512;
                            case InstructionSet.X64_AVX512VP2INTERSECT: return ReadyToRunInstructionSet.Avx512Vp2intersect;
                            case InstructionSet.X64_AVX512VP2INTERSECT_X64: return ReadyToRunInstructionSet.Avx512Vp2intersect;
                            case InstructionSet.X64_AVXIFMA: return ReadyToRunInstructionSet.AvxIfma;
                            case InstructionSet.X64_AVXIFMA_X64: return ReadyToRunInstructionSet.AvxIfma;
                            case InstructionSet.X64_AVXVNNI: return ReadyToRunInstructionSet.AvxVnni;
                            case InstructionSet.X64_AVXVNNI_X64: return ReadyToRunInstructionSet.AvxVnni;
                            case InstructionSet.X64_GFNI: return ReadyToRunInstructionSet.Gfni;
                            case InstructionSet.X64_GFNI_X64: return ReadyToRunInstructionSet.Gfni;
                            case InstructionSet.X64_GFNI_V256: return ReadyToRunInstructionSet.Gfni_V256;
                            case InstructionSet.X64_GFNI_V512: return ReadyToRunInstructionSet.Gfni_V512;
                            case InstructionSet.X64_SHA: return ReadyToRunInstructionSet.Sha;
                            case InstructionSet.X64_SHA_X64: return ReadyToRunInstructionSet.Sha;
                            case InstructionSet.X64_WAITPKG: return ReadyToRunInstructionSet.WaitPkg;
                            case InstructionSet.X64_WAITPKG_X64: return ReadyToRunInstructionSet.WaitPkg;
                            case InstructionSet.X64_X86Serialize: return ReadyToRunInstructionSet.X86Serialize;
                            case InstructionSet.X64_X86Serialize_X64: return ReadyToRunInstructionSet.X86Serialize;
                            case InstructionSet.X64_Vector128: return null;
                            case InstructionSet.X64_Vector256: return null;
                            case InstructionSet.X64_Vector512: return null;
                            case InstructionSet.X64_VectorT128: return ReadyToRunInstructionSet.VectorT128;
                            case InstructionSet.X64_VectorT256: return ReadyToRunInstructionSet.VectorT256;
                            case InstructionSet.X64_VectorT512: return ReadyToRunInstructionSet.VectorT512;
                            case InstructionSet.X64_AVXVNNIINT: return ReadyToRunInstructionSet.AvxVnniInt8;
                            case InstructionSet.X64_AVXVNNIINT_V512: return ReadyToRunInstructionSet.AvxVnniInt8_V512;

                            default: throw new Exception("Unknown instruction set");
                        }
                    }

                case TargetArchitecture.X86:
                    {
                        switch (instructionSet)
                        {
                            case InstructionSet.X86_X86Base: return ReadyToRunInstructionSet.X86Base;
                            case InstructionSet.X86_X86Base_X64: return null;
                            case InstructionSet.X86_SSE42: return ReadyToRunInstructionSet.Sse42;
                            case InstructionSet.X86_SSE42_X64: return null;
                            case InstructionSet.X86_AVX: return ReadyToRunInstructionSet.Avx;
                            case InstructionSet.X86_AVX_X64: return null;
                            case InstructionSet.X86_AVX2: return ReadyToRunInstructionSet.Avx2;
                            case InstructionSet.X86_AVX2_X64: return null;
                            case InstructionSet.X86_AVX512: return ReadyToRunInstructionSet.Evex;
                            case InstructionSet.X86_AVX512_X64: return null;
                            case InstructionSet.X86_AVX512v2: return ReadyToRunInstructionSet.Avx512Ifma;
                            case InstructionSet.X86_AVX512v2_X64: return null;
                            case InstructionSet.X86_AVX512v3: return ReadyToRunInstructionSet.Avx512Bitalg;
                            case InstructionSet.X86_AVX512v3_X64: return null;
                            case InstructionSet.X86_AVX10v1: return ReadyToRunInstructionSet.Avx512Bf16;
                            case InstructionSet.X86_AVX10v1_X64: return null;
                            case InstructionSet.X86_AVX10v2: return ReadyToRunInstructionSet.Avx10v2;
                            case InstructionSet.X86_AVX10v2_X64: return null;
                            case InstructionSet.X86_APX: return ReadyToRunInstructionSet.Apx;
                            case InstructionSet.X86_AES: return ReadyToRunInstructionSet.Aes;
                            case InstructionSet.X86_AES_X64: return null;
                            case InstructionSet.X86_AES_V256: return ReadyToRunInstructionSet.Aes_V256;
                            case InstructionSet.X86_AES_V512: return ReadyToRunInstructionSet.Aes_V512;
                            case InstructionSet.X86_AVX512VP2INTERSECT: return ReadyToRunInstructionSet.Avx512Vp2intersect;
                            case InstructionSet.X86_AVX512VP2INTERSECT_X64: return null;
                            case InstructionSet.X86_AVXIFMA: return ReadyToRunInstructionSet.AvxIfma;
                            case InstructionSet.X86_AVXIFMA_X64: return null;
                            case InstructionSet.X86_AVXVNNI: return ReadyToRunInstructionSet.AvxVnni;
                            case InstructionSet.X86_AVXVNNI_X64: return null;
                            case InstructionSet.X86_GFNI: return ReadyToRunInstructionSet.Gfni;
                            case InstructionSet.X86_GFNI_X64: return null;
                            case InstructionSet.X86_GFNI_V256: return ReadyToRunInstructionSet.Gfni_V256;
                            case InstructionSet.X86_GFNI_V512: return ReadyToRunInstructionSet.Gfni_V512;
                            case InstructionSet.X86_SHA: return ReadyToRunInstructionSet.Sha;
                            case InstructionSet.X86_SHA_X64: return null;
                            case InstructionSet.X86_WAITPKG: return ReadyToRunInstructionSet.WaitPkg;
                            case InstructionSet.X86_WAITPKG_X64: return null;
                            case InstructionSet.X86_X86Serialize: return ReadyToRunInstructionSet.X86Serialize;
                            case InstructionSet.X86_X86Serialize_X64: return null;
                            case InstructionSet.X86_Vector128: return null;
                            case InstructionSet.X86_Vector256: return null;
                            case InstructionSet.X86_Vector512: return null;
                            case InstructionSet.X86_VectorT128: return ReadyToRunInstructionSet.VectorT128;
                            case InstructionSet.X86_VectorT256: return ReadyToRunInstructionSet.VectorT256;
                            case InstructionSet.X86_VectorT512: return ReadyToRunInstructionSet.VectorT512;
                            case InstructionSet.X86_AVXVNNIINT: return ReadyToRunInstructionSet.AvxVnniInt8;
                            case InstructionSet.X86_AVXVNNIINT_V512: return ReadyToRunInstructionSet.AvxVnniInt8_V512;

                            default: throw new Exception("Unknown instruction set");
                        }
                    }

                default: throw new Exception("Unknown architecture");
            }
        }
    }
}
