# UIRepo SDK sample plugin

A real F4SE plugin that draws a UI through the public UIRepo ABI. It is the
first consumer of that ABI to run in the game.

Everything it knows about UIRepo comes from two headers — `NativeApi.h` and
`NativeApiLoader.h`. It links nothing: the API is resolved at runtime from the
already-loaded framework plugin. If this builds and draws, a mod author can do
the same thing.

## What it exercises

The parts CI cannot reach:

- resolving the function table in a real process, from a genuinely separate DLL
- version negotiation against a live framework
- capability bits, tested rather than assumed
- passing `UIRepoStyleV1` by pointer across a module boundary into another DLL,
  with the exact-`size` rule enforced on the far side
- reading result codes back when a call is refused, and reporting the cause

## What you should see

A card below the framework's own probe, deliberately offset so the two are
never confused. It shows the ABI version it negotiated and the capability mask
it was handed — read from the live table, not compiled in.

## Timing

The document is built on `kPostLoadGame` and `kNewGame`, and torn down on
`kPreLoadGame`. That is not arbitrary: the framework's log shows the renderer
initialised and a probe frame rendered before those events fire, so it is the
first moment a consumer can safely build anything.

Building on plugin load does not work. The framework may not be loaded yet, and
the renderer certainly is not ready.

## Difference from `samples/minimal`

`samples/minimal` is deliberately *not* an F4SE plugin, so it can be
cross-built anywhere as a compile canary for the package. This one is a real
plugin, so it needs CommonLibF4 and only builds where that is available. Both
consume the same two headers.

## Log

`Documents/My Games/Fallout4/F4SE/UIRepoSample_F4.log`
