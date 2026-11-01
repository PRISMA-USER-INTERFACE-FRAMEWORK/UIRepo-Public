-- Standalone build file for the minimal consumer.
--
-- Shipped inside the SDK package so a plugin author can see exactly what
-- integrating UIRepo costs: one include directory, no linker changes, no
-- dependency on the framework's build.

target("uirepo_sdk_sample")
    set_kind("shared")
    set_languages("c++17")
    add_includedirs("../../include")
    add_files("src/main.cpp")
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
target_end()
