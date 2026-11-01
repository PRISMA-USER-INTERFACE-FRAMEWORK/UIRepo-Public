-- Custom xmake toolchain: cross-compile to Windows x64 (MSVC ABI) from Linux with
-- clang-cl + lld-link and the xwin-provided CRT/SDK. Mirrors cmake/xwin-clang-cl.cmake.
--
-- xmake has no clang-cl toolchain registered on Linux (it wants vcvars), and its
-- builtin find_tool("clang-cl") only probes Visual Studio install paths. We resolve
-- the LLVM programs from PATH with find_program instead, and install this file as a
-- GLOBAL toolchain ($(globaldir)/toolchains/xwin/xmake.lua) so xmake package builds
-- (minhook, spdlog, ...) can resolve it too.
toolchain("xwin", function ()
    set_kind("standalone")

    -- Package builds reject a toolchain whose check() is false; clang-cl on PATH
    -- is sufficient proof it is usable here.
    on_check(function (toolchain)
        return import("lib.detect.find_program")("clang-cl") ~= nil
    end)

    on_load(function (toolchain)
        import("lib.detect.find_program")
        local function prog(name)
            return find_program(name) or name
        end

        -- Pin absolute program paths (find_tool cannot locate clang-cl on Linux)
        -- and, where the basename does not map to the right flag module, hint the
        -- xmake toolname explicitly via "toolname@program".
        --   cc/cxx  -> clang-cl basename already maps to the cl-style module
        --   ld/sh   -> lld_link module drives lld-link
        --   ar      -> lld-link has no /lib mode; use llvm-ar (COFF archive that
        --              lld-link links) with xmake's llvm_ar module
        toolchain:set("toolset", "cc",  prog("clang-cl"))
        toolchain:set("toolset", "cxx", prog("clang-cl"))
        toolchain:set("toolset", "ld",  "lld_link@" .. prog("lld-link"))
        toolchain:set("toolset", "sh",  "lld_link@" .. prog("lld-link"))
        toolchain:set("toolset", "ar",  "llvm_ar@" .. prog("llvm-ar"))
        toolchain:set("toolset", "mrc", "llvm_rc@" .. prog("llvm-rc"))

        local xwin = os.getenv("XWIN_ROOT") or "/opt/xwin"

        -- Compiler: target the Windows MSVC ABI and add the xwin header roots as
        -- system includes (/imsvc keeps them out of warning/-Werror scope).
        toolchain:add("cxflags", "--target=x86_64-pc-windows-msvc", {force = true})
        toolchain:add("cxflags", "-fms-compatibility", "-fms-extensions", {force = true})
        for _, inc in ipairs({
            "/imsvc" .. xwin .. "/crt/include",
            "/imsvc" .. xwin .. "/sdk/include/ucrt",
            "/imsvc" .. xwin .. "/sdk/include/um",
            "/imsvc" .. xwin .. "/sdk/include/shared",
            "/imsvc" .. xwin .. "/sdk/include/winrt",
        }) do
            toolchain:add("cxflags", inc, {force = true})
        end

        -- Resource compiler (llvm-rc) needs the SDK headers too (commonlib's
        -- generated .rc pulls winres.h). cxflags above only reach the C/C++ driver,
        -- so hand the same include roots to the rc tool as -I flags.
        for _, inc in ipairs({
            xwin .. "/crt/include",
            xwin .. "/sdk/include/ucrt",
            xwin .. "/sdk/include/um",
            xwin .. "/sdk/include/shared",
            xwin .. "/sdk/include/winrt",
        }) do
            toolchain:add("mrcflags", "-I" .. inc, {force = true})
        end

        -- Linker (both exe and shared): xwin import/static library roots.
        for _, lp in ipairs({
            "/libpath:" .. xwin .. "/crt/lib/x86_64",
            "/libpath:" .. xwin .. "/sdk/lib/ucrt/x86_64",
            "/libpath:" .. xwin .. "/sdk/lib/um/x86_64",
        }) do
            toolchain:add("ldflags", lp, {force = true})
            toolchain:add("shflags", lp, {force = true})
        end
    end)
end)
