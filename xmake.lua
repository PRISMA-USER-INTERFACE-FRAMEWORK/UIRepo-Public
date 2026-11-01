set_xmakever("3.0.0")
set_project("UIRepo")
set_version("0.1.0")
set_languages("c++23")
set_encodings("utf-8")

add_rules("mode.release", "mode.releasedbg", "mode.debug")

add_repositories("uirepo-local xmake/local-repo")
includes("xmake/retained_animation.lua")

if is_plat("windows") then
    add_requires("minhook v1.3.4")
end

target("uirepo_core")
    set_kind("static")
    add_includedirs("native/cpp/include", { public = true })
    add_files("native/cpp/src/Core.cpp", "native/cpp/src/Controls.cpp", "native/cpp/src/Animation.cpp", "native/cpp/src/Document.cpp", "native/cpp/src/Layout.cpp", "native/cpp/src/StyleValidation.cpp", "native/cpp/src/Render.cpp")

    if is_plat("windows") then
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_cxflags("/permissive-", "/W4")
    end

target_end()

target("uirepo_package")
    set_kind("static")
    add_includedirs("native/cpp/include", { public = true })
    add_includedirs("native/cpp/src", { private = true })
    add_files(
        "native/cpp/src/PackageHash.cpp",
        "native/cpp/src/PackageManifest.cpp",
        "native/cpp/src/PackagePath.cpp",
        "native/cpp/src/PackageReader.cpp",
        "native/cpp/src/PackageWriter.cpp"
    )

    if is_plat("windows") then
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_cxflags("/permissive-", "/W4")
    end

target_end()

target("uirepo_declarative")
    set_kind("static")
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include", { public = true })
    add_files(
        "native/cpp/src/DeclarativeFields.cpp",
        "native/cpp/src/DeclarativeJson.cpp",
        "native/cpp/src/DeclarativeParse.cpp",
        "native/cpp/src/DeclarativeSerialize.cpp",
        "native/cpp/src/DeclarativeStyle.cpp"
    )

    if is_plat("windows") then
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_cxflags("/permissive-", "/W4")
    end

target_end()

target("uirepo_package_host")
    set_kind("static")
    add_deps("uirepo_package", "uirepo_declarative", "uirepo_core")
    add_includedirs("native/cpp/include", { public = true })
    add_includedirs("native/cpp/src", { private = true })
    add_files("native/cpp/src/PackageHost.cpp")

    if is_plat("windows") then
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_cxflags("/permissive-", "/W4")
    end

target_end()

target("uirepo_package_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_package")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/PackageTests.cpp", "native/cpp/tests/PackageMalformedTests.cpp")

target_end()

target("uirepo_package_host_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_package_host")
    add_includedirs("native/cpp/include")
    add_files(
        "native/cpp/tests/PackageHostTests.cpp",
        "native/cpp/tests/PackageHostRefusalTests.cpp"
    )

target_end()

target("uirepo_package_host_runtime_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_package_host")
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/PackageHostRuntimeTests.cpp",
        "native/fo4/src/PackageHostRuntime.cpp",
        "native/fo4/src/DocumentRegistry.cpp",
        "native/fo4/src/DocumentRegistryControls.cpp",
        "native/fo4/src/DocumentRegistryLifecycle.cpp",
        "native/fo4/src/DocumentRegistrySurface.cpp",
        "native/fo4/src/DocumentRegistryText.cpp",
        "native/fo4/src/DocumentRegistryTextLayout.cpp"
    )

target_end()

target("uirepo_core_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/CoreTests.cpp")

target_end()

target("uirepo_policy_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/PolicyTests.cpp")

target_end()

target("uirepo_dispatch_lifecycle_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/DispatchLifecycleTests.cpp")

target_end()

target("uirepo_focus_lease_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/FocusLeaseTests.cpp")

target_end()

target("uirepo_active_device_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/ActiveDeviceTests.cpp")

target_end()

target("uirepo_extended_pointer_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/ExtendedPointerTests.cpp")

target_end()

target("uirepo_bounded_cache_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/BoundedCacheTests.cpp")

target_end()

target("uirepo_image_policy_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/ImagePolicyTests.cpp")

target_end()

target("uirepo_spatial_navigation_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/SpatialNavigationTests.cpp")

target_end()

target("uirepo_native_tree_foundation_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/NativeTreeFoundationTests.cpp")

target_end()

target("uirepo_native_tree_mutation_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/NativeTreeMutationTests.cpp")

target_end()

target("uirepo_animation_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/AnimationTests.cpp")

target_end()

target("uirepo_declarative_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_declarative")
    add_includedirs("native/cpp/include")
    add_files(
        "native/cpp/tests/DeclarativeTests.cpp",
        "native/cpp/tests/DeclarativeErrorTests.cpp",
        "native/cpp/tests/DeclarativePercentTests.cpp"
    )

target_end()

-- ABI negotiation and multi-consumer harness.
--
-- The consumers are separate binaries from the providers on purpose: this is
-- the only place the ABI is exercised across a real module boundary rather
-- than within one translation unit. Portable, so it runs on Linux.
local abi_harness_include = {"native/cpp/include", "sdk/include", "native/cpp/tests/abi"}
-- Same Windows defines every other target here uses. WIN32_LEAN_AND_MEAN is
-- load-bearing rather than cosmetic: without it windows.h drags in the OLE/COM
-- headers, and unknwnbase.h's QueryInterface overloads do not compile under
-- clang-cl.
local abi_harness_defines = {"WIN32_LEAN_AND_MEAN", "NOMINMAX"}

target("uirepo_abi_provider_v1")
    set_kind("shared")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/ProviderV1.cpp")
    add_defines(abi_harness_defines)
    -- These targets implement the API, so the public header must declare its
    -- entry point as exported rather than imported.
    add_defines("UIREPO_NATIVE_API_BUILD")
target_end()

target("uirepo_abi_provider_v2")
    set_kind("shared")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/ProviderV2.cpp")
    add_defines(abi_harness_defines)
    -- These targets implement the API, so the public header must declare its
    -- entry point as exported rather than imported.
    add_defines("UIREPO_NATIVE_API_BUILD")
target_end()

target("uirepo_abi_consumer_documents")
    set_kind("shared")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/ConsumerDocuments.cpp")
    add_defines(abi_harness_defines)
target_end()

target("uirepo_abi_consumer_surfaces")
    set_kind("shared")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/ConsumerSurfaces.cpp")
    add_defines(abi_harness_defines)
target_end()

target("uirepo_abi_consumer_state")
    set_kind("shared")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/ConsumerState.cpp")
    add_defines(abi_harness_defines)
target_end()

target("uirepo_abi_harness")
    set_kind("binary")
    set_default(false)
    add_includedirs(abi_harness_include)
    add_files("native/cpp/tests/abi/Harness.cpp")
    add_defines(abi_harness_defines)
    if not is_plat("windows") then
        add_syslinks("dl")
    end
-- ApiResult.h is deliberately free of Windows and D3D11 dependencies so the
-- result-code semantics can be exercised portably.
target("uirepo_api_result_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files("native/cpp/tests/ApiResultTests.cpp")
target_end()

target("uirepo_native_api_contract_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files("native/cpp/tests/NativeApiContractTests.cpp")

target_end()

target("uirepo_native_api_v2_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/NativeApiV2Tests.cpp",
        "native/fo4/src/DocumentRegistry.cpp",
        "native/fo4/src/DocumentRegistryControls.cpp",
        "native/fo4/src/DocumentRegistryLifecycle.cpp",
        "native/fo4/src/DocumentRegistrySurface.cpp",
        "native/fo4/src/DocumentRegistryText.cpp",
        "native/fo4/src/DocumentRegistryTextLayout.cpp"
    )

target_end()

target("uirepo_render_hook_broker_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include")
    add_files(
        "native/cpp/tests/RenderHookBrokerTests.cpp",
        "native/cpp/src/RenderHookBroker.cpp"
    )

target_end()

target("uirepo_active_device_state_tests")
    set_kind("binary")
    set_default(false)
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/ActiveDeviceStateTests.cpp",
        "native/fo4/src/ActiveDeviceState.cpp"
    )

target_end()

target("uirepo_input_lifecycle_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/InputLifecycleTests.cpp",
        "native/fo4/src/DocumentRegistry.cpp",
        "native/fo4/src/DocumentRegistryKey.cpp",
        "native/fo4/src/DocumentRegistryControls.cpp",
        "native/fo4/src/DocumentRegistryText.cpp",
        "native/fo4/src/DocumentRegistryTextLayout.cpp"
    )

target_end()

target("uirepo_control_registry_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/ControlRegistryTests.cpp",
        "native/fo4/src/DocumentRegistry.cpp",
        "native/fo4/src/DocumentRegistryControls.cpp",
        "native/fo4/src/DocumentRegistryText.cpp",
        "native/fo4/src/DocumentRegistryTextLayout.cpp"
    )

target_end()

target("uirepo_registry_lifecycle_tests")
    set_kind("binary")
    set_default(false)
    add_deps("uirepo_core")
    add_includedirs("native/cpp/include", "native/fo4/src")
    add_files(
        "native/cpp/tests/RegistryLifecycleTests.cpp",
        "native/fo4/src/DocumentRegistry.cpp",
        "native/fo4/src/DocumentRegistryControls.cpp",
        "native/fo4/src/DocumentRegistryLifecycle.cpp",
        "native/fo4/src/DocumentRegistrySurface.cpp",
        "native/fo4/src/DocumentRegistryText.cpp",
        "native/fo4/src/DocumentRegistryTextLayout.cpp"
    )

target_end()

if is_plat("windows") then
    target("uirepo_render_d3d11")
        set_kind("static")
        add_deps("uirepo_core")
        add_includedirs("native/cpp/include", { public = true })
        add_includedirs("native/cpp/src", { private = true })
        add_files(
            "native/cpp/src/D3D11Renderer.cpp",
            "native/cpp/src/D3D11ImageCache.cpp",
            "native/cpp/src/D3D11TextLayout.cpp",
            "native/cpp/src/D3D11StateGuard.cpp",
            "native/cpp/src/D3D11SurfaceManager.cpp"
        )
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_cxflags("/permissive-", "/W4")
    target_end()

    target("uirepo_d3d11_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11RendererTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_target_color_space_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11TargetColorSpaceTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_debug_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11DebugLayerTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_image_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11ImageTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_text_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11TextLayoutTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_surface_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11SurfaceTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    -- The device-identity rule the engine target check depends on. Windows-only
    -- because it is a COM rule; needs a real device, so it runs on WARP.
    target("uirepo_com_identity_tests")
        set_kind("binary")
        set_default(false)
        add_includedirs("native/cpp/include", "native/fo4/src")
        add_files("native/cpp/tests/ComIdentityTests.cpp")
        add_syslinks("d3d11", "dxgi")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    target("uirepo_d3d11_surface_device_tests")
        set_kind("binary")
        set_default(false)
        add_deps("uirepo_render_d3d11")
        add_includedirs("native/cpp/include")
        add_files("native/cpp/tests/D3D11SurfaceDeviceTests.cpp")
        add_syslinks("d3d11", "d3dcompiler", "dwrite", "dxgi", "ole32", "windowscodecs")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()

    -- The SDK sample builds against the STAGED package (build/sdk/include),
    -- not the source tree, so anything missing from the package breaks this
    -- target instead of being quietly supplied by the repository.
    -- Run scripts/package-sdk.sh first.
    -- It links nothing from the framework, which is the point: a consumer has
    -- no build-time dependency on UIRepo at all.
    target("uirepo_sdk_sample")
        set_kind("shared")
        set_default(false)
        add_includedirs("build/sdk/include")
        add_files("sdk/samples/minimal/src/main.cpp")
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    target_end()
end

local commonlib_pin = "7351f6d4e8a9b7c3faf47dbdb8f7713a035999e7"
local build_sha = os.getenv("GITHUB_SHA") or "local"
build_sha = build_sha:gsub("[^%w%._%-]", "_")

local commonlib_path = os.getenv("UIREPO_COMMONLIBF4_PATH")
if not commonlib_path or not os.isfile(path.join(commonlib_path, "xmake.lua")) then
    local vendored_path = path.join(os.scriptdir(), "lib", "commonlibf4")
    if os.isfile(path.join(vendored_path, "xmake.lua")) then
        commonlib_path = vendored_path
    else
        commonlib_path = nil
    end
end

if commonlib_path then
    local actual_commonlib_pin = nil
    if os.getenv("GITHUB_ACTIONS") == "true" then
        actual_commonlib_pin = os.getenv("UIREPO_COMMONLIBF4_VERIFIED_SHA")
    end

    if actual_commonlib_pin then
        actual_commonlib_pin = actual_commonlib_pin:gsub("%s+", "")
        if actual_commonlib_pin ~= commonlib_pin then
            raise(
                "Preverified CommonLibF4 SHA mismatch: expected " .. commonlib_pin .. ", got " .. actual_commonlib_pin
            )
        end
    else
        actual_commonlib_pin = os.iorunv(
            "git",
            { "-C", commonlib_path, "rev-parse", "HEAD" }
        ):gsub("%s+", "")
        if actual_commonlib_pin ~= commonlib_pin then
            raise(
                "CommonLibF4 checkout mismatch: expected " .. commonlib_pin .. ", got " .. actual_commonlib_pin
            )
        end
    end

    includes(commonlib_path)
    add_defines("COMMONLIB_RUNTIMECOUNT=3")

    if is_plat("windows") then
        target("UIRepo_F4")
            set_version("0.1.0")
            set_filename("UIRepo_F4.dll")
            set_symbols("debug")

            add_rules("commonlibf4.plugin", {
                name = "UIRepo_F4",
                author = "PRISMA-USER-INTERFACE-FRAMEWORK",
                description = "Native retained-mode UI framework for Fallout 4"
            })

            add_deps("uirepo_render_d3d11", "uirepo_package_host")
            add_packages("minhook")
            add_includedirs(
                "native/cpp/include",
                "native/fo4/src"
            )
            set_pcxxheader("native/fo4/src/PCH.h")
            add_files("native/fo4/src/**.cpp")
            add_syslinks("comctl32", "d3d11", "d3dcompiler", "dwrite", "dxgi", "imm32", "ole32", "windowscodecs")
            add_defines(
                "WIN32_LEAN_AND_MEAN",
                "NOMINMAX",
                "UNICODE",
                "_UNICODE",
                "UIREPO_NATIVE_API_BUILD",
                'UIREPO_COMMONLIBF4_PIN=\"' .. actual_commonlib_pin .. '\"',
                'UIREPO_BUILD_SHA=\"' .. build_sha .. '\"'
            )
            add_cxflags("/W4", "/Z7", { force = true })
            add_ldflags("/DEBUG", { force = true })
            -- Strip the build-machine path from the DLL's embedded PDB reference so shipped
            -- binaries point at a bare "UIRepo_F4.pdb" for crash-dump symbol resolution.
            add_shflags("/PDBALTPATH:UIRepo_F4.pdb", { force = true })
        target_end()

        -- The first consumer of the public ABI that runs in the game.
        --
        -- It sees the SDK headers and nothing else: no framework sources, no
        -- internal includes, no link against UIRepo_F4. If a change breaks
        -- what a mod author can build, it breaks here.
        target("UIRepoSample_F4")
            set_version("0.1.0")
            set_filename("UIRepoSample_F4.dll")
            set_symbols("debug")

            add_rules("commonlibf4.plugin", {
                name = "UIRepoSample_F4",
                author = "PRISMA-USER-INTERFACE-FRAMEWORK",
                description = "Minimal consumer of the UIRepo native ABI"
            })

            add_includedirs("native/cpp/include", "sdk/include")
            add_files("sdk/samples/f4se-plugin/src/main.cpp")
            add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE")
            add_cxflags("/W4", "/Z7", { force = true })
            add_ldflags("/DEBUG", { force = true })
            add_shflags("/PDBALTPATH:UIRepoSample_F4.pdb", { force = true })
        target_end()
    end
else
    print("NOTE: UIRepo_F4 target disabled: run scripts/setup-commonlib.ps1 or set UIREPO_COMMONLIBF4_PATH")
end
