// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma warning disable CS0067 // events are declared but not used

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.Loader;
using System.Runtime.Remoting;
using System.Security;
using System.Security.Permissions;
using System.Security.Principal;
using System.Threading;

namespace System
{
    public sealed partial class AppDomain : MarshalByRefObject
    {
        private static AppDomain? s_domain;

        private IPrincipal? _defaultPrincipal;
        private PrincipalPolicy _principalPolicy = PrincipalPolicy.NoPrincipal;

        private AppDomain() { }

        public static AppDomain CurrentDomain
        {
            get
            {
                // Create AppDomain instance only once external code asks for it. AppDomain brings lots of unnecessary
                // dependencies into minimal trimmed app via ToString method.
                if (s_domain == null)
                {
                    Interlocked.CompareExchange(ref s_domain, new AppDomain(), null);
                }
                return s_domain;
            }
        }

        public string BaseDirectory => AppContext.BaseDirectory;

        public string? RelativeSearchPath => null;

        public AppDomainSetup SetupInformation => new AppDomainSetup();

        [Obsolete(Obsoletions.CodeAccessSecurityMessage, DiagnosticId = Obsoletions.CodeAccessSecurityDiagId, UrlFormat = Obsoletions.SharedUrlFormat)]
        public PermissionSet PermissionSet => new PermissionSet(PermissionState.Unrestricted);

        public event UnhandledExceptionEventHandler? UnhandledException
        {
            add { AppContext.UnhandledException += value; }
            remove { AppContext.UnhandledException -= value; }
        }

        public string? DynamicDirectory => null;

        [Obsolete("AppDomain.SetDynamicBase has been deprecated and is not supported.")]
        public void SetDynamicBase(string? path) { }

        public string FriendlyName
        {
            get
            {
                Assembly? assembly = Assembly.GetEntryAssembly();
                return assembly != null ? assembly.GetName().Name! : "DefaultDomain";
            }
        }

        public int Id => 1;

        public bool IsFullyTrusted => true;

        public bool IsHomogenous => true;

        public event EventHandler? DomainUnload;

        public event EventHandler<FirstChanceExceptionEventArgs>? FirstChanceException
        {
            add { AppContext.FirstChanceException += value; }
            remove { AppContext.FirstChanceException -= value; }
        }

        public event EventHandler? ProcessExit;

        internal static void OnProcessExit()
        {
            AppDomain? domain = s_domain;
            domain?.ProcessExit?.Invoke(domain, EventArgs.Empty);
        }

        public string ApplyPolicy(string assemblyName)
        {
            ArgumentException.ThrowIfNullOrEmpty(assemblyName);
            if (assemblyName[0] == '\0')
            {
                throw new ArgumentException(SR.Argument_EmptyString, nameof(assemblyName));
            }

            return assemblyName;
        }

        [Obsolete(Obsoletions.AppDomainCreateUnloadMessage, DiagnosticId = Obsoletions.AppDomainCreateUnloadDiagId, UrlFormat = Obsoletions.SharedUrlFormat)]
        public static AppDomain CreateDomain(string friendlyName)
        {
            ArgumentNullException.ThrowIfNull(friendlyName);

            throw new PlatformNotSupportedException(SR.PlatformNotSupported_AppDomains);
        }

        [RequiresUnreferencedCode("Types and members the loaded assembly depends on might be removed")]
        public int ExecuteAssembly(string assemblyFile) => ExecuteAssembly(assemblyFile, null);

        [RequiresUnreferencedCode("Types and members the loaded assembly depends on might be removed")]
        public int ExecuteAssembly(string assemblyFile, string?[]? args)
        {
            ArgumentNullException.ThrowIfNull(assemblyFile);

            string fullPath = Path.GetFullPath(assemblyFile);
            Assembly assembly = Assembly.LoadFile(fullPath);
            return ExecuteAssembly(assembly, args);
        }

        [RequiresUnreferencedCode("Types and members the loaded assembly depends on might be removed")]
        [Obsolete(Obsoletions.CodeAccessSecurityMessage, DiagnosticId = Obsoletions.CodeAccessSecurityDiagId, UrlFormat = Obsoletions.SharedUrlFormat)]
        public int ExecuteAssembly(string assemblyFile, string?[]? args, byte[]? hashValue, Configuration.Assemblies.AssemblyHashAlgorithm hashAlgorithm)
        {
            throw new PlatformNotSupportedException(SR.PlatformNotSupported_CAS); // This api is only meaningful for very specific partial trust/CAS scenarios
        }

        private static int ExecuteAssembly(Assembly assembly, string?[]? args)
        {
            MethodInfo entry = assembly.EntryPoint ??
                throw new MissingMethodException(SR.Arg_EntryPointNotFoundException);

            object? result = entry.Invoke(
                obj: null,
                invokeAttr: BindingFlags.DoNotWrapExceptions,
                binder: null,
                parameters: entry.GetParametersAsSpan().Length > 0 ? [args] : null,
                culture: null);

            return result != null ? (int)result : 0;
        }

        public int ExecuteAssemblyByName(AssemblyName assemblyName, params string?[]? args) =>
            ExecuteAssembly(Assembly.Load(assemblyName), args);

        public int ExecuteAssemblyByName(string assemblyName) =>
            ExecuteAssemblyByName(assemblyName, null);

        public int ExecuteAssemblyByName(string assemblyName, params string?[]? args) =>
            ExecuteAssembly(Assembly.Load(assemblyName), args);

        public object? GetData(string name) => AppContext.GetData(name);

        public void SetData(string name, object? data) => AppContext.SetData(name, data);

        public bool? IsCompatibilitySwitchSet(string value)
        {
            return AppContext.TryGetSwitch(value, out bool result) ? result : default(bool?);
        }

        public bool IsDefaultAppDomain() => true;

        public bool IsFinalizingForUnload() => false;

        public override string ToString() =>
            SR.AppDomain_Name + FriendlyName + Environment.NewLineConst + SR.AppDomain_NoContextPolicies;

        [Obsolete(Obsoletions.AppDomainCreateUnloadMessage, DiagnosticId = Obsoletions.AppDomainCreateUnloadDiagId, UrlFormat = Obsoletions.SharedUrlFormat)]
        public static void Unload(AppDomain domain)
        {
            ArgumentNullException.ThrowIfNull(domain);

            throw new CannotUnloadAppDomainException(SR.Arg_PlatformNotSupported);
        }

        [RequiresUnreferencedCode("Types and members the loaded assembly depends on might be removed")]
        public Assembly Load(byte[] rawAssembly) => Assembly.Load(rawAssembly);

        [RequiresUnreferencedCode("Types and members the loaded assembly depends on might be removed")]
        public Assembly Load(byte[] rawAssembly, byte[]? rawSymbolStore) => Assembly.Load(rawAssembly, rawSymbolStore);

        public Assembly Load(AssemblyName assemblyRef) => Assembly.Load(assemblyRef);

        public Assembly Load(string assemblyString) => Assembly.Load(assemblyString);

        public Assembly[] ReflectionOnlyGetAssemblies() => Array.Empty<Assembly>();

        public static bool MonitoringIsEnabled
        {
            get => true;
            set
            {
                if (!value)
                {
                    throw new ArgumentException(SR.Arg_MustBeTrue);
                }
            }
        }

        public long MonitoringSurvivedMemorySize => MonitoringSurvivedProcessMemorySize;

        public static long MonitoringSurvivedProcessMemorySize
        {
            get
            {
                GCMemoryInfo mi = GC.GetGCMemoryInfo();
                return mi.HeapSizeBytes - mi.FragmentedBytes;
            }
        }

        public long MonitoringTotalAllocatedMemorySize => GC.GetTotalAllocatedBytes(precise: false);

        [Obsolete("AppDomain.GetCurrentThreadId has been deprecated because it does not provide a stable Id when managed threads are running on fibers (aka lightweight threads). To get a stable identifier for a managed thread, use the ManagedThreadId property on Thread instead.")]
        public static int GetCurrentThreadId() => Environment.CurrentManagedThreadId;

        public bool ShadowCopyFiles => false;

        [Obsolete("AppDomain.AppendPrivatePath has been deprecated and is not supported.")]
        public void AppendPrivatePath(string? path) { }

        [Obsolete("AppDomain.ClearPrivatePath has been deprecated and is not supported.")]
        public void ClearPrivatePath() { }

        [Obsolete("AppDomain.ClearShadowCopyPath has been deprecated and is not supported.")]
        public void ClearShadowCopyPath() { }

        [Obsolete("AppDomain.SetCachePath has been deprecated and is not supported.")]
        public void SetCachePath(string? path) { }

        [Obsolete("AppDomain.SetShadowCopyFiles has been deprecated and is not supported.")]
        public void SetShadowCopyFiles() { }

        [Obsolete("AppDomain.SetShadowCopyPath has been deprecated and is not supported.")]
        public void SetShadowCopyPath(string? path) { }

        public Assembly[] GetAssemblies() => AssemblyLoadContext.GetLoadedAssemblies();

        public event AssemblyLoadEventHandler? AssemblyLoad
        {
            add { AssemblyLoadContext.AssemblyLoad += value; }
            remove { AssemblyLoadContext.AssemblyLoad -= value; }
        }

        public event ResolveEventHandler? AssemblyResolve
        {
            add { AssemblyLoadContext.AssemblyResolve += value; }
            remove { AssemblyLoadContext.AssemblyResolve -= value; }
        }

        public event ResolveEventHandler? ReflectionOnlyAssemblyResolve;

        public event ResolveEventHandler? TypeResolve
        {
            add { AssemblyLoadContext.TypeResolve += value; }
            remove { AssemblyLoadContext.TypeResolve -= value; }
        }

        public event ResolveEventHandler? ResourceResolve
        {
            add { AssemblyLoadContext.ResourceResolve += value; }
            remove { AssemblyLoadContext.ResourceResolve -= value; }
        }

        public void SetPrincipalPolicy(PrincipalPolicy policy)
        {
            _principalPolicy = policy;
        }

        public void SetThreadPrincipal(IPrincipal principal)
        {
            ArgumentNullException.ThrowIfNull(principal);

            // Set the principal while checking it has not been set previously.
            if (Interlocked.CompareExchange(ref _defaultPrincipal, principal, null) is not null)
            {
                throw new SystemException(SR.AppDomain_Policy_PrincipalTwice);
            }
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstance(string assemblyName, string typeName)
        {
            ArgumentNullException.ThrowIfNull(assemblyName);

            return Activator.CreateInstance(assemblyName, typeName);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstance(string assemblyName, string typeName, bool ignoreCase, BindingFlags bindingAttr, Binder? binder, object?[]? args, Globalization.CultureInfo? culture, object?[]? activationAttributes)
        {
            ArgumentNullException.ThrowIfNull(assemblyName);

            return Activator.CreateInstance(assemblyName,
                                            typeName,
                                            ignoreCase,
                                            bindingAttr,
                                            binder,
                                            args,
                                            culture,
                                            activationAttributes);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstance(string assemblyName, string typeName, object?[]? activationAttributes)
        {
            ArgumentNullException.ThrowIfNull(assemblyName);

            return Activator.CreateInstance(assemblyName, typeName, activationAttributes);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceAndUnwrap(string assemblyName, string typeName)
        {
            ObjectHandle? oh = CreateInstance(assemblyName, typeName);
            return oh?.Unwrap();
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceAndUnwrap(string assemblyName, string typeName, bool ignoreCase, BindingFlags bindingAttr, Binder? binder, object?[]? args, Globalization.CultureInfo? culture, object?[]? activationAttributes)
        {
            ObjectHandle? oh = CreateInstance(assemblyName,
                                             typeName,
                                             ignoreCase,
                                             bindingAttr,
                                             binder,
                                             args,
                                             culture,
                                             activationAttributes);
            return oh?.Unwrap();
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceAndUnwrap(string assemblyName, string typeName, object?[]? activationAttributes)
        {
            ObjectHandle? oh = CreateInstance(assemblyName, typeName, activationAttributes);
            return oh?.Unwrap();
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstanceFrom(string assemblyFile, string typeName)
        {
            return Activator.CreateInstanceFrom(assemblyFile, typeName);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstanceFrom(string assemblyFile, string typeName, bool ignoreCase, BindingFlags bindingAttr, Binder? binder, object?[]? args, Globalization.CultureInfo? culture, object?[]? activationAttributes)
        {
            return Activator.CreateInstanceFrom(assemblyFile,
                                                typeName,
                                                ignoreCase,
                                                bindingAttr,
                                                binder,
                                                args,
                                                culture,
                                                activationAttributes);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public ObjectHandle? CreateInstanceFrom(string assemblyFile, string typeName, object?[]? activationAttributes)
        {
            return Activator.CreateInstanceFrom(assemblyFile, typeName, activationAttributes);
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceFromAndUnwrap(string assemblyFile, string typeName)
        {
            ObjectHandle? oh = CreateInstanceFrom(assemblyFile, typeName);
            return oh?.Unwrap();
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceFromAndUnwrap(string assemblyFile, string typeName, bool ignoreCase, BindingFlags bindingAttr, Binder? binder, object?[]? args, Globalization.CultureInfo? culture, object?[]? activationAttributes)
        {
            ObjectHandle? oh = CreateInstanceFrom(assemblyFile,
                                                 typeName,
                                                 ignoreCase,
                                                 bindingAttr,
                                                 binder,
                                                 args,
                                                 culture,
                                                 activationAttributes);
            return oh?.Unwrap();
        }

        [RequiresUnreferencedCode("Type and its constructor could be removed")]
        public object? CreateInstanceFromAndUnwrap(string assemblyFile, string typeName, object?[]? activationAttributes)
        {
            ObjectHandle? oh = CreateInstanceFrom(assemblyFile, typeName, activationAttributes);
            return oh?.Unwrap();
        }

        internal IPrincipal? GetThreadPrincipal()
        {
            IPrincipal? principal = _defaultPrincipal;
            if (principal == null)
            {
                switch (_principalPolicy)
                {
                    case PrincipalPolicy.UnauthenticatedPrincipal:
                        principal = (IPrincipal)GetDefaultPrincipal(null);
                        break;
                    case PrincipalPolicy.WindowsPrincipal:
#if TARGET_WINDOWS
                        principal = (IPrincipal)GetDefaultWindowsPrincipal(null);
                        break;
#else
                        // WindowsPrincipal is not available, throw PNSE
                        throw new PlatformNotSupportedException(SR.PlatformNotSupported_Principal);
#endif
                }
            }

            return principal;

            [UnsafeAccessor(UnsafeAccessorKind.StaticMethod, Name = "GetDefaultInstance")]
            [return: UnsafeAccessorType("System.Security.Principal.GenericPrincipal, System.Security.Claims")]
            static extern object GetDefaultPrincipal(
                [UnsafeAccessorType("System.Security.Principal.GenericPrincipal, System.Security.Claims")] object? _);

#if TARGET_WINDOWS
            [UnsafeAccessor(UnsafeAccessorKind.StaticMethod, Name = "GetDefaultInstance")]
            [return: UnsafeAccessorType("System.Security.Principal.WindowsPrincipal, System.Security.Principal.Windows")]
            static extern object GetDefaultWindowsPrincipal(
                [UnsafeAccessorType("System.Security.Principal.WindowsPrincipal, System.Security.Principal.Windows")] object? _);
#endif
        }
    }
}
