from pathlib import Path
import uuid
root=Path(__file__).resolve().parents[1]
project_type='{BC8A1FFA-BEE3-4634-8014-F334798102B3}'
projects=[
    ('HvCore','src/HvCore/HvCore.vcxproj','StaticLibrary',[
     'src/HvCore/src/types.cpp','src/HvCore/src/paging.cpp',
     'src/HvCore/src/cpu.cpp','src/HvCore/src/sync.cpp', 'src/HvCore/src/exit.cpp'],
     ['src/HvCore/include/hv/types.h','src/HvCore/include/hv/paging.h',
      'src/HvCore/include/hv/cpu.h','src/HvCore/include/hv/sync.h',
      'src/HvCore/include/hv/exit.h'],None),
    ('HvTests','tests/HvTests/HvTests.vcxproj','Application',['tests/HvTests/Tests.cpp'],[],'HvCore'),
    ('HvIntrospection','src/HvIntrospection/HvIntrospection.vcxproj','StaticLibrary',
     ['src/HvIntrospection/src/introspection.cpp','src/HvIntrospection/src/introspection_hvcore_offline.cpp'],
     ['src/HvIntrospection/include/hv/introspection.h','src/HvIntrospection/include/hv/introspection_hvcore_offline.h'],'HvCore'),
    ('HvIntrospectionTests','tests/HvIntrospectionTests/HvIntrospectionTests.vcxproj','Application',
     ['tests/HvIntrospectionTests/Tests.cpp'],[],['HvCore','HvIntrospection']),
    ('HvKernelTransport','src/HvKernelTransport/HvKernelTransport.vcxproj','StaticLibrary',
     ['src/HvKernelTransport/src/kernel_protocol.cpp','src/HvKernelTransport/src/kernel_client_win32.cpp'],
     ['src/HvKernelTransport/include/hv/kernel_protocol.h','src/HvKernelTransport/include/hv/kernel_client.h'],None),
    ('HvKernelTransportTests','tests/HvKernelTransportTests/HvKernelTransportTests.vcxproj','Application',
     ['tests/HvKernelTransportTests/Tests.cpp'],[],'HvKernelTransport'),
    ('HvKernelProbe','apps/HvKernelProbe/HvKernelProbe.vcxproj','Application',
     ['apps/HvKernelProbe/Main.cpp'],[],'HvKernelTransport'),
    ('HvTelemetryPublisher','apps/HvTelemetryPublisher/HvTelemetryPublisher.vcxproj','Application',
     ['apps/HvTelemetryPublisher/Main.cpp'],[],'HvKernelTransport'),
    ('HvProcessManager','src/HvProcessManager/HvProcessManager.vcxproj' ,'StaticLibrary',
     ['src/HvProcessManager/src/process.cpp','src/HvProcessManager/src/introspection_win32.cpp'],
     ['src/HvProcessManager/include/hv/process.h','src/HvProcessManager/include/hv/introspection_win32.h'],
     'HvIntrospection'),
    ('HvProcessInspector','apps/HvProcessInspector/HvProcessInspector.vcxproj','Application',
     ['apps/HvProcessInspector/Main.cpp'],[], 'HvProcessManager'),
    ('HvProcessTests','tests/HvProcessTests/HvProcessTests.vcxproj','Application',
     ['tests/HvProcessTests/Tests.cpp'],[], 'HvProcessManager'),
    ('HvProbe','apps/HvProbe/HvProbe.vcxproj','Application',['apps/HvProbe/Main.cpp'],[],'HvCore'),
    ('HvBoxDemo','apps/HvBoxDemo/HvBoxDemo.vcxproj','Application',['apps/HvBoxDemo/Main.cpp'],[],['HvCore','HvProcessManager']),
    ('HvGameOverlay','apps/HvGameOverlay/HvGameOverlay.vcxproj','Application',
     ['apps/HvGameOverlay/src/main.cpp','apps/HvGameOverlay/src/game.cpp',
      'apps/HvGameOverlay/src/render.cpp','apps/HvGameOverlay/src/geometry.cpp'],
     ['apps/HvGameOverlay/include/ourcute/config.h','apps/HvGameOverlay/include/ourcute/game.h',
      'apps/HvGameOverlay/include/ourcute/geometry.h','apps/HvGameOverlay/include/ourcute/render.h'],
     ['HvProcessManager','HvIntrospection','HvCore','HvKernelTransport']),
    ('HvGameTests','tests/HvGameTests/HvGameTests.vcxproj','Application',
     ['tests/HvGameTests/Tests.cpp','apps/HvGameOverlay/src/geometry.cpp'],
     ['apps/HvGameOverlay/include/ourcute/geometry.h'],None),
    ('HvWHPGuest','apps/HvWHPGuest/HvWHPGuest.vcxproj','Application',
     ['apps/HvWHPGuest/Main.cpp'],['apps/HvWHPGuest/GuestState.h'],None),
    ('HvWHPGuestTests','tests/HvWHPGuestTests/HvWHPGuestTests.vcxproj','Application',
     ['tests/HvWHPGuestTests/Tests.cpp'],['apps/HvWHPGuest/GuestState.h'],None),
    ('HvProbeDriver','drivers/HvProbeDriver/HvProbeDriver.vcxproj','Driver',
     ['drivers/HvProbeDriver/Driver.cpp','src/HvKernelTransport/src/kernel_protocol.cpp'],[],None),
]
guids={name:'{'+str(uuid.uuid5(uuid.NAMESPACE_URL,'https://example.local/hvlab/'+name)).upper()+'}' for name,*_ in projects}

def emit_project(name,relpath,typ,sources,headers,ref):
    path=root/relpath
    sdk='WindowsKernelModeDriver10.0' if typ=='Driver' else 'v143'
    kind='Driver' if typ=='Driver' else typ
    ntarget='    <TargetVersion>Windows10</TargetVersion>\n    <DriverType>WDM</DriverType>\n    <DriverTargetPlatform>Desktop</DriverTargetPlatform>\n' if typ=='Driver' else ''
    subsystem='Windows' if name in ('HvBoxDemo','HvGameOverlay') else 'Console'
    if name in ('HvCore','HvIntrospection','HvProcessManager','HvKernelTransport'): subsystem='NotSet'
    link_add='WinHvPlatform.lib;%(AdditionalDependencies)' if name=='HvWHPGuest' else ('user32.lib;gdi32.lib;%(AdditionalDependencies)' if name in ('HvBoxDemo','HvGameOverlay') else ('Wdmsec.lib;%(AdditionalDependencies)' if typ=='Driver' else '%(AdditionalDependencies)'))
    core_includes='$(SolutionDir)src\\HvKernelTransport\\include;$(SolutionDir)src\\HvCore\\include;$(SolutionDir)src\\HvIntrospection\\include;$(SolutionDir)src\\HvProcessManager\\include;$(SolutionDir)apps\\HvGameOverlay\\include;%(AdditionalIncludeDirectories)'
    p=[f'''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{guids[name]}</ProjectGuid>
    <RootNamespace>{name}</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
''']
    for conf in ['Debug','Release']:
        debug='true' if conf=='Debug' else 'false'
        p.append(f'''  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='{conf}|x64'" Label="Configuration">
    <ConfigurationType>{kind}</ConfigurationType>
    <UseDebugLibraries>{debug}</UseDebugLibraries>
    <PlatformToolset>{sdk}</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
{ntarget}  </PropertyGroup>
''')
    p.append('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />\n  <ImportGroup Label="ExtensionSettings" />\n  <ImportGroup Label="Shared" />\n')
    for conf in ['Debug','Release']:
        p.append(f'''  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='{conf}|x64'">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
''')
    p.append('  <PropertyGroup Label="UserMacros" />\n')
    p.append('  <PropertyGroup>\n    <OutDir>$(SolutionDir)out\\$(Platform)\\$(Configuration)\\</OutDir>\n    <IntDir>$(SolutionDir)out\\obj\\$(ProjectName)\\$(Platform)\\$(Configuration)\\</IntDir>\n  </PropertyGroup>\n')
    for conf in ['Debug','Release']:
        debug=conf=='Debug'
        p.append(f'''  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='{conf}|x64'">
    <ClCompile>
      <WarningLevel>Level4</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <ExceptionHandling>false</ExceptionHandling>
      <RuntimeTypeInfo>false</RuntimeTypeInfo>
      <AdditionalIncludeDirectories>{core_includes}</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>{'_DEBUG' if debug else 'NDEBUG'};WIN32_LEAN_AND_MEAN;NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <Optimization>{'Disabled' if debug else 'MaxSpeed'}</Optimization>
    </ClCompile>
    <Link>
      <SubSystem>{subsystem}</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <AdditionalDependencies>{link_add}</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
''')
    if sources:
        p.append('  <ItemGroup>\n')
        for src in sources:
            relative=Path(__import__('os').path.relpath(root/src,path.parent)).as_posix().replace('/','\\')
            p.append(f'    <ClCompile Include="{relative}" />\n')
        p.append('  </ItemGroup>\n')
    if headers:
        p.append('  <ItemGroup>\n')
        for src in headers:
            relative=Path(__import__('os').path.relpath(root/src,path.parent)).as_posix().replace('/','\\')
            p.append(f'    <ClInclude Include="{relative}" />\n')
        p.append('  </ItemGroup>\n')
    for dependency in ([ref] if isinstance(ref, str) else (ref or [])):
        reftarget=next(x for x in projects if x[0]==dependency)
        relative=Path(__import__('os').path.relpath(root/reftarget[1],path.parent)).as_posix().replace('/','\\')
        p.append(f'''  <ItemGroup>
    <ProjectReference Include="{relative}">
      <Project>{guids[dependency]}</Project>
    </ProjectReference>
  </ItemGroup>
''')
    p.append('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />\n  <ImportGroup Label="ExtensionTargets" />\n</Project>\n')
    path.write_text(''.join(p),encoding='utf-8')

for item in projects:
    emit_project(*item)
lines=['Microsoft Visual Studio Solution File, Format Version 12.00',
'# Visual Studio Version 17',
'VisualStudioVersion = 17.0.31903.59',
'MinimumVisualStudioVersion = 10.0.40219.1']
for name,path,*_ in projects:
    lines.append(f'Project("{project_type}") = "{name}", "{path.replace("/",chr(92))}", "{guids[name]}"')
    lines.append('EndProject')
lines.extend(['Global',
'\tGlobalSection(SolutionConfigurationPlatforms) = preSolution',
'\t\tDebug|x64 = Debug|x64',
'\t\tRelease|x64 = Release|x64',
'\tEndGlobalSection',
'\tGlobalSection(ProjectConfigurationPlatforms) = postSolution'])
for name,*_ in projects:
    for c in ['Debug','Release']:
        lines.append(f'\t\t{guids[name]}.{c}|x64.ActiveCfg = {c}|x64')
        if name!='HvProbeDriver':
            lines.append(f'\t\t{guids[name]}.{c}|x64.Build.0 = {c}|x64')
lines.extend(['\tEndGlobalSection','\tGlobalSection(SolutionProperties) = preSolution','\t\tHideSolutionNode = FALSE','\tEndGlobalSection','EndGlobal'])
(root/'HypervisorLab.sln').write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
print('Generated solution',root/'HypervisorLab.sln')
