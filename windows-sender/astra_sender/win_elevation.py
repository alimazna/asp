"""Windows privilege-level helpers for the ASTRA sender.

MetaTrader5's IPC handshake only succeeds when the sender and ``terminal64.exe``
run at the same integrity level. MT5 exposes no "is the terminal elevated" flag,
so the terminal's level is read from its process token. Every path degrades to
``None``/``False`` off Windows or when the query is denied, so callers report
"unknown" honestly instead of guessing.
"""

from __future__ import annotations

import os
from typing import Iterator, Optional, Tuple

_TERMINAL_EXE_NAMES = ("terminal64.exe", "terminal.exe")


def is_elevated() -> bool:
    """True when this process runs elevated (Windows only)."""
    if os.name != "nt":
        return False
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def _iter_processes() -> Iterator[Tuple[int, str]]:
    """Yield (pid, exe_name) for running processes (Windows only)."""
    if os.name != "nt":
        return
    import ctypes
    from ctypes import wintypes

    TH32CS_SNAPPROCESS = 0x00000002

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("cntUsage", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.c_void_p),
            ("th32ModuleID", wintypes.DWORD),
            ("cntThreads", wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", wintypes.DWORD),
            ("szExeFile", wintypes.WCHAR * 260),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel32.Process32FirstW.argtypes = [wintypes.HANDLE,
                                         ctypes.POINTER(PROCESSENTRY32W)]
    kernel32.Process32NextW.argtypes = [wintypes.HANDLE,
                                        ctypes.POINTER(PROCESSENTRY32W)]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if not snap or snap == ctypes.c_void_p(-1).value:
        return
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    try:
        if not kernel32.Process32FirstW(snap, ctypes.byref(entry)):
            return
        while True:
            yield int(entry.th32ProcessID), entry.szExeFile
            if not kernel32.Process32NextW(snap, ctypes.byref(entry)):
                break
    finally:
        kernel32.CloseHandle(snap)


def _process_elevation(pid: int) -> Optional[bool]:
    """True/False for a readable process token, None when it cannot be read."""
    if os.name != "nt":
        return None
    try:
        import ctypes
        from ctypes import wintypes

        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        TOKEN_QUERY = 0x0008
        TOKEN_ELEVATION = 20

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL,
                                         wintypes.DWORD]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        advapi32.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                              ctypes.POINTER(wintypes.HANDLE)]
        advapi32.GetTokenInformation.argtypes = [
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD)]

        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False,
                                      pid)
        if not handle:
            return None
        token = wintypes.HANDLE()
        try:
            if not advapi32.OpenProcessToken(handle, TOKEN_QUERY,
                                             ctypes.byref(token)):
                return None
            try:
                elevation = wintypes.DWORD()
                returned = wintypes.DWORD()
                ok = advapi32.GetTokenInformation(
                    token, TOKEN_ELEVATION, ctypes.byref(elevation),
                    ctypes.sizeof(elevation), ctypes.byref(returned))
                if not ok:
                    return None
                return bool(elevation.value)
            finally:
                kernel32.CloseHandle(token)
        finally:
            kernel32.CloseHandle(handle)
    except Exception:
        return None


def terminal_elevation() -> Optional[bool]:
    """Elevation of a running MT5 terminal, or None when none/unknown.

    With several terminals running, True is returned if any readable one is
    elevated, and False only when every readable one is not. A mix is reported
    as elevated because that is the state that breaks the handshake for a
    non-elevated sender.
    """
    if os.name != "nt":
        return None
    levels = []
    try:
        for pid, name in _iter_processes():
            if name.lower() in _TERMINAL_EXE_NAMES:
                level = _process_elevation(pid)
                if level is not None:
                    levels.append(level)
    except Exception:
        return None
    if not levels:
        return None
    return any(levels)


def describe_mismatch() -> str:
    """One-line privilege comparison, or '' when there is nothing to warn about.

    This is the sentence the GUI and the diagnostic show so the owner is not
    left guessing between "terminal not running" and "wrong privilege level".
    """
    ours = is_elevated()
    theirs = terminal_elevation()
    if theirs is None:
        return ""
    if ours == theirs:
        return ""
    level = "elevated" if theirs else "not elevated"
    return (f"PRIVILEGE MISMATCH: ASTRA Sender is "
            f"{'elevated' if ours else 'not elevated'} but MetaTrader 5 is "
            f"{level}. Close both and start them at the same level "
            f"(normally: neither as Administrator).")
