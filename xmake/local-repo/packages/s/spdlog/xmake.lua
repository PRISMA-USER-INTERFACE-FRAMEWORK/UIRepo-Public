-- Local override of the xmake-repo spdlog package.
--
-- The upstream package builds the compiled library with CMake, and xmake's CMake
-- package builder hard-requires a Visual Studio install for any windows-target
-- package (buildenvs -> _get_msvc_runenvs -> assert "vs not found!"). Our clang-cl
-- + xwin cross-build has no VS. Header-only spdlog is not an option either: it pulls
-- <windows.h> into every consumer TU and its MAX_PATH macro collides with
-- CommonLibF4's REX::W32::MAX_PATH.
--
-- So build the COMPILED library with xmake-native instead (the same path minhook
-- uses successfully under the xwin toolchain). windows.h then stays inside spdlog's
-- own translation units and never leaks to CommonLibF4.
package("spdlog")
    set_homepage("https://github.com/gabime/spdlog")
    set_description("Fast C++ logging library.")
    set_license("MIT")

    add_urls("https://github.com/gabime/spdlog/archive/refs/tags/$(version).zip",
             "https://github.com/gabime/spdlog.git")
    add_versions("v1.16.0", "3d25808d2fc4db86621a46855800c99ab5734999b61c4cbf9470edf631555397")

    add_configs("header_only", {description = "Use header only version.", default = false, type = "boolean"})
    add_configs("std_format",  {description = "Use std::format instead of fmt.", default = false, type = "boolean"})
    add_configs("wchar",       {description = "Support wchar api.", default = false, type = "boolean"})

    on_load(function (package)
        if not package:config("header_only") then
            package:add("defines", "SPDLOG_COMPILED_LIB")
        end
        if package:config("std_format") then
            package:add("defines", "SPDLOG_USE_STD_FORMAT")
        end
        if package:config("wchar") then
            package:add("defines", "SPDLOG_WCHAR_TO_UTF8_SUPPORT")
        end
    end)

    on_install(function (package)
        if package:config("header_only") then
            os.cp("include", package:installdir())
            return
        end

        local defines = {"SPDLOG_COMPILED_LIB"}
        if package:config("std_format") then
            table.insert(defines, "SPDLOG_USE_STD_FORMAT")
        end
        if package:config("wchar") then
            table.insert(defines, "SPDLOG_WCHAR_TO_UTF8_SUPPORT")
        end
        local defines_str = '"' .. table.concat(defines, '", "') .. '"'

        -- With std::format the bundled fmt must not be compiled.
        local del_bundled = ""
        if package:config("std_format") then
            del_bundled = '\n    del_files("src/bundled_fmtlib_format.cpp")'
        end

        io.writefile("xmake.lua", ([[
add_rules("mode.release")
target("spdlog")
    set_kind("static")
    set_languages("c++23")
    add_files("src/*.cpp")%s
    add_includedirs("include", {public = true})
    add_headerfiles("include/(spdlog/**)")
    add_defines(%s, {public = true})
    if is_plat("windows") then
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    end
]]):format(del_bundled, defines_str))

        import("package.tools.xmake").install(package)
    end)
