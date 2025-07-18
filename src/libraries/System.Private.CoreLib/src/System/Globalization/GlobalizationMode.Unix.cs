// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace System.Globalization
{
    internal static partial class GlobalizationMode
    {
        private static partial class Settings
        {
            /// <summary>
            /// Load ICU (when not in Invariant mode) in a static cctor to ensure it is loaded as side-effect of
            /// the GlobalizationMode.Invariant check. Globalization P/Invokes, e.g. in CompareInfo.GetSortKey,
            /// rely on ICU already being loaded before they are called.
            /// </summary>
#if !TARGET_MACCATALYST && !TARGET_IOS && !TARGET_TVOS
            static Settings()
            {
                // Use GlobalizationMode.Invariant to allow ICU initialization to be trimmed when Invariant=true
                // and PredefinedCulturesOnly is unspecified.
                if (!GlobalizationMode.Invariant)
                {
                    if (TryGetAppLocalIcuSwitchValue(out string? icuSuffixAndVersion))
                    {
                        LoadAppLocalIcu(icuSuffixAndVersion);
                    }
                    else
                    {
                        int loaded = LoadICU();
                        if (loaded == 0)
                        {
                            Environment.FailFast(GetIcuLoadFailureMessage());
                        }
                    }
                }
            }
#endif

            private static string GetIcuLoadFailureMessage()
            {
                // These strings can't go into resources, because a resource lookup requires globalization, which requires ICU
                if (OperatingSystem.IsBrowser() || OperatingSystem.IsWasi() || OperatingSystem.IsAndroid() ||
                    OperatingSystem.IsIOS() || OperatingSystem.IsTvOS())
                {
                    return "Unable to load required ICU Globalization data. Please see https://aka.ms/dotnet-missing-libicu for more information";
                }
                else
                {
                    return "Couldn't find a valid ICU package installed on the system. " +
                        "Please install libicu (or icu-libs) using your package manager and try again. " +
                        "Alternatively you can set the configuration flag System.Globalization.Invariant to true if you want to run with no globalization support. " +
                        "Please see https://aka.ms/dotnet-missing-libicu for more information.";
                }
            }
        }

        internal static bool UseNls => false;

        private static void LoadAppLocalIcuCore(ReadOnlySpan<char> version, ReadOnlySpan<char> suffix)
        {

#if TARGET_OSX
            const string extension = ".dylib";
            bool versionAtEnd = false;
#else
            string extension = version.Length > 0 ? "so." : "so";
            bool versionAtEnd = true;
#endif
            ReadOnlySpan<char> suffixAndSeparator = string.Concat(suffix, ".");

#if !TARGET_OSX
            // In Linux we need to load libicudata first because libicuuc and libicui18n depend on it. In order for the loader to find
            // it on the same path, we load it before loading the other two libraries.
            LoadLibrary(CreateLibraryName("libicudata", suffixAndSeparator, extension, version, versionAtEnd), failOnLoadFailure: true);
#endif

            IntPtr icuucLib = LoadLibrary(CreateLibraryName("libicuuc", suffixAndSeparator, extension, version, versionAtEnd), failOnLoadFailure: true);
            IntPtr icuinLib = LoadLibrary(CreateLibraryName("libicui18n", suffixAndSeparator, extension, version, versionAtEnd), failOnLoadFailure: true);

            Interop.Globalization.InitICUFunctions(icuucLib, icuinLib, version, suffix);
        }
    }
}
