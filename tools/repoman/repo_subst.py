# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import logging
import os
from argparse import ArgumentParser
from string import Template
from typing import Any, Callable, Dict, List, Optional

import omni.repo.man

logger = logging.getLogger(__name__)


def setup_repo_tool(parser: ArgumentParser, config: Dict[str, Any]) -> Optional[Callable]:
    parser.description = """
        Tool to replace repo tokens in text.
    """
    parser.add_argument(
        "-f",
        "--files",
        dest="files",
        help="A list of source template and destination file.",
    )

    tool_config = config.get("repo_subst", {})
    if not tool_config.get("enabled", True):
        return None

    def run_repo_tool(options: Any, config: Dict[str, Any]):
        tokens = omni.repo.man.get_tokens()

        files: List[str] = options.files or config.get("repo_subst", {}).get("files", [])
        for source_file, destination_file in files:
            destination_dir = os.path.dirname(destination_file)
            if destination_dir:
                os.makedirs(destination_dir, exist_ok=True)
            logger.info(f"Substitution from {source_file} to {destination_file}")

            with open(source_file, "r") as source_ptr, open(destination_file, "w") as destination_ptr:
                content: str = source_ptr.read()
                substitution: str = Template(content).safe_substitute(tokens)
                destination_ptr.write(substitution)

    return run_repo_tool
