#!/usr/bin/env python3

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional


schema_version = 1
dependencies_directory_name = "dependencies"
default_config_name = "dependencies.json"
module_name_pattern = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def project_root_path() -> Path:
    return Path(__file__).resolve().parent.parent


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download fixed Git tags into the project dependencies directory."
    )
    parser.add_argument(
        "--config",
        default=default_config_name,
        help="Configuration path relative to the project root or an absolute path.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate Python, Git, and JSON configuration without downloading anything.",
    )
    return parser.parse_args()


def resolve_config_path(project_root: Path, config_value: str) -> Path:
    config_path = Path(config_value)
    if not config_path.is_absolute():
        config_path = project_root / config_path
    return config_path.resolve()


def find_git() -> str:
    # This script only detects Git. It never installs or configures software.
    git_path = shutil.which("git")
    if git_path is None:
        raise RuntimeError(
            "Git was not found. Install Git manually and ensure it is available in PATH."
        )
    return git_path


def run_git(
    git_path: str,
    arguments: List[str],
    working_directory: Optional[Path] = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
    completed_process = subprocess.run(
        [git_path] + arguments,
        cwd=str(working_directory) if working_directory is not None else None,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and completed_process.returncode != 0:
        error_message = completed_process.stderr.strip()
        if not error_message:
            error_message = completed_process.stdout.strip()
        raise RuntimeError(
            "Git command failed: git {}\n{}".format(
                " ".join(arguments),
                error_message,
            )
        )
    return completed_process


def check_git_version(git_path: str) -> str:
    completed_process = run_git(git_path, ["--version"])
    version_text = completed_process.stdout.strip()
    if not version_text:
        raise RuntimeError("Git was found but did not return a version string.")
    return version_text


def read_config(config_path: Path) -> Dict:
    if not config_path.is_file():
        raise RuntimeError("Configuration file was not found: {}".format(config_path))

    try:
        with config_path.open("r", encoding="utf-8") as config_file:
            config = json.load(config_file)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            "Invalid JSON in {} at line {}, column {}: {}".format(
                config_path,
                error.lineno,
                error.colno,
                error.msg,
            )
        ) from error

    if not isinstance(config, dict):
        raise RuntimeError("The configuration root must be a JSON object.")
    return config


def validate_module(module: Dict, index: int) -> Dict:
    if not isinstance(module, dict):
        raise RuntimeError("Module at index {} must be a JSON object.".format(index))

    allowed_keys = {"name", "git", "tag", "submodules"}
    unknown_keys = set(module.keys()) - allowed_keys
    if unknown_keys:
        raise RuntimeError(
            "Module at index {} contains unsupported fields: {}".format(
                index,
                ", ".join(sorted(unknown_keys)),
            )
        )

    for required_key in ("name", "git", "tag"):
        value = module.get(required_key)
        if not isinstance(value, str) or not value.strip():
            raise RuntimeError(
                "Module at index {} requires a non-empty '{}' string.".format(
                    index,
                    required_key,
                )
            )

    name = module["name"].strip()
    git_url = module["git"].strip()
    tag = module["tag"].strip()
    submodules = module.get("submodules", False)

    if module_name_pattern.fullmatch(name) is None:
        raise RuntimeError(
            "Invalid module name '{}'. Use letters, numbers, dots, dashes, or underscores.".format(
                name
            )
        )
    if not isinstance(submodules, bool):
        raise RuntimeError("Module '{}' field 'submodules' must be boolean.".format(name))

    return {
        "name": name,
        "git": git_url,
        "tag": tag,
        "submodules": submodules,
    }


def validate_config(config: Dict) -> List[Dict]:
    if config.get("schema_version") != schema_version:
        raise RuntimeError(
            "Unsupported schema_version. Expected {}.".format(schema_version)
        )

    modules_value = config.get("modules")
    if not isinstance(modules_value, list) or not modules_value:
        raise RuntimeError("The 'modules' field must be a non-empty JSON array.")

    modules = []
    module_names = set()
    for index, module_value in enumerate(modules_value):
        module = validate_module(module_value, index)
        if module["name"] in module_names:
            raise RuntimeError("Duplicate module name: {}".format(module["name"]))
        module_names.add(module["name"])
        modules.append(module)
    return modules


def validate_tag(git_path: str, module: Dict) -> None:
    tag_reference = "refs/tags/{}".format(module["tag"])
    completed_process = run_git(
        git_path,
        ["check-ref-format", tag_reference],
        check=False,
    )
    if completed_process.returncode != 0:
        raise RuntimeError(
            "Module '{}' contains an invalid Git tag: {}".format(
                module["name"],
                module["tag"],
            )
        )


def ensure_target_path(dependencies_root: Path, module_name: str) -> Path:
    target_path = (dependencies_root / module_name).resolve()
    try:
        target_path.relative_to(dependencies_root.resolve())
    except ValueError as error:
        raise RuntimeError(
            "Module target escapes the dependencies directory: {}".format(module_name)
        ) from error
    return target_path


def repository_value(
    git_path: str,
    repository_path: Path,
    arguments: List[str],
    required: bool = True,
) -> Optional[str]:
    completed_process = run_git(
        git_path,
        arguments,
        working_directory=repository_path,
        check=False,
    )
    if completed_process.returncode != 0:
        if required:
            raise RuntimeError(
                "Unable to inspect repository '{}': {}".format(
                    repository_path,
                    completed_process.stderr.strip(),
                )
            )
        return None
    return completed_process.stdout.strip()


def resolved_tag_commit(
    git_path: str,
    repository_path: Path,
    tag: str,
) -> Optional[str]:
    return repository_value(
        git_path,
        repository_path,
        ["rev-parse", "--verify", "refs/tags/{}^{{commit}}".format(tag)],
        required=False,
    )


def update_submodules(git_path: str, repository_path: Path) -> None:
    run_git(
        git_path,
        ["submodule", "update", "--init", "--recursive", "--depth", "1"],
        working_directory=repository_path,
    )


def verify_repository(
    git_path: str,
    repository_path: Path,
    module: Dict,
) -> str:
    origin_url = repository_value(
        git_path,
        repository_path,
        ["remote", "get-url", "origin"],
    )
    if origin_url != module["git"]:
        raise RuntimeError(
            "Module '{}' origin mismatch. Expected '{}', found '{}'.".format(
                module["name"],
                module["git"],
                origin_url,
            )
        )

    tag_commit = resolved_tag_commit(
        git_path,
        repository_path,
        module["tag"],
    )
    if tag_commit is None:
        raise RuntimeError(
            "Module '{}' did not provide tag '{}'.".format(
                module["name"],
                module["tag"],
            )
        )

    head_commit = repository_value(
        git_path,
        repository_path,
        ["rev-parse", "HEAD"],
    )
    if head_commit != tag_commit:
        raise RuntimeError(
            "Module '{}' HEAD does not match tag '{}'.".format(
                module["name"],
                module["tag"],
            )
        )
    return head_commit


def clone_module(
    git_path: str,
    dependencies_root: Path,
    target_path: Path,
    module: Dict,
) -> str:
    temporary_path = Path(
        tempfile.mkdtemp(
            prefix=".{}.download-".format(module["name"]),
            dir=str(dependencies_root),
        )
    )
    try:
        clone_arguments = [
            "clone",
            "--depth",
            "1",
            "--branch",
            module["tag"],
        ]
        if module["submodules"]:
            clone_arguments.extend(["--recurse-submodules", "--shallow-submodules"])
        clone_arguments.extend(["--", module["git"], str(temporary_path)])
        run_git(git_path, clone_arguments)

        head_commit = verify_repository(git_path, temporary_path, module)
        temporary_path.replace(target_path)
        return head_commit
    finally:
        if temporary_path.exists():
            # Only remove the unique temporary directory created above.
            shutil.rmtree(temporary_path)


def synchronize_existing_module(
    git_path: str,
    target_path: Path,
    module: Dict,
) -> str:
    if not (target_path / ".git").exists():
        raise RuntimeError(
            "Module target exists but is not a Git repository: {}".format(target_path)
        )

    origin_url = repository_value(
        git_path,
        target_path,
        ["remote", "get-url", "origin"],
    )
    if origin_url != module["git"]:
        raise RuntimeError(
            "Module '{}' origin mismatch. Expected '{}', found '{}'.".format(
                module["name"],
                module["git"],
                origin_url,
            )
        )

    worktree_status = repository_value(
        git_path,
        target_path,
        ["status", "--porcelain"],
    )
    if worktree_status:
        raise RuntimeError(
            "Module '{}' contains local changes; synchronization was stopped.".format(
                module["name"]
            )
        )

    head_commit = repository_value(git_path, target_path, ["rev-parse", "HEAD"])
    tag_commit = resolved_tag_commit(git_path, target_path, module["tag"])
    if tag_commit != head_commit:
        tag_reference = "refs/tags/{}".format(module["tag"])
        run_git(
            git_path,
            [
                "fetch",
                "--depth",
                "1",
                "--force",
                "origin",
                "{}:{}".format(tag_reference, tag_reference),
            ],
            working_directory=target_path,
        )
        run_git(
            git_path,
            ["checkout", "--detach", tag_reference],
            working_directory=target_path,
        )

    if module["submodules"]:
        update_submodules(git_path, target_path)
    return verify_repository(git_path, target_path, module)


def synchronize_module(
    git_path: str,
    dependencies_root: Path,
    module: Dict,
) -> str:
    target_path = ensure_target_path(dependencies_root, module["name"])
    if target_path.exists():
        return synchronize_existing_module(git_path, target_path, module)
    return clone_module(
        git_path,
        dependencies_root,
        target_path,
        module,
    )


def main() -> int:
    arguments = parse_arguments()
    project_root = project_root_path()
    config_path = resolve_config_path(project_root, arguments.config)

    try:
        git_path = find_git()
        git_version = check_git_version(git_path)
        config = read_config(config_path)
        modules = validate_config(config)
        for module in modules:
            validate_tag(git_path, module)

        print("Git: {}".format(git_version))
        print("Configuration: {}".format(config_path))

        if arguments.check:
            for module in modules:
                print(
                    "CHECKED: {} git={} tag={} submodules={}".format(
                        module["name"],
                        module["git"],
                        module["tag"],
                        module["submodules"],
                    )
                )
            print("Configuration and Git checks passed; no files were downloaded.")
            return 0

        dependencies_root = (project_root / dependencies_directory_name).resolve()
        dependencies_root.mkdir(parents=True, exist_ok=True)
        print("Destination: {}".format(dependencies_root))

        for module in modules:
            print("SYNC: {} tag={}".format(module["name"], module["tag"]))
            head_commit = synchronize_module(git_path, dependencies_root, module)
            print(
                "READY: {} tag={} commit={}".format(
                    module["name"],
                    module["tag"],
                    head_commit,
                )
            )
        return 0
    except (OSError, RuntimeError) as error:
        print("ERROR: {}".format(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
