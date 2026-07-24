from collections.abc import Sequence
from pathlib import Path
import shlex
import subprocess


def runCommand(
    command: Sequence[str],
    workingDirectory: Path,
    *,
    captureOutput: bool = False,
    check: bool = True,
    timeoutSeconds: float | None = None,
) -> subprocess.CompletedProcess[str]:
    normalizedCommand = [str(argument) for argument in command]
    print(f"+ {shlex.join(normalizedCommand)}", flush=True)
    return subprocess.run(
        normalizedCommand,
        cwd=workingDirectory,
        check=check,
        capture_output=captureOutput,
        text=True,
        timeout=timeoutSeconds,
    )
