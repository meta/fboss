#!/usr/bin/env python3
"""
Tool to detect missing dependencies between Buck and cmake builds.

This tool:
1. Parses all BUCK files to extract targets and their dependencies
2. Parses all cmake files to extract targets and their dependencies
3. Correlates cmake targets with Buck targets by matching source files
4. Reports missing dependencies in cmake files
"""

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class BuckTarget:
    """Represents a Buck target (cpp_library, thrift_library, etc.)"""

    name: str
    buck_path: str  # e.g., "//fboss/fsdb/common"
    full_name: str  # e.g., "//fboss/fsdb/common:utils"
    target_type: str  # "cpp_library", "thrift_library", etc.
    srcs: list[str] = field(default_factory=list)
    headers: list[str] = field(default_factory=list)
    deps: list[str] = field(default_factory=list)
    exported_deps: list[str] = field(default_factory=list)
    thrift_srcs: list[str] = field(default_factory=list)
    file_path: str = ""  # Actual BUCK file path


@dataclass
class CmakeTarget:
    """Represents a cmake target (add_library, thrift_add_library, etc.)"""

    name: str
    target_type: str  # "library", "thrift", "executable"
    srcs: list[str] = field(default_factory=list)
    deps: list[str] = field(default_factory=list)
    thrift_deps: list[str] = field(default_factory=list)
    file_path: str = ""  # The cmake file where this target is defined


@dataclass
class TargetMatch:
    """Represents a match between a cmake target and a Buck target"""

    cmake_target: CmakeTarget
    buck_target: BuckTarget
    matching_srcs: list[str] = field(default_factory=list)
    match_score: float = 0.0  # 0-1, 1 being perfect match


@dataclass
class MissingDependency:
    """Represents a missing dependency in cmake"""

    cmake_target: CmakeTarget
    buck_target: BuckTarget
    buck_dep: str  # The Buck dependency that's missing
    suggested_cmake_dep: Optional[str] = None  # Suggested cmake target to add


@dataclass
class FileInconsistency:
    """Represents a source file present on one side but missing from the other"""

    # "missing_from_cmake" or "missing_from_buck"
    direction: str
    file_path: str  # repo-root-relative path
    cmake_target: CmakeTarget
    buck_target: BuckTarget


@dataclass
class SortViolation:
    """Represents an unsorted srcs/headers list in a build target"""

    build_system: str  # "buck" or "cmake"
    file_path: str  # path to the BUCK or cmake file
    target_name: str  # full_name for Buck targets, name for cmake targets
    list_name: str  # "srcs" or "headers"
    items: list[str]  # the unsorted list as extracted from the file


class BuckParser:
    """Parser for BUCK files"""

    def __init__(self, repo_root: str):
        self.repo_root = Path(repo_root)
        self.targets: dict[str, BuckTarget] = {}  # full_name -> target

    def parse_all(self) -> dict[str, BuckTarget]:
        """Parse all BUCK files in the repository"""
        for buck_file in self.repo_root.rglob("BUCK"):
            self._parse_file(buck_file)
        return self.targets

    def _parse_file(self, file_path: Path):
        """Parse a single BUCK file"""
        try:
            with open(file_path) as f:
                content = f.read()
        except Exception as e:
            print(f"Warning: Could not read {file_path}: {e}")
            return

        # Get the buck path from file location
        rel_path = file_path.parent.relative_to(self.repo_root)
        buck_path = "//" + str(rel_path).replace("\\", "/")

        # Parse cpp_library and executable-type targets (cpp_binary,
        # cpp_unittest, cpp_benchmark). They all share the same srcs/deps shape.
        for rule_type in ("cpp_library", "cpp_binary", "cpp_unittest", "cpp_benchmark"):
            self._parse_cpp_rule(rule_type, content, buck_path, str(file_path))

        # Parse thrift_library targets
        self._parse_thrift_library(content, buck_path, str(file_path))

    def _extract_string_list(self, content: str, key: str) -> list[str]:
        """Extract a list of strings from a Buck stanza"""
        # Match patterns like: key = ["item1", "item2"] or key = [\n"item1",\n"item2"\n]
        # Use word boundary or start-of-line/whitespace to ensure 'deps' doesn't match
        # inside 'cpp2_deps' or 'exported_deps'
        pattern = rf"(?:^|[\s,\n])({key})\s*=\s*\[(.*?)\]"
        match = re.search(pattern, content, re.DOTALL | re.MULTILINE)
        if not match:
            return []

        list_content = match.group(2)
        return re.findall(r'"([^"]*)"', list_content)

    def _extract_deps_list(self, content: str, key: str) -> list[str]:
        """Extract a list of dependencies from a Buck stanza"""
        return self._extract_string_list(content, key)

    def _parse_cpp_rule(
        self, rule_type: str, content: str, buck_path: str, file_path: str
    ):
        """Parse cpp_library / cpp_binary / cpp_unittest / cpp_benchmark declarations"""
        # Match the rule keyword; allow comments between '(' and 'name =' by
        # extracting the full block first, then reading name from inside it.
        pattern = rf"{rule_type}\s*\("
        for match in re.finditer(pattern, content):
            start = match.start()

            block_content = self._extract_block(content, start)
            if not block_content:
                continue

            name_match = re.search(r'name\s*=\s*"([^"]+)"', block_content)
            if not name_match:
                continue
            name = name_match.group(1)

            target = BuckTarget(
                name=name,
                buck_path=buck_path,
                full_name=f"{buck_path}:{name}",
                target_type=rule_type,
                srcs=self._extract_string_list(block_content, "srcs"),
                headers=self._extract_string_list(block_content, "headers"),
                deps=self._extract_deps_list(block_content, "deps"),
                exported_deps=self._extract_deps_list(block_content, "exported_deps"),
                file_path=file_path,
            )
            self.targets[target.full_name] = target

    def _parse_thrift_library(self, content: str, buck_path: str, file_path: str):
        """Parse thrift_library declarations"""
        pattern = r"thrift_library\s*\("
        for match in re.finditer(pattern, content):
            start = match.start()

            block_content = self._extract_block(content, start)
            if not block_content:
                continue

            name_match = re.search(r'name\s*=\s*"([^"]+)"', block_content)
            if not name_match:
                continue
            name = name_match.group(1)

            # thrift_srcs is a dict like {"file.thrift": [...]}
            thrift_srcs = self._extract_thrift_srcs(block_content)

            target = BuckTarget(
                name=name,
                buck_path=buck_path,
                full_name=f"{buck_path}:{name}",
                target_type="thrift_library",
                deps=self._extract_deps_list(block_content, "deps"),
                thrift_srcs=thrift_srcs,
                file_path=file_path,
            )
            self.targets[target.full_name] = target

    def _extract_thrift_srcs(self, content: str) -> list[str]:
        """Extract thrift source files from thrift_srcs dict"""
        pattern = r"thrift_srcs\s*=\s*\{([^}]*)\}"
        match = re.search(pattern, content, re.DOTALL)
        if not match:
            return []
        # Extract filenames from the dict keys
        dict_content = match.group(1)
        return re.findall(r'"([^"]+\.thrift)"', dict_content)

    def _extract_block(self, content: str, start: int) -> Optional[str]:
        """Extract a complete block starting from a function call"""
        # Find the opening parenthesis
        paren_start = content.find("(", start)
        if paren_start == -1:
            return None

        depth = 1
        pos = paren_start + 1
        while pos < len(content) and depth > 0:
            if content[pos] == "(":
                depth += 1
            elif content[pos] == ")":
                depth -= 1
            pos += 1

        if depth == 0:
            return content[paren_start:pos]
        return None


class CmakeParser:
    """Parser for cmake files"""

    def __init__(self, repo_root: str):
        self.repo_root = Path(repo_root)
        self.targets: dict[str, CmakeTarget] = {}  # name -> target
        self.variables: dict[str, list[str]] = {}  # cmake variable -> values
        self.duplicate_errors: list[str] = []  # List of duplicate target errors

    def _add_target(self, name: str, target: CmakeTarget):
        """Add a target, reporting error if it already exists"""
        if name in self.targets:
            existing = self.targets[name]
            error = (
                f"ERROR: Duplicate cmake target '{name}' defined in:\n"
                f"  First:  {existing.file_path}\n"
                f"  Second: {target.file_path}"
            )
            self.duplicate_errors.append(error)
            print(error)
        self.targets[name] = target

    def parse_all(self) -> dict[str, CmakeTarget]:
        """Parse all cmake files in cmake/ directory and CMakeLists.txt"""
        # Parse the main CMakeLists.txt first
        cmakelists = self.repo_root / "CMakeLists.txt"
        if cmakelists.exists():
            self._parse_file(cmakelists)

        # Then parse all cmake files in the cmake/ directory
        cmake_dir = self.repo_root / "cmake"
        for cmake_file in cmake_dir.rglob("*.cmake"):
            self._parse_file(cmake_file)
        return self.targets

    def _parse_file(self, file_path: Path):
        """Parse a single cmake file"""
        try:
            with open(file_path) as f:
                content = f.read()
        except Exception as e:
            print(f"Warning: Could not read {file_path}: {e}")
            return

        # Parse set() commands first to capture variables
        self._parse_set_commands(content)

        # Parse add_library
        self._parse_add_library(content, str(file_path))

        # Parse C++ Thrift libraries
        self._parse_thrift_cpp_libraries(content, str(file_path))

        # Parse add_executable
        self._parse_add_executable(content, str(file_path))

        # Parse target_link_libraries to get dependencies
        self._parse_target_link_libraries(content)

    def _parse_set_commands(self, content: str):
        """Parse set() commands to capture variable definitions"""
        # Match set(VAR_NAME value1 value2 ...) or set(VAR_NAME\n  value1\n  value2\n)
        pattern = r"set\s*\(\s*(\w+)\s+([^)]+)\)"
        for match in re.finditer(pattern, content, re.IGNORECASE):
            var_name = match.group(1)
            values_block = match.group(2)
            # Extract values (split by whitespace/newlines, filter empty/comments)
            values = []
            for line in values_block.split("\n"):
                for raw_token in line.split():
                    stripped = raw_token.strip()
                    if stripped and not stripped.startswith("#"):
                        values.append(stripped)
            if values:
                self.variables[var_name] = values

    def _expand_variables(self, deps: list[str]) -> list[str]:
        """Expand cmake ${VAR} references in dependency list"""
        expanded = []
        for raw_dep in deps:
            dep = raw_dep.strip()
            if not dep:
                continue
            # Check if this is a variable reference like ${var_name}
            var_match = re.match(r"\$\{(\w+)\}", dep)
            if var_match:
                var_name = var_match.group(1)
                if var_name in self.variables:
                    expanded.extend(self.variables[var_name])
                # else: unknown variable, skip it
            else:
                expanded.append(dep)
        return expanded

    def _parse_add_library(self, content: str, file_path: str):
        """Parse add_library calls"""
        # Match add_library(name\n  file1.cpp\n  file2.cpp\n)
        pattern = r"\badd_library\s*\(\s*(\w+)\s*\n([^)]*)\)"
        for match in re.finditer(pattern, content):
            name = match.group(1)
            files_block = match.group(2)
            # Extract file paths
            srcs = self._extract_source_files(files_block)

            target = CmakeTarget(
                name=name, target_type="library", srcs=srcs, file_path=file_path
            )
            self._add_target(name, target)

    def _parse_add_executable(self, content: str, file_path: str):
        """Parse add_executable calls"""
        pattern = r"add_executable\s*\(\s*(\w+)\s*\n([^)]*)\)"
        for match in re.finditer(pattern, content):
            name = match.group(1)
            files_block = match.group(2)
            srcs = self._extract_source_files(files_block)

            target = CmakeTarget(
                name=name, target_type="executable", srcs=srcs, file_path=file_path
            )
            self._add_target(name, target)

    def _parse_thrift_cpp_libraries(self, content: str, file_path: str):
        """Parse the calls that generate C++ Thrift libraries"""
        # Match the target name and thrift file
        pattern = (
            r"\b(thrift_add_library|thrift_generate)"
            r"\s*\(\s*([\w-]+)\s*\n\s*([^\s]+\.thrift)"
        )
        for match in re.finditer(pattern, content):
            command, name, thrift_file = match.groups()

            # Find the full block to extract DEPENDS
            block_start = match.start()
            block = self._extract_cmake_block(content, block_start)
            # thrift_generate() also generates code for other languages, and
            # adds it to a target created with add_library(), whose
            # dependencies target_link_libraries() lists.
            is_generate = command == "thrift_generate"
            if is_generate and not re.search(r"\bLANGUAGE\s+cpp2\b", block or ""):
                continue
            deps = []
            if block and not is_generate:
                deps = self._extract_thrift_depends(block)

            target = CmakeTarget(
                name=name,
                target_type="thrift",
                srcs=[thrift_file],
                thrift_deps=deps,
                file_path=file_path,
            )
            self._add_target(name, target)

    def _extract_thrift_depends(self, block: str) -> list[str]:
        """Extract DEPENDS from a thrift cmake block"""
        # Look for DEPENDS section
        pattern = r"DEPENDS\s*\n((?:\s+\w+\s*\n)*)"
        match = re.search(pattern, block)
        if not match:
            return []
        deps_block = match.group(1)
        return [d.strip() for d in deps_block.split("\n") if d.strip()]

    def _extract_cmake_block(self, content: str, start: int) -> Optional[str]:
        """Extract a cmake block from ( to )"""
        paren_start = content.find("(", start)
        if paren_start == -1:
            return None

        depth = 1
        pos = paren_start + 1
        while pos < len(content) and depth > 0:
            if content[pos] == "(":
                depth += 1
            elif content[pos] == ")":
                depth -= 1
            pos += 1

        if depth == 0:
            return content[paren_start:pos]
        return None

    def _parse_target_link_libraries(self, content: str):
        """Parse target_link_libraries to associate dependencies with targets"""
        # Match both multi-line and single-line formats
        pattern = r"target_link_libraries\s*\(\s*(\w+)\s+([^)]*)\)"
        for match in re.finditer(pattern, content):
            target_name = match.group(1)
            deps_block = match.group(2)

            if target_name in self.targets:
                # Parse deps from the block (split by whitespace/newlines)
                raw_deps = []
                for line in deps_block.split("\n"):
                    for raw_token in line.split():
                        stripped = raw_token.strip()
                        if stripped and not stripped.startswith("#"):
                            raw_deps.append(stripped)
                # Expand any ${VAR} references
                deps = self._expand_variables(raw_deps)
                # Extend existing deps (don't overwrite - there may be multiple
                # target_link_libraries calls for the same target)
                self.targets[target_name].deps.extend(deps)

    def _extract_source_files(self, block: str) -> list[str]:
        """Extract source file paths from a cmake block"""
        files = []
        for raw_line in block.split("\n"):
            stripped = raw_line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if stripped in ("PUBLIC", "PRIVATE", "INTERFACE"):
                continue
            files.append(stripped)
        return files


class DependencyAnalyzer:
    """Analyzes dependencies between Buck and cmake targets"""

    def __init__(self, repo_root: str):
        self.repo_root = Path(repo_root)
        self.buck_parser = BuckParser(repo_root)
        self.cmake_parser = CmakeParser(repo_root)
        self.buck_targets: dict[str, BuckTarget] = {}
        self.cmake_targets: dict[str, CmakeTarget] = {}
        self.matches: list[TargetMatch] = []
        # Maps to help correlate Buck deps to cmake deps
        self.buck_to_cmake: dict[str, str] = {}  # buck full_name -> cmake name

    def _parse(self):
        """Parse all BUCK and cmake files (shared by all analysis modes, idempotent)."""
        if self.buck_targets:
            return
        print("Parsing BUCK files...")
        self.buck_targets = self.buck_parser.parse_all()
        print(f"  Found {len(self.buck_targets)} Buck targets")

        print("Parsing cmake files...")
        self.cmake_targets = self.cmake_parser.parse_all()
        print(f"  Found {len(self.cmake_targets)} cmake targets")

    def analyze(self) -> list[MissingDependency]:
        """Parse and check dependency consistency between Buck and cmake."""
        self._parse()

        print("Matching targets by source files...")
        self.matches = self._match_targets()
        print(f"  Found {len(self.matches)} matches")

        print("Building Buck-to-cmake dependency mapping...")
        self._build_dep_mapping()

        print("Analyzing missing dependencies...")
        return self._find_missing_deps()

    def check_files(
        self, from_commit: Optional[str] = None, to_commit: Optional[str] = None
    ) -> list["FileInconsistency"]:
        """Parse and check file-list consistency between cmake and BUCK.

        Without a commit range: reports files listed in cmake targets that are
        absent from any BUCK target (cmake → BUCK direction only, because a
        file can legitimately exist on disk yet be intentionally excluded from
        cmake).

        With a commit range: reports files changed in that range that are
        present in one build system but absent from the other. The assumption
        is that if you changed a file, it should be in both systems.
        """
        self._parse()

        if from_commit is not None:
            changed = self._get_changed_files(from_commit, to_commit or "HEAD")
            print(f"  Checking {len(changed)} files changed in commit range")
            return self._find_file_inconsistencies_changed(changed)
        return self._find_file_inconsistencies_cmake_only()

    def check_sorted(
        self, from_commit: Optional[str] = None, to_commit: Optional[str] = None
    ) -> list[SortViolation]:
        """Parse and check that srcs/headers file lists are sorted alphabetically.

        Checks Buck targets (srcs and headers) and cmake targets (srcs).
        Sort order is case-insensitive (str.casefold) to match common conventions.

        Without a commit range: checks all targets.
        With a commit range: limits the check to targets defined in BUCK/*.cmake
        files touched in that range, so the report only covers newly-edited lists
        and is not flooded by pre-existing violations elsewhere in the repo.
        """
        self._parse()

        # If a commit range is given, build a set of absolute paths to the
        # build files changed in that range and skip targets defined elsewhere.
        changed_build_files: Optional[set[str]] = None
        if from_commit is not None:
            all_changed = self._get_changed_files(from_commit, to_commit or "HEAD")
            changed_build_files = {
                str(self.repo_root / f)
                for f in all_changed
                if (
                    f.endswith("/BUCK")
                    or f == "BUCK"
                    or f.endswith(".cmake")
                    or f == "CMakeLists.txt"
                )
            }
            print(
                f"  Checking sort order in {len(changed_build_files)} "
                f"changed build file(s)"
            )

        violations: list[SortViolation] = []

        for bt in self.buck_targets.values():
            if (
                changed_build_files is not None
                and bt.file_path not in changed_build_files
            ):
                continue
            for list_name, items in (("srcs", bt.srcs), ("headers", bt.headers)):
                if len(items) > 1 and items != sorted(items, key=str.casefold):
                    violations.append(
                        SortViolation(
                            build_system="buck",
                            file_path=bt.file_path,
                            target_name=bt.full_name,
                            list_name=list_name,
                            items=items,
                        )
                    )

        for ct in self.cmake_targets.values():
            if (
                changed_build_files is not None
                and ct.file_path not in changed_build_files
            ):
                continue
            if len(ct.srcs) > 1 and ct.srcs != sorted(ct.srcs, key=str.casefold):
                violations.append(
                    SortViolation(
                        build_system="cmake",
                        file_path=ct.file_path,
                        target_name=ct.name,
                        list_name="srcs",
                        items=ct.srcs,
                    )
                )

        return violations

    def _normalize_path(self, path: str, buck_path: str = "") -> str:
        """Normalize a source file path to a canonical form for comparison"""
        # Remove leading fboss/ if present since Buck files use relative paths
        path = path.strip()

        # For cmake paths like 'fboss/fsdb/common/Utils.cpp'
        if path.startswith("fboss/"):
            return path

        # For Buck paths which are relative to their directory
        if buck_path:
            # buck_path is like "//fboss/fsdb/common"
            dir_path = buck_path.lstrip("/")
            return f"{dir_path}/{path}"

        return path

    def _match_targets(self) -> list[TargetMatch]:  # noqa: PLR0912
        """Match cmake targets to Buck targets based on source files"""
        matches = []

        for cmake_target in self.cmake_targets.values():
            if not cmake_target.srcs:
                continue

            # Normalize cmake source files
            cmake_srcs = set()
            for src in cmake_target.srcs:
                # Only consider .cpp, .h, .thrift files
                if src.endswith((".cpp", ".h", ".thrift")):
                    cmake_srcs.add(src)

            if not cmake_srcs:
                continue

            best_match: Optional[BuckTarget] = None
            best_score = 0.0
            best_matching_srcs: list[str] = []
            same_name_match: Optional[BuckTarget] = None
            same_name_matching_srcs: list[str] = []

            for buck_target in self.buck_targets.values():
                if buck_target.target_type not in ("cpp_library", "thrift_library"):
                    continue
                # Normalize Buck source files
                buck_srcs = set()
                for src in buck_target.srcs:
                    normalized = self._normalize_path(src, buck_target.buck_path)
                    buck_srcs.add(normalized)
                for src in buck_target.headers:
                    normalized = self._normalize_path(src, buck_target.buck_path)
                    buck_srcs.add(normalized)
                for src in buck_target.thrift_srcs:
                    # thrift files need directory context
                    normalized = self._normalize_path(src, buck_target.buck_path)
                    buck_srcs.add(normalized)

                if not buck_srcs:
                    continue

                # Find intersection
                matching = cmake_srcs.intersection(buck_srcs)
                if not matching:
                    continue

                # Score based on Jaccard similarity
                union = cmake_srcs.union(buck_srcs)
                score = len(matching) / len(union) if union else 0

                # If names match exactly, remember this as a strong candidate
                if buck_target.name == cmake_target.name:
                    same_name_match = buck_target
                    same_name_matching_srcs = list(matching)

                if score > best_score:
                    best_score = score
                    best_match = buck_target
                    best_matching_srcs = list(matching)

            # Prefer same-name match over highest-score match if both exist
            if same_name_match:
                best_match = same_name_match
                best_matching_srcs = same_name_matching_srcs
                # Recalculate score for the same-name match
                buck_srcs = set()
                for src in same_name_match.srcs:
                    buck_srcs.add(self._normalize_path(src, same_name_match.buck_path))
                for src in same_name_match.headers:
                    buck_srcs.add(self._normalize_path(src, same_name_match.buck_path))
                for src in same_name_match.thrift_srcs:
                    buck_srcs.add(self._normalize_path(src, same_name_match.buck_path))
                union = cmake_srcs.union(buck_srcs)
                best_score = (
                    len(set(same_name_matching_srcs)) / len(union) if union else 0
                )

            if best_match and best_score > 0:
                matches.append(
                    TargetMatch(
                        cmake_target=cmake_target,
                        buck_target=best_match,
                        matching_srcs=best_matching_srcs,
                        match_score=best_score,
                    )
                )

        return matches

    def _build_dep_mapping(self):
        """Build a mapping from Buck target names to cmake target names.

        When multiple cmake targets match the same Buck target, prefer
        the cmake target with the same name as the Buck target.

        For thrift libraries, also map sub-targets like -cpp2-types, -cpp2-services
        to the corresponding cmake thrift target.
        """
        # Track which Buck targets have same-name cmake matches
        same_name_matches: dict[str, str] = {}  # buck_full_name -> cmake_name

        for match in self.matches:
            if match.buck_target.name == match.cmake_target.name:
                same_name_matches[match.buck_target.full_name] = match.cmake_target.name

        for match in self.matches:
            buck_full = match.buck_target.full_name
            # If there's a same-name match for this Buck target, use it
            cmake_name = same_name_matches.get(buck_full, match.cmake_target.name)

            self.buck_to_cmake[buck_full] = cmake_name
            # Also map short form like ":name" within same BUCK file
            short_name = f":{match.buck_target.name}"
            self.buck_to_cmake[short_name] = cmake_name

            # For thrift libraries, also map sub-targets like -cpp2-types
            # Buck thrift_library generates sub-targets like:
            #   //path:name-cpp2-types, //path:name-cpp2-services, etc.
            # These should all map to the same cmake thrift target
            if match.buck_target.target_type == "thrift_library":
                thrift_suffixes = [
                    "-cpp2-types",
                    "-cpp2-services",
                    "-cpp2",
                    "-py",
                    "-py3",
                    "-rust",
                    "-go",
                ]
                buck_path = match.buck_target.buck_path
                buck_name = match.buck_target.name
                for suffix in thrift_suffixes:
                    sub_target_full = f"{buck_path}:{buck_name}{suffix}"
                    sub_target_short = f":{buck_name}{suffix}"
                    self.buck_to_cmake[sub_target_full] = cmake_name
                    self.buck_to_cmake[sub_target_short] = cmake_name

    def _resolve_buck_dep(self, dep: str, buck_path: str) -> str:
        """Resolve a Buck dependency to its full form"""
        if dep.startswith(":"):
            # Local reference within same BUCK file
            return f"{buck_path}{dep}"
        if dep.startswith("//"):
            return dep
        return dep

    def _get_transitive_cmake_deps(
        self, target_name: str, visited: Optional[set[str]] = None
    ) -> set[str]:
        """Get all transitive dependencies of a cmake target"""
        if visited is None:
            visited = set()
        if target_name in visited:
            return set()
        visited.add(target_name)

        target = self.cmake_targets.get(target_name)
        if not target:
            return set()

        all_deps = set(target.deps) | set(target.thrift_deps)
        for dep in list(all_deps):
            # Pass visited by reference to avoid re-exploring same nodes
            all_deps |= self._get_transitive_cmake_deps(dep, visited)
        return all_deps

    def _is_shim_path(self, path: str) -> bool:
        """Return True for paths that are OSS/internal shims and should be ignored."""
        parts = path.split("/")
        return "oss" in parts or "facebook" in parts

    def _is_plain_cpp(self, src: str) -> bool:
        """Return True if src looks like a plain .cpp filename (not a ref or glob)."""
        return (
            src.endswith(".cpp")
            and not src.startswith(("//", ":", "${"))
            and "*" not in src
            and "?" not in src
        )

    def _build_global_cpp_corpora(self) -> tuple[set[str], set[str]]:
        """Return (all_buck_cpp, all_cmake_cpp): repo-root-relative .cpp paths
        known to each build system, excluding shims and non-existent files.

        Buck `headers` lists can contain .cpp files (header-only / template
        impl patterns), so we scan both srcs and headers when building the
        Buck corpus.
        """
        all_buck_cpp: set[str] = set()
        for bt in self.buck_targets.values():
            for src in bt.srcs + bt.headers:
                norm = self._normalize_path(src, bt.buck_path)
                if not norm.startswith("fboss/"):
                    continue
                if (
                    self._is_plain_cpp(norm)
                    and not self._is_shim_path(norm)
                    and (self.repo_root / norm).is_file()
                ):
                    all_buck_cpp.add(norm)

        all_cmake_cpp: set[str] = set()
        for ct in self.cmake_targets.values():
            for src in ct.srcs:
                if (
                    self._is_plain_cpp(src)
                    and not self._is_shim_path(src)
                    and (self.repo_root / src).is_file()
                ):
                    all_cmake_cpp.add(src)

        return all_buck_cpp, all_cmake_cpp

    def _get_changed_files(self, from_commit: str, to_commit: str) -> set[str]:
        """Return repo-root-relative paths of files changed in the commit range."""
        result = subprocess.run(
            ["git", "diff", "--name-only", from_commit, to_commit],
            check=False,
            capture_output=True,
            text=True,
            cwd=self.repo_root,
        )
        if result.returncode != 0:
            print(f"Warning: git diff failed: {result.stderr.strip()}")
            return set()
        return set(result.stdout.splitlines())

    def _find_file_inconsistencies_cmake_only(self) -> list[FileInconsistency]:
        """Check that .cpp files in cmake targets also appear in the corresponding
        BUCK target (cmake → BUCK direction only).

        Uses same-name 1:1 pairing as a confidence gate so macro-generated
        Buck targets (invisible to the parser) never produce false positives.
        A file is only flagged if it is absent from the *entire* Buck corpus,
        not just the paired target, to handle cases where cmake merges targets.
        """
        all_buck_cpp, _ = self._build_global_cpp_corpora()

        # Index Buck targets by name, keyed by normalized name (hyphens → underscores)
        # so cmake targets (which use underscores) match BUCK targets (which use hyphens).
        buck_by_norm_name: dict[str, list[BuckTarget]] = {}
        for bt in self.buck_targets.values():
            norm = bt.name.replace("-", "_")
            buck_by_norm_name.setdefault(norm, []).append(bt)

        inconsistencies: list[FileInconsistency] = []

        for cmake_name, cmake_target in self.cmake_targets.items():
            cands = buck_by_norm_name.get(cmake_name.replace("-", "_"))
            if not cands:
                continue

            c_cpp: set[str] = set()
            for src in cmake_target.srcs:
                if (
                    self._is_plain_cpp(src)
                    and not self._is_shim_path(src)
                    and (self.repo_root / src).is_file()
                ):
                    c_cpp.add(src)

            # Confidence gate: pick the Buck candidate with the most .cpp overlap.
            best_buck: Optional[BuckTarget] = None
            best_overlap = 0
            for bt in cands:
                b_cpp: set[str] = set()
                for src in bt.srcs:
                    norm = self._normalize_path(src, bt.buck_path)
                    if (
                        norm.startswith("fboss/")
                        and self._is_plain_cpp(norm)
                        and not self._is_shim_path(norm)
                        and (self.repo_root / norm).is_file()
                    ):
                        b_cpp.add(norm)
                overlap = len(c_cpp & b_cpp)
                if overlap > best_overlap:
                    best_overlap = overlap
                    best_buck = bt

            if best_buck is None or best_overlap == 0:
                continue

            for f in c_cpp:
                if f not in all_buck_cpp:
                    inconsistencies.append(
                        FileInconsistency(
                            direction="missing_from_buck",
                            file_path=f,
                            cmake_target=cmake_target,
                            buck_target=best_buck,
                        )
                    )

        return inconsistencies

    def _find_file_inconsistencies_changed(  # noqa: PLR0912
        self, changed_files: set[str]
    ) -> list[FileInconsistency]:
        """Check changed files bidirectionally.

        Any .cpp file touched in the commit range is expected to be present in
        both build systems.  We find it in cmake or BUCK, then verify the other
        side also has it.  This catches both "added to BUCK but forgot cmake"
        and "added to cmake but forgot BUCK".

        Shim paths (oss/, facebook/) and files not on disk are silently ignored.
        """
        all_buck_cpp, all_cmake_cpp = self._build_global_cpp_corpora()

        inconsistencies: list[FileInconsistency] = []

        # Build an index for reporting: file → (cmake_target, buck_target)
        cmake_file_to_target: dict[str, CmakeTarget] = {}
        for ct in self.cmake_targets.values():
            for src in ct.srcs:
                if self._is_plain_cpp(src):
                    cmake_file_to_target[src] = ct

        buck_file_to_target: dict[str, BuckTarget] = {}
        for bt in self.buck_targets.values():
            for src in bt.srcs:
                norm = self._normalize_path(src, bt.buck_path)
                if self._is_plain_cpp(norm):
                    buck_file_to_target[norm] = bt

        for f in sorted(changed_files):
            if not self._is_plain_cpp(f):
                continue
            if self._is_shim_path(f):
                continue
            if not (self.repo_root / f).is_file():
                continue

            in_cmake = f in all_cmake_cpp
            in_buck = f in all_buck_cpp

            if in_cmake and not in_buck:
                cmake_target = cmake_file_to_target.get(f)
                if cmake_target:
                    # Use an empty sentinel BuckTarget for reporting
                    inconsistencies.append(
                        FileInconsistency(
                            direction="missing_from_buck",
                            file_path=f,
                            cmake_target=cmake_target,
                            buck_target=BuckTarget(
                                name="(none)",
                                buck_path="",
                                full_name="",
                                target_type="",
                            ),
                        )
                    )
            elif in_buck and not in_cmake:
                buck_target = buck_file_to_target.get(f)
                if buck_target:
                    inconsistencies.append(
                        FileInconsistency(
                            direction="missing_from_cmake",
                            file_path=f,
                            cmake_target=CmakeTarget(name="(none)", target_type=""),
                            buck_target=buck_target,
                        )
                    )

        return inconsistencies

    def _find_missing_deps(self) -> list[MissingDependency]:
        """Find dependencies that exist in Buck but are missing in cmake"""
        missing = []

        for match in self.matches:
            cmake_target = match.cmake_target
            buck_target = match.buck_target

            # Get all Buck dependencies
            all_buck_deps = set(buck_target.deps) | set(buck_target.exported_deps)

            # Get cmake dependencies (including transitive)
            cmake_deps = set(cmake_target.deps) | set(cmake_target.thrift_deps)
            transitive_cmake_deps = self._get_transitive_cmake_deps(cmake_target.name)

            for buck_dep in all_buck_deps:
                # Resolve the Buck dep
                full_buck_dep = self._resolve_buck_dep(buck_dep, buck_target.buck_path)

                # Check if we have a cmake equivalent
                cmake_equiv = self.buck_to_cmake.get(full_buck_dep)

                if cmake_equiv and (
                    cmake_equiv not in cmake_deps
                    and cmake_equiv not in transitive_cmake_deps
                ):
                    missing.append(
                        MissingDependency(
                            cmake_target=cmake_target,
                            buck_target=buck_target,
                            buck_dep=buck_dep,
                            suggested_cmake_dep=cmake_equiv,
                        )
                    )

        return missing


def print_dep_report(
    missing_deps: list[MissingDependency],
    matches: list[TargetMatch],
    verbose: bool = False,
):
    """Print the dependency-consistency report."""
    if verbose:
        print("\n" + "=" * 80)
        print("TARGET MATCHES (cmake -> Buck)")
        print("=" * 80)
        for match in sorted(matches, key=lambda m: m.cmake_target.name):
            print(f"\n{match.cmake_target.name} -> {match.buck_target.full_name}")
            print(f"  Match score: {match.match_score:.2%}")
            print(f"  cmake file: {match.cmake_target.file_path}")
            print(f"  BUCK file: {match.buck_target.file_path}")
            if match.matching_srcs:
                print(f"  Matching sources: {', '.join(match.matching_srcs[:3])}")
                if len(match.matching_srcs) > 3:
                    print(f"    ... and {len(match.matching_srcs) - 3} more")

    print("\n" + "=" * 80)
    print("MISSING DEPENDENCIES")
    print("=" * 80)

    if not missing_deps:
        print("\nNo missing dependencies found!")
        return

    # Group by cmake file
    by_file: dict[str, list[MissingDependency]] = {}
    for md in missing_deps:
        path = md.cmake_target.file_path
        if path not in by_file:
            by_file[path] = []
        by_file[path].append(md)

    for file_path, deps in sorted(by_file.items()):
        print(f"\n{file_path}:")
        for md in deps:
            print(f"  Target: {md.cmake_target.name}")
            print(f"    Missing: {md.suggested_cmake_dep}")
            print(f"    (from Buck dep: {md.buck_dep})")
            print(f"    Buck target: {md.buck_target.full_name}")


def print_file_inconsistencies_report(
    inconsistencies: list[FileInconsistency], mode_description: str
):
    """Print the file-list inconsistency report."""
    print("\n" + "=" * 80)
    print(f"FILE LIST INCONSISTENCIES ({mode_description})")
    print("=" * 80)

    if not inconsistencies:
        print("\nNo file list inconsistencies found!")
        return

    missing_from_cmake = [
        fi for fi in inconsistencies if fi.direction == "missing_from_cmake"
    ]
    missing_from_buck = [
        fi for fi in inconsistencies if fi.direction == "missing_from_buck"
    ]

    if missing_from_cmake:
        print("\nIn BUCK but missing from cmake:")
        for fi in sorted(missing_from_cmake, key=lambda x: x.file_path):
            print(f"  {fi.file_path}")
            print(f"    Buck target: {fi.buck_target.full_name}")
            print(f"    Buck file:   {fi.buck_target.file_path}")

    if missing_from_buck:
        print("\nIn cmake but missing from BUCK:")
        for fi in sorted(missing_from_buck, key=lambda x: x.file_path):
            print(f"  {fi.file_path}")
            print(f"    Target:    {fi.cmake_target.name}")
            print(f"    cmake file: {fi.cmake_target.file_path}")


def print_sort_violations_report(violations: list[SortViolation]):
    """Print the sort-order violation report."""
    print("\n" + "=" * 80)
    print("FILE LIST SORT ORDER VIOLATIONS")
    print("=" * 80)

    if not violations:
        print("\nAll file lists are sorted!")
        return

    by_file: dict[str, list[SortViolation]] = {}
    for v in violations:
        by_file.setdefault(v.file_path, []).append(v)

    for file_path, file_violations in sorted(by_file.items()):
        print(f"\n{file_path}:")
        for v in sorted(file_violations, key=lambda x: x.target_name):
            print(f"  [{v.build_system}] {v.target_name}  ({v.list_name})")
            expected = sorted(v.items, key=str.casefold)
            for i, (actual, exp) in enumerate(zip(v.items, expected)):
                if actual != exp:
                    print(
                        f"    First unsorted entry at position {i}: "
                        f"{actual!r} (expected {exp!r})"
                    )
                    break


def _handle_list_commands(args, analyzer) -> Optional[int]:
    """Print list-style debug output and return 0, or None if no list flag set."""
    if args.list_buck:
        print("\nBuck targets:")
        for name, target in sorted(analyzer.buck_targets.items()):
            print(f"  {name} ({target.target_type})")
            if target.srcs:
                print(f"    srcs: {target.srcs[:3]}...")
        return 0

    if args.list_cmake:
        print("\ncmake targets:")
        for name, target in sorted(analyzer.cmake_targets.items()):
            print(f"  {name} ({target.target_type})")
            print(f"    file: {target.file_path}")
            if target.srcs:
                print(f"    srcs: {target.srcs[:3]}...")
        return 0

    if getattr(args, "list_matches", False):
        print("\nTarget matches (cmake -> Buck):")
        for match in sorted(analyzer.matches, key=lambda m: m.cmake_target.name):
            print(f"  {match.cmake_target.name} -> {match.buck_target.full_name}")
            print(f"    score: {match.match_score:.2%}")
        return 0

    return None


def main():
    parser = argparse.ArgumentParser(
        description="Check consistency between Buck and cmake builds",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                              # Check dependency consistency (default)
  %(prog)s --check-files                # Check cmake file lists against BUCK
  %(prog)s --check-files \\
      --from-commit HEAD~               # Check files changed since HEAD~
  %(prog)s --check-files \\
      --from-commit HEAD~ --to-commit HEAD  # Explicit commit range
  %(prog)s --check-sorted               # Check sort order (all targets)
  %(prog)s --check-sorted --from-commit HEAD~  # Sort check limited to changed build files
  %(prog)s --check-files --check-sorted \\
      --from-commit HEAD~               # Both checks, scoped to the commit
  %(prog)s -r /path/to/fboss           # Analyze specific repository
  %(prog)s -v                           # Verbose output with match details
""",
    )
    parser.add_argument(
        "-r",
        "--repo-root",
        default=".",
        help="Repository root directory (default: current directory)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Show verbose output including all matches (dep check only)",
    )
    parser.add_argument(
        "--check-files",
        action="store_true",
        help=(
            "Check file-list consistency instead of dependency consistency. "
            "Without a commit range: reports cmake files absent from BUCK. "
            "With --from-commit: reports changed files absent from either side."
        ),
    )
    parser.add_argument(
        "--from-commit",
        metavar="REF",
        help=(
            "Start of commit range for --check-files / --check-sorted "
            "(exclusive, like git diff A B)"
        ),
    )
    parser.add_argument(
        "--to-commit",
        metavar="REF",
        default="HEAD",
        help="End of commit range for --check-files (inclusive, default: HEAD)",
    )
    parser.add_argument(
        "--check-sorted",
        action="store_true",
        help=(
            "Check that srcs/headers file lists are sorted alphabetically "
            "(case-insensitive). Checks Buck targets (srcs and headers) and "
            "cmake targets (srcs)."
        ),
    )
    parser.add_argument("--target", help="Only analyze a specific cmake target")
    parser.add_argument(
        "--list-matches",
        action="store_true",
        help="List all cmake-to-Buck target matches",
    )
    parser.add_argument(
        "--list-buck", action="store_true", help="List all Buck targets found"
    )
    parser.add_argument(
        "--list-cmake", action="store_true", help="List all cmake targets found"
    )

    args = parser.parse_args()

    if args.from_commit and not (args.check_files or args.check_sorted):
        print("Error: --from-commit requires --check-files or --check-sorted")
        return 1

    # Resolve the repo root
    repo_root = Path(args.repo_root).resolve()
    if not (repo_root / "cmake").is_dir():
        print(f"Error: {repo_root}/cmake directory not found")
        print("Make sure you're running from the fboss repository root")
        return 1

    analyzer = DependencyAnalyzer(str(repo_root))

    # --check-files and --check-sorted can be combined (they share one parse pass)
    if args.check_files or args.check_sorted:
        rc = _handle_list_commands(args, analyzer)
        if rc is not None:
            return rc
        exit_code = 0
        if args.check_files:
            inconsistencies = analyzer.check_files(
                from_commit=args.from_commit,
                to_commit=args.to_commit if args.from_commit else None,
            )
            mode = (
                f"changed files {args.from_commit}..{args.to_commit}"
                if args.from_commit
                else "cmake → BUCK"
            )
            print_file_inconsistencies_report(inconsistencies, mode)
            if inconsistencies:
                exit_code = 1
        if args.check_sorted:
            violations = analyzer.check_sorted(
                from_commit=args.from_commit or None,
                to_commit=args.to_commit if args.from_commit else None,
            )
            print_sort_violations_report(violations)
            if violations:
                exit_code = 1
        if analyzer.cmake_parser.duplicate_errors:
            exit_code = 1
        return exit_code

    # Default mode: dependency consistency check
    missing = analyzer.analyze()
    rc = _handle_list_commands(args, analyzer)
    if rc is not None:
        return rc

    if args.target:
        missing = [m for m in missing if m.cmake_target.name == args.target]

    print_dep_report(missing, analyzer.matches, args.verbose)
    has_duplicates = len(analyzer.cmake_parser.duplicate_errors) > 0
    return 1 if missing or has_duplicates else 0


if __name__ == "__main__":
    sys.exit(main())
