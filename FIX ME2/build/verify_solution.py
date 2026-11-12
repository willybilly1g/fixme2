#!/usr/bin/env python3
"""Hermetic structural VS2022 solution validation (does not replace MSVC build)."""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'
errors = []
projects = sorted(ROOT.rglob('*.vcxproj'))
project_guids = {}
checked_sources = 0
checked_includes = 0
checked_references = 0
include_re = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)

for file in projects:
    tree = ET.parse(file)
    doc = tree.getroot()
    guid = doc.findtext('.//' + NS + 'ProjectGuid')
    if not guid:
        errors.append(f'{file}: no ProjectGuid')
    else:
        project_guids[guid.upper()] = file

for project in projects:
    doc = ET.parse(project).getroot()
    include_by_config = {}
    for config in ('Debug', 'Release'):
        groups = [x for x in doc.findall(NS + 'ItemDefinitionGroup')
                  if f"'{config}|x64'" in x.attrib.get('Condition', '')]
        if len(groups) != 1:
            errors.append(f'{project}: missing or duplicate {config}|x64 config')
            continue
        cl = groups[0].find(NS + 'ClCompile')
        if cl is None:
            errors.append(f'{project}: no ClCompile property group ({config})')
            continue
        inc = cl.findtext(NS + 'AdditionalIncludeDirectories') or ''
        path_list = []
        for token in inc.split(';'):
            if token == '%(AdditionalIncludeDirectories)' or not token:
                continue
            if token.startswith('$(SolutionDir)'):
                path = ROOT / token[len('$(SolutionDir)'):].replace('\\', '/')
                if not path.is_dir():
                    errors.append(f'{project.name}/{config}: include directory missing: {path}')
                path_list.append(path)
            else:
                errors.append(f'{project.name}/{config}: unexpected include macro: {token}')
        include_by_config[config] = path_list
        if cl.findtext(NS + 'LanguageStandard') != 'stdcpp20':
            errors.append(f'{project.name}/{config}: must use stdcpp20')

    for kind in ('ClCompile', 'ClInclude'):
        for node in doc.findall('.//' + NS + kind):
            if not node.attrib.get('Include'):
                continue
            src = (project.parent / node.attrib['Include'].replace('\\', '/')).resolve()
            if not src.is_file():
                errors.append(f'{project.name}: {kind} not found: {src}')
                continue
            if kind == 'ClCompile':
                checked_sources += 1
                for match in include_re.finditer(src.read_text(encoding='utf-8-sig')):
                    header = match.group(1)
                    for config, include_dirs in include_by_config.items():
                        candidates = [src.parent / header] + [p / header for p in include_dirs]
                        if not any(c.is_file() for c in candidates):
                            errors.append(f'{project.name}/{config}: {src.name} cannot resolve "{header}"')
                        else:
                            checked_includes += 1

    for node in doc.findall('.//' + NS + 'ProjectReference'):
        ref = (project.parent / node.attrib.get('Include', '').replace('\\', '/')).resolve()
        checked_references += 1
        if not ref.is_file():
            errors.append(f'{project.name}: missing project reference {ref}')
            continue
        actual_guid = ET.parse(ref).getroot().findtext('.//' + NS + 'ProjectGuid')
        ref_guid = node.findtext(NS + 'Project')
        if not actual_guid or not ref_guid or actual_guid.upper() != ref_guid.upper():
            errors.append(f'{project.name}: wrong GUID for reference {ref.name}')

solution = (ROOT / 'HypervisorLab.sln').read_text(encoding='utf-8-sig')
for guid, project in project_guids.items():
    if guid not in solution.upper():
        errors.append(f'{project.name}: project absent from solution')

print(f'VS2022 validation: {len(projects)} projects, {checked_sources} translation units, '
      f'{checked_includes} local include resolutions (Debug+Release), '
      f'{checked_references} project dependencies')
if errors:
    for msg in errors:
        print('ERROR:', msg)
    sys.exit(1)
print('PASS: project sources, headers, configurations, include directories and reference GUIDs')
