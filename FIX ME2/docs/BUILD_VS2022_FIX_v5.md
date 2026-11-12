# VS2022 build fix v5 / Issue reproduction and correction

## Cause

The v4 solution generator configured `AdditionalIncludeDirectories` with `HvCore/include` and `HvProcessManager/include` **only**. However, project source files include `hv/introspection.h`, `hv/introspection_hvcore_offline.h`, and `ourcute/{config,geometry,render}.h`, which live in separate include roots.

Consequences: source compiler `C1083` for missing headers; IntelliSense `E1696` and follow-on parse errors such as `E0018`/`E0065`/`E0020`; `HvIntrospection` failed to produce `HvIntrospection.lib`, leading to `LNK1104` in dependent apps.

## Corrective action

All 12 `.vcxproj` files now have, in both `Debug|x64` and `Release|x64`, the following roots:

```xml
<AdditionalIncludeDirectories>$(SolutionDir)src\HvCore\include;$(SolutionDir)src\HvIntrospection\include;$(SolutionDir)src\HvProcessManager\include;$(SolutionDir)apps\HvGameOverlay\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
```

The canonical source for this property is `build/generate_solution.py`. Do not hand-edit only a subset of `.vcxproj` files. No absolute user paths are embedded.

## Repeatable verification on Windows

1. Close any old VS solution. Extract entire v5 archive into a new directory.
2. Start Visual Studio 2022, open `HypervisorLab.sln`, select `Debug | x64`.
3. `Build > Clean Solution` and `Build > Rebuild Solution`.
4. If errors remain, check **Output > Build** for the first compiler error rather than the final linker error.
5. Run `HvTests.exe`, `HvIntrospectionTests.exe`, `HvGameTests.exe`, and `HvGameOverlay.exe --demo`.
6. Repeat using `Release | x64` and run `build\build_vs2022.cmd` from an x64 Native Tools Command Prompt.

**Testing limitations:** The fix has been structurally verified and its portable code tested on Linux. An actual MSVC/Win32 build was not available; no claim of complete Windows runtime validation is made. The WDK driver remains optional and excluded from the default solution build.
