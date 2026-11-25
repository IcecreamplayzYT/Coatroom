"""Coatroom MCP stdio bridge server (zero-dependency Python 3).

Exposes Coatroom's 3D/2D automotive livery painting, vector Bezier pen, text,
decal, AO baking, PNG export, and F12 beauty-render tools over MCP JSON-RPC 2.0
stdio by spawning ``build/Coatroom.exe --mcp-stdio`` and forwarding messages.

Usage:
    python mcp/coatroom_mcp.py
    python mcp/coatroom_mcp.py --exe path/to/Coatroom.exe

Example MCP client config:
    {
      "mcpServers": {
        "coatroom": {
          "command": "python",
          "args": ["C:/path/to/LiveryPaintStudio/mcp/coatroom_mcp.py"]
        }
      }
    }
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
from pathlib import Path


def resolve_coatroom_exe(explicit_exe: str | None = None) -> Path:
    """Locate Coatroom.exe from CLI flag, COATROOM_EXE env var, or repo build/."""
    candidates: list[Path] = []
    if explicit_exe:
        candidates.append(Path(explicit_exe))
    env_exe = os.environ.get("COATROOM_EXE")
    if env_exe:
        candidates.append(Path(env_exe))

    repo_root = Path(__file__).resolve().parent.parent
    candidates.extend([
        repo_root / "build" / "Coatroom.exe",
        repo_root / "build" / "Release" / "Coatroom.exe",
        repo_root / "Coatroom.exe",
    ])
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError(
        "Coatroom.exe not found. Build with build.ps1 or pass --exe / COATROOM_EXE."
    )


def _pump_stream(src, dst, *, json_lines_only: bool = False) -> None:
    """Forward lines from src binary stream to dst binary stream with immediate flush."""
    try:
        while True:
            line = src.readline()
            if not line:
                break
            if json_lines_only:
                stripped = line.strip()
                if not stripped:
                    continue
                # Validate JSON-RPC framing so stray non-JSON text never pollutes stdout.
                try:
                    json.loads(stripped.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    sys.stderr.buffer.write(b"[coatroom_mcp] dropped non-JSON line: " + line)
                    sys.stderr.buffer.flush()
                    continue
                dst.write(stripped + b"\n")
            else:
                dst.write(line)
            dst.flush()
    except BrokenPipeError:
        pass


def run_bridge(exe_path: Path) -> int:
    """Spawn Coatroom.exe --mcp-stdio and bridge stdin/stdout JSON-RPC 2.0."""
    proc = subprocess.Popen(
        [str(exe_path), "--mcp-stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    assert proc.stdin is not None and proc.stdout is not None and proc.stderr is not None

    t_out = threading.Thread(
        target=_pump_stream,
        args=(proc.stdout, sys.stdout.buffer),
        kwargs={"json_lines_only": True},
        daemon=True,
    )
    t_err = threading.Thread(
        target=_pump_stream,
        args=(proc.stderr, sys.stderr.buffer),
        daemon=True,
    )
    t_out.start()
    t_err.start()

    try:
        while True:
            line = sys.stdin.buffer.readline()
            if not line:
                break
            stripped = line.strip()
            if not stripped:
                continue
            # Handle optional Content-Length framing if an older MCP client sends headers.
            if stripped.lower().startswith(b"content-length:"):
                try:
                    length = int(stripped.split(b":", 1)[1].strip())
                except ValueError:
                    continue
                while True:
                    hdr = sys.stdin.buffer.readline()
                    if not hdr or hdr.strip() == b"":
                        break
                payload = sys.stdin.buffer.read(length).strip()
                if not payload:
                    continue
                stripped = payload
            proc.stdin.write(stripped + b"\n")
            proc.stdin.flush()
    except BrokenPipeError:
        pass
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass

    rc = proc.wait()
    t_out.join(timeout=2.0)
    t_err.join(timeout=2.0)
    return rc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Coatroom MCP stdio server bridge")
    parser.add_argument("--exe", help="Path to Coatroom.exe", default=None)
    args = parser.parse_args(argv)

    try:
        exe_path = resolve_coatroom_exe(args.exe)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    return run_bridge(exe_path)


if __name__ == "__main__":
    raise SystemExit(main())
