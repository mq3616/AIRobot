from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


def extend_sys_path_for_pyserial() -> None:
    user_profile = Path(os.environ.get("USERPROFILE", ""))
    candidates = [
        user_profile / ".platformio" / "penv" / "Lib" / "site-packages",
        user_profile / ".platformio" / "python3" / "Lib" / "site-packages",
    ]
    for candidate in candidates:
        if candidate.exists() and str(candidate) not in sys.path:
            sys.path.append(str(candidate))


extend_sys_path_for_pyserial()

try:
    import serial
    from serial.tools import list_ports
except Exception as exc:  # pragma: no cover
    raise SystemExit(
        "pyserial is unavailable. Install it or ensure PlatformIO is installed under "
        "%USERPROFILE%\\.platformio."
    ) from exc

import tkinter as tk
from tkinter import messagebox, ttk


PROJECT_ROOT = Path(__file__).resolve().parents[1]
PREFERENCES_PATH = PROJECT_ROOT / ".serial_console_gui.json"
DEFAULT_ENVIRONMENT = "esp32-s3-devkit"
DEFAULT_BAUD = "115200"


def zh(text: str) -> str:
    return text.encode("utf-8").decode("utf-8")


@dataclass
class AppPreferences:
    port: str = "COM3"
    baud: str = DEFAULT_BAUD
    environment: str = DEFAULT_ENVIRONMENT
    auto_reconnect: bool = True
    song: str = "canon"
    instrument: str = "musicbox"
    volume: int = 70
    musicbox_accompaniment: bool = False

    @classmethod
    def load(cls) -> "AppPreferences":
        if not PREFERENCES_PATH.exists():
            return cls()
        try:
            data = json.loads(PREFERENCES_PATH.read_text(encoding="utf-8"))
            return cls(
                port=str(data.get("port", "COM3")),
                baud=str(data.get("baud", DEFAULT_BAUD)),
                environment=str(data.get("environment", DEFAULT_ENVIRONMENT)),
                auto_reconnect=bool(data.get("auto_reconnect", True)),
                song=(
                    "juebieshu"
                    if str(data.get("song", "canon")) == "mysoul"
                    else str(data.get("song", "canon"))
                ),
                instrument=str(data.get("instrument", "musicbox")),
                volume=max(0, min(100, int(data.get("volume", 70)))),
                musicbox_accompaniment=bool(
                    data.get("musicbox_accompaniment", False)
                ),
            )
        except Exception:
            return cls()

    def save(self) -> None:
        payload = {
            "port": self.port,
            "baud": self.baud,
            "environment": self.environment,
            "auto_reconnect": self.auto_reconnect,
            "song": self.song,
            "instrument": self.instrument,
            "volume": self.volume,
            "musicbox_accompaniment": self.musicbox_accompaniment,
        }
        PREFERENCES_PATH.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
        )


class SerialConsoleApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("\u63a7\u5236\u53f0")
        self.root.geometry("1280x900")
        self.root.minsize(1080, 720)
        self.root.configure(bg="#0d1117")

        self.preferences = AppPreferences.load()
        self.serial_port: Optional[serial.Serial] = None
        self.reader_thread: Optional[threading.Thread] = None
        self.reader_stop = threading.Event()
        self.log_queue: "queue.Queue[tuple[str, str]]" = queue.Queue()
        self.upload_thread: Optional[threading.Thread] = None
        self.connected = False

        self.port_var = tk.StringVar(value=self.preferences.port)
        self.baud_var = tk.StringVar(value=self.preferences.baud)
        self.environment_var = tk.StringVar(value=self.preferences.environment)
        self.command_var = tk.StringVar()
        self.status_var = tk.StringVar(value="\u5c31\u7eea")
        self.auto_scroll_var = tk.BooleanVar(value=True)
        self.auto_reconnect_var = tk.BooleanVar(value=self.preferences.auto_reconnect)
        self.song_var = tk.StringVar(value=self.preferences.song)
        self.instrument_var = tk.StringVar(value=self.preferences.instrument)
        self.volume_var = tk.IntVar(value=self.preferences.volume)
        self.volume_label_var = tk.StringVar(
            value=f"{self.preferences.volume}%"
        )
        self.musicbox_accompaniment_var = tk.BooleanVar(
            value=self.preferences.musicbox_accompaniment
        )
        self.musicbox_accompaniment_label_var = tk.StringVar()

        self.available_ports: list[str] = []
        self.quick_commands = [
            ("\u5e2e\u52a9", "help"),
            ("WiFi \u72b6\u6001", "wifi"),
            ("\u8bbe\u5907\u8d44\u6599", "profile"),
            ("\u5185\u5b58", "mem"),
            ("\u6d4b\u8bd5\u97f3", "beep"),
            ("\u91cd\u8fde WiFi", "reconnect"),
            ("\u95ee\u4e00\u53e5\u8bdd", "ask hello"),
            ("\u8bf4\u4e00\u53e5\u8bdd", "say hello i am PiPi"),
        ]
        self.song_options = {
            "\u5361\u519c": "canon",
            "\u9e1f\u4e4b\u8bd7": "tori",
            "\u98ce\u5c45\u4f4f\u7684\u8857\u9053": "truth",
            "\u8bc0\u522b\u4e66": "juebieshu",
            "\u8d77\u98ce\u4e86": "qifeng",
            "\u7a7f\u8d8a\u65f6\u7a7a\u7684\u601d\u5ff5": "sinian",
        }
        self.song_reverse = {value: key for key, value in self.song_options.items()}
        self.instrument_options = {
            "\u516b\u97f3\u76d2": "musicbox",
            "\u94a2\u7434": "piano",
        }
        self.instrument_reverse = {
            value: key for key, value in self.instrument_options.items()
        }

        self._build_ui()
        self._refresh_musicbox_accompaniment_label()
        self._refresh_ports()
        self._apply_default_port()
        self.root.after(120, self._drain_log_queue)
        self.root.after(500, self._auto_connect_on_startup)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        self.style = ttk.Style()
        self.style.theme_use("clam")
        self.style.configure("TFrame", background="#0d1117")
        self.style.configure("Card.TFrame", background="#161b22")
        self.style.configure("Body.TLabel", background="#161b22", foreground="#9fb0c3")
        self.style.configure(
            "Action.TButton",
            padding=(10, 8),
            background="#238636",
            foreground="#f0f6fc",
            borderwidth=0,
            focusthickness=0,
        )
        self.style.map(
            "Action.TButton",
            background=[("active", "#2ea043"), ("pressed", "#1a7f37"), ("disabled", "#30363d")],
            foreground=[("disabled", "#6e7681")],
        )
        self.style.configure(
            "Primary.TButton",
            padding=(14, 14),
            background="#1f6feb",
            foreground="#f0f6fc",
            borderwidth=0,
            focusthickness=0,
        )
        self.style.map(
            "Primary.TButton",
            background=[("active", "#388bfd"), ("pressed", "#1158c7"), ("disabled", "#30363d")],
            foreground=[("disabled", "#6e7681")],
        )
        self.style.configure(
            "Danger.TButton",
            padding=(14, 14),
            background="#da3633",
            foreground="#f0f6fc",
            borderwidth=0,
            focusthickness=0,
        )
        self.style.map(
            "Danger.TButton",
            background=[("active", "#f85149"), ("pressed", "#b62324"), ("disabled", "#30363d")],
            foreground=[("disabled", "#6e7681")],
        )
        self.style.configure(
            "TEntry",
            fieldbackground="#0f141b",
            foreground="#f0f6fc",
            insertcolor="#f0f6fc",
            bordercolor="#30363d",
            lightcolor="#30363d",
            darkcolor="#30363d",
        )
        self.style.configure(
            "TCombobox",
            fieldbackground="#0f141b",
            foreground="#f0f6fc",
            background="#0f141b",
            arrowcolor="#f0f6fc",
            bordercolor="#30363d",
            lightcolor="#30363d",
            darkcolor="#30363d",
        )
        self.style.map(
            "TCombobox",
            fieldbackground=[("readonly", "#0f141b")],
            selectbackground=[("readonly", "#1f6feb")],
            selectforeground=[("readonly", "#f0f6fc")],
        )

        shell = ttk.Frame(self.root, padding=16)
        shell.pack(fill="both", expand=True)

        header = ttk.Frame(shell)
        header.pack(fill="x")

        tk.Label(
            header,
            text="AIRobot \u63a7\u5236\u53f0",
            font=("Segoe UI Semibold", 22),
            bg="#0d1117",
            fg="#f0f6fc",
        ).pack(anchor="w")

        tk.Label(
            header,
            text="\u4e00\u4e2a\u7a97\u53e3\u91cc\u5b8c\u6210\u4e32\u53e3\u76d1\u89c6\u3001\u547d\u4ee4\u53d1\u9001\u548c\u56fa\u4ef6\u70e7\u5f55\u3002",
            font=("Segoe UI", 10),
            bg="#0d1117",
            fg="#8b949e",
        ).pack(anchor="w", pady=(4, 12))

        toolbar = ttk.Frame(shell, style="Card.TFrame", padding=12)
        toolbar.pack(fill="x")

        ttk.Label(toolbar, text="\u4e32\u53e3", style="Body.TLabel").grid(row=0, column=0, sticky="w")
        self.port_combo = ttk.Combobox(
            toolbar, width=16, textvariable=self.port_var, state="readonly", values=[]
        )
        self.port_combo.grid(row=1, column=0, padx=(0, 12), pady=(4, 0), sticky="we")

        ttk.Label(toolbar, text="\u6ce2\u7279\u7387", style="Body.TLabel").grid(row=0, column=1, sticky="w")
        self.baud_combo = ttk.Combobox(
            toolbar,
            width=12,
            textvariable=self.baud_var,
            state="readonly",
            values=["9600", "57600", "115200", "230400", "460800", "921600"],
        )
        self.baud_combo.grid(row=1, column=1, padx=(0, 12), pady=(4, 0), sticky="we")

        ttk.Label(toolbar, text="\u73af\u5883", style="Body.TLabel").grid(row=0, column=2, sticky="w")
        self.environment_entry = ttk.Entry(toolbar, textvariable=self.environment_var, width=20)
        self.environment_entry.grid(row=1, column=2, padx=(0, 16), pady=(4, 0), sticky="we")

        controls = ttk.Frame(toolbar, style="Card.TFrame")
        controls.grid(row=1, column=3, sticky="e")

        self.refresh_button = ttk.Button(
            controls, text="\u5237\u65b0\u4e32\u53e3", style="Action.TButton", command=self._refresh_ports
        )
        self.refresh_button.pack(side="left", padx=(0, 8))

        self.connect_button = ttk.Button(
            controls, text="\u8fde\u63a5\u4e32\u53e3", style="Action.TButton", command=self._toggle_connection
        )
        self.connect_button.pack(side="left", padx=(0, 8))

        self.reset_button = ttk.Button(
            controls, text="\u91cd\u7f6e\u677f\u5b50", style="Action.TButton", command=self._reset_board
        )
        self.reset_button.pack(side="left", padx=(0, 8))

        self.upload_button = ttk.Button(
            controls, text="\u70e7\u5f55\u56fa\u4ef6", style="Action.TButton", command=self._start_upload
        )
        self.upload_button.pack(side="left")

        toolbar.columnconfigure(3, weight=1)

        options = ttk.Frame(shell, padding=(0, 10, 0, 8))
        options.pack(fill="x")

        self.auto_scroll_check = tk.Checkbutton(
            options,
            text="\u81ea\u52a8\u6eda\u52a8\u65e5\u5fd7",
            variable=self.auto_scroll_var,
            bg="#0d1117",
            fg="#c9d1d9",
            activebackground="#0d1117",
            activeforeground="#f0f6fc",
            selectcolor="#1f6feb",
            highlightthickness=1,
            highlightbackground="#3b4552",
            highlightcolor="#58a6ff",
            bd=0,
            relief="flat",
            font=("Segoe UI", 10),
        )
        self.auto_scroll_check.pack(side="left")

        self.auto_reconnect_check = tk.Checkbutton(
            options,
            text="\u70e7\u5f55\u6210\u529f\u540e\u81ea\u52a8\u91cd\u8fde\u4e32\u53e3",
            variable=self.auto_reconnect_var,
            bg="#0d1117",
            fg="#c9d1d9",
            activebackground="#0d1117",
            activeforeground="#f0f6fc",
            selectcolor="#1f6feb",
            highlightthickness=1,
            highlightbackground="#3b4552",
            highlightcolor="#58a6ff",
            bd=0,
            relief="flat",
            font=("Segoe UI", 10),
        )
        self.auto_reconnect_check.pack(side="left", padx=(16, 0))

        body = ttk.Frame(shell)
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=3)
        body.columnconfigure(1, weight=2)
        body.rowconfigure(0, weight=1)

        log_card = ttk.Frame(body, style="Card.TFrame", padding=12)
        log_card.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        log_card.rowconfigure(1, weight=1)
        log_card.columnconfigure(0, weight=1)

        tk.Label(
            log_card,
            text="\u8bbe\u5907\u65e5\u5fd7",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, sticky="w")

        log_frame = ttk.Frame(log_card)
        log_frame.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)

        self.log_text = tk.Text(
            log_frame,
            wrap="word",
            font=("Consolas", 10),
            bg="#0b0f14",
            fg="#dce7f3",
            insertbackground="#dce7f3",
            relief="flat",
            padx=12,
            pady=12,
        )
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scroll.set)
        self.log_text.tag_configure("system", foreground="#9dc7a6")
        self.log_text.tag_configure("error", foreground="#ff8b94")
        self.log_text.tag_configure("upload", foreground="#ffd27f")
        self.log_text.tag_configure("board", foreground="#d8f7de")
        self.log_text.tag_configure("sent", foreground="#8fd6ff")

        right = ttk.Frame(body)
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(0, weight=0)
        right.rowconfigure(1, weight=1)
        right.rowconfigure(2, weight=1)
        right.rowconfigure(3, weight=1)

        command_card = ttk.Frame(right, style="Card.TFrame", padding=12)
        command_card.grid(row=0, column=0, sticky="ew")
        command_card.columnconfigure(0, weight=1)

        tk.Label(
            command_card,
            text="\u53d1\u9001\u547d\u4ee4",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, sticky="w")

        entry_frame = ttk.Frame(command_card, style="Card.TFrame")
        entry_frame.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        entry_frame.columnconfigure(0, weight=1)

        self.command_entry = ttk.Entry(entry_frame, textvariable=self.command_var)
        self.command_entry.grid(row=0, column=0, sticky="ew", padx=(0, 8))
        self.command_entry.bind("<Return>", lambda _: self._send_command())

        self.send_button = ttk.Button(
            entry_frame, text="\u53d1\u9001", style="Action.TButton", command=self._send_command
        )
        self.send_button.grid(row=0, column=1)

        volume_card = ttk.Frame(right, style="Card.TFrame", padding=12)
        volume_card.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        volume_card.columnconfigure(0, weight=1)

        tk.Label(
            volume_card,
            text="\u97f3\u91cf\u63a7\u5236",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, sticky="w")

        volume_row = ttk.Frame(volume_card, style="Card.TFrame")
        volume_row.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        volume_row.columnconfigure(0, weight=1)

        self.volume_scale = tk.Scale(
            volume_row,
            from_=0,
            to=100,
            orient="horizontal",
            variable=self.volume_var,
            showvalue=False,
            resolution=1,
            bg="#161b22",
            fg="#c9d1d9",
            troughcolor="#0f141b",
            activebackground="#58a6ff",
            highlightthickness=0,
            bd=0,
            font=("Segoe UI", 10),
            command=self._on_volume_changed,
        )
        self.volume_scale.grid(row=0, column=0, sticky="ew")
        self.volume_scale.bind("<ButtonRelease-1>", self._apply_volume_from_event)

        tk.Label(
            volume_row,
            textvariable=self.volume_label_var,
            font=("Segoe UI Semibold", 10),
            bg="#161b22",
            fg="#58a6ff",
            width=5,
        ).grid(row=0, column=1, padx=(12, 0))

        ttk.Button(
            volume_card,
            text="\u5e94\u7528\u97f3\u91cf",
            style="Action.TButton",
            command=self._apply_volume,
        ).grid(row=2, column=0, sticky="e", pady=(10, 0))

        accomp_card = ttk.Frame(right, style="Card.TFrame", padding=12)
        accomp_card.grid(row=2, column=0, sticky="ew", pady=(10, 0))
        accomp_card.columnconfigure(0, weight=1)
        accomp_card.columnconfigure(1, weight=1)

        tk.Label(
            accomp_card,
            text="\u516b\u97f3\u76d2\u4f34\u594f",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, sticky="w")

        tk.Label(
            accomp_card,
            textvariable=self.musicbox_accompaniment_label_var,
            font=("Segoe UI Semibold", 10),
            bg="#161b22",
            fg="#58a6ff",
        ).grid(row=0, column=1, sticky="e")

        ttk.Button(
            accomp_card,
            text="\u5207\u6362\u4f34\u594f",
            style="Action.TButton",
            command=self._toggle_musicbox_accompaniment,
        ).grid(row=1, column=0, columnspan=2, sticky="ew", pady=(10, 0))

        quick_card = ttk.Frame(right, style="Card.TFrame", padding=12)
        quick_card.grid(row=3, column=0, sticky="nsew", pady=(10, 0))
        quick_card.columnconfigure(0, weight=1)

        tk.Label(
            quick_card,
            text="\u5feb\u6377\u547d\u4ee4",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, sticky="w")

        button_grid = ttk.Frame(quick_card, style="Card.TFrame")
        button_grid.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        for index, (label, command) in enumerate(self.quick_commands):
            row = index // 2
            column = index % 2
            ttk.Button(
                button_grid,
                text=label,
                style="Action.TButton",
                command=lambda value=command: self._send_command(value),
            ).grid(row=row, column=column, sticky="ew", padx=(0, 8), pady=(0, 8))
        button_grid.columnconfigure(0, weight=1)
        button_grid.columnconfigure(1, weight=1)

        music_card = ttk.Frame(right, style="Card.TFrame", padding=12)
        music_card.grid(row=4, column=0, sticky="nsew", pady=(10, 0))
        music_card.columnconfigure(0, weight=1)
        music_card.columnconfigure(1, weight=1)

        tk.Label(
            music_card,
            text="\u97f3\u4e50\u63a7\u5236",
            font=("Segoe UI Semibold", 12),
            bg="#161b22",
            fg="#f0f6fc",
        ).grid(row=0, column=0, columnspan=2, sticky="w")

        tk.Label(
            music_card,
            text="\u6b4c\u66f2",
            font=("Segoe UI", 10),
            bg="#161b22",
            fg="#9fb0c3",
        ).grid(row=1, column=0, sticky="w", pady=(10, 4))
        self.song_combo = ttk.Combobox(
            music_card,
            state="readonly",
            values=list(self.song_options.keys()),
        )
        self.song_combo.set(self.song_reverse.get(self.song_var.get(), "\u5361\u519c"))
        self.song_combo.grid(row=2, column=0, sticky="ew", padx=(0, 8))

        tk.Label(
            music_card,
            text="\u97f3\u8272",
            font=("Segoe UI", 10),
            bg="#161b22",
            fg="#9fb0c3",
        ).grid(row=1, column=1, sticky="w", pady=(10, 4))
        self.instrument_combo = ttk.Combobox(
            music_card,
            state="readonly",
            values=list(self.instrument_options.keys()),
        )
        self.instrument_combo.set(
            self.instrument_reverse.get(self.instrument_var.get(), "\u516b\u97f3\u76d2")
        )
        self.instrument_combo.grid(row=2, column=1, sticky="ew")

        player_bar = ttk.Frame(music_card, style="Card.TFrame")
        player_bar.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(14, 0))
        player_bar.columnconfigure(0, weight=1)
        player_bar.columnconfigure(1, weight=1)

        ttk.Button(
            player_bar,
            text="\u64ad\u653e",
            style="Primary.TButton",
            command=self._play_selected_song,
        ).grid(row=0, column=0, sticky="ew", padx=(0, 8))
        ttk.Button(
            player_bar,
            text="\u6682\u505c",
            style="Danger.TButton",
            command=self._stop_song,
        ).grid(row=0, column=1, sticky="ew")

        song_grid = ttk.Frame(music_card, style="Card.TFrame")
        song_grid.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        for index, (label, value) in enumerate(self.song_options.items()):
            row = index // 2
            column = index % 2
            ttk.Button(
                song_grid,
                text=label,
                style="Action.TButton",
                command=lambda v=value: self._play_song(v),
            ).grid(row=row, column=column, sticky="ew", padx=(0, 8), pady=(0, 8))
        song_grid.columnconfigure(0, weight=1)
        song_grid.columnconfigure(1, weight=1)

        action_bar = ttk.Frame(shell, style="Card.TFrame", padding=12)
        action_bar.pack(fill="x", pady=(10, 0))

        ttk.Button(
            action_bar, text="\u6e05\u7a7a\u65e5\u5fd7", style="Action.TButton", command=self._clear_log
        ).pack(side="left")

        self.status_label = tk.Label(
            action_bar,
            textvariable=self.status_var,
            font=("Segoe UI Semibold", 10),
            bg="#161b22",
            fg="#9fb0c3",
        )
        self.status_label.pack(side="right")

    def _apply_default_port(self) -> None:
        if self.available_ports and self.port_var.get() not in self.available_ports:
            self.port_var.set(self.available_ports[0])

    def _refresh_ports(self) -> None:
        ports = sorted(item.device for item in list_ports.comports())
        self.available_ports = ports
        self.port_combo["values"] = ports
        self._apply_default_port()
        if ports:
            self._append_log("system", f"\u53d1\u73b0\u4e32\u53e3: {', '.join(ports)}")
            self._set_status("\u4e32\u53e3\u5217\u8868\u5df2\u5237\u65b0")
        else:
            self._append_log("error", "\u6ca1\u6709\u68c0\u6d4b\u5230\u4e32\u53e3\u8bbe\u5907")
            self._set_status("\u672a\u68c0\u6d4b\u5230\u4e32\u53e3")

    def _toggle_connection(self) -> None:
        if self.connected:
            self._disconnect_serial()
        else:
            self._connect_serial()

    def _auto_connect_on_startup(self) -> None:
        if self.connected:
            return
        port = self.port_var.get().strip()
        if not port:
            return
        if self.available_ports and port not in self.available_ports:
            self._append_log("error", f"\u542f\u52a8\u65f6\u672a\u627e\u5230\u4e32\u53e3: {port}")
            self._set_status("\u672a\u627e\u5230\u4fdd\u5b58\u7684\u4e32\u53e3")
            return
        self._append_log("system", f"\u6b63\u5728\u81ea\u52a8\u8fde\u63a5 {port}")
        self._connect_serial()

    def _connect_serial(self) -> None:
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("\u7f3a\u5c11\u4e32\u53e3", "\u8bf7\u5148\u9009\u62e9\u4e32\u53e3\u3002")
            return
        if self.connected:
            return
        try:
            self.serial_port = serial.Serial(
                port=port,
                baudrate=int(self.baud_var.get()),
                timeout=0.2,
                write_timeout=1.0,
            )
        except Exception as exc:
            self.serial_port = None
            self._append_log("error", f"\u4e32\u53e3\u8fde\u63a5\u5931\u8d25: {exc}")
            self._set_status("\u4e32\u53e3\u8fde\u63a5\u5931\u8d25")
            return

        self.connected = True
        self.reader_stop.clear()
        self.reader_thread = threading.Thread(target=self._serial_reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_button.configure(text="\u65ad\u5f00\u4e32\u53e3")
        self._append_log("system", f"\u5df2\u8fde\u63a5 {port} @ {self.baud_var.get()}")
        self._set_status(f"\u5df2\u8fde\u63a5 {port}")
        self.root.after(150, self._sync_volume_after_connect)
        self.root.after(260, self._sync_musicbox_accompaniment_after_connect)
        self.command_entry.focus_set()

    def _disconnect_serial(self) -> None:
        self.reader_stop.set()
        if self.serial_port is not None:
            try:
                if self.serial_port.is_open:
                    self.serial_port.close()
            except Exception:
                pass
        self.serial_port = None
        self.connected = False
        self.connect_button.configure(text="\u8fde\u63a5\u4e32\u53e3")
        self._append_log("system", "\u4e32\u53e3\u5df2\u65ad\u5f00")
        self._set_status("\u4e32\u53e3\u672a\u8fde\u63a5")

    def _serial_reader_loop(self) -> None:
        assert self.serial_port is not None
        while not self.reader_stop.is_set():
            try:
                raw = self.serial_port.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if text:
                    self.log_queue.put(("board", text))
            except Exception as exc:
                self.log_queue.put(("error", f"\u4e32\u53e3\u8bfb\u53d6\u5f02\u5e38: {exc}"))
                self.root.after(0, self._disconnect_serial)
                break

    def _send_command(self, command: Optional[str] = None) -> None:
        text = (command if command is not None else self.command_var.get()).strip()
        if not text:
            return
        if not self.connected or self.serial_port is None or not self.serial_port.is_open:
            messagebox.showwarning("\u4e32\u53e3\u672a\u8fde\u63a5", "\u8bf7\u5148\u8fde\u63a5\u4e32\u53e3\u3002")
            return
        try:
            self.serial_port.write((text + "\n").encode("utf-8"))
            self._append_log("sent", f">>> {text}")
            self._set_status(f"\u5df2\u53d1\u9001: {text}")
            if command is None:
                self.command_var.set("")
        except Exception as exc:
            self._append_log("error", f"\u53d1\u9001\u5931\u8d25: {exc}")
            self._set_status("\u53d1\u9001\u5931\u8d25")

    def _on_volume_changed(self, _value: str) -> None:
        self.volume_label_var.set(f"{self.volume_var.get()}%")

    def _apply_volume_from_event(self, _event: tk.Event[tk.Misc]) -> None:
        self._apply_volume()

    def _apply_volume(self) -> None:
        self.volume_label_var.set(f"{self.volume_var.get()}%")
        self._save_preferences()
        if not self.connected:
            self._set_status("\u97f3\u91cf\u5df2\u4fdd\u5b58\uff0c\u8fde\u63a5\u540e\u81ea\u52a8\u540c\u6b65")
            return
        self._send_command(f"volume {self.volume_var.get()}")

    def _sync_volume_after_connect(self) -> None:
        if not self.connected:
            return
        self._send_command(f"volume {self.volume_var.get()}")

    def _refresh_musicbox_accompaniment_label(self) -> None:
        self.musicbox_accompaniment_label_var.set(
            "\u5df2\u5f00\u542f" if self.musicbox_accompaniment_var.get() else "\u5df2\u5173\u95ed"
        )

    def _toggle_musicbox_accompaniment(self) -> None:
        self.musicbox_accompaniment_var.set(
            not self.musicbox_accompaniment_var.get()
        )
        self._refresh_musicbox_accompaniment_label()
        self._save_preferences()
        if not self.connected:
            self._set_status("\u4f34\u594f\u72b6\u6001\u5df2\u4fdd\u5b58\uff0c\u8fde\u63a5\u540e\u81ea\u52a8\u540c\u6b65")
            return
        self._send_command(
            "accomp on" if self.musicbox_accompaniment_var.get() else "accomp off"
        )

    def _sync_musicbox_accompaniment_after_connect(self) -> None:
        if not self.connected:
            return
        self._send_command(
            "accomp on" if self.musicbox_accompaniment_var.get() else "accomp off"
        )

    def _reset_board(self) -> None:
        if not self.connected or self.serial_port is None or not self.serial_port.is_open:
            messagebox.showwarning("\u4e32\u53e3\u672a\u8fde\u63a5", "\u8bf7\u5148\u8fde\u63a5\u4e32\u53e3\u540e\u518d\u91cd\u7f6e\u3002")
            return
        try:
            self.serial_port.dtr = False
            self.serial_port.rts = True
            time.sleep(0.12)
            self.serial_port.dtr = True
            self.serial_port.rts = False
            self._append_log("system", "\u5df2\u89e6\u53d1\u677f\u5b50\u590d\u4f4d")
            self._set_status("\u677f\u5b50\u5df2\u590d\u4f4d")
        except Exception as exc:
            self._append_log("error", f"\u590d\u4f4d\u5931\u8d25: {exc}")
            self._set_status("\u590d\u4f4d\u5931\u8d25")

    def _selected_song_value(self) -> str:
        return self.song_options.get(self.song_combo.get(), "canon")

    def _selected_instrument_value(self) -> str:
        return self.instrument_options.get(self.instrument_combo.get(), "musicbox")

    def _play_selected_song(self) -> None:
        self._play_song(self._selected_song_value())

    def _play_song(self, song: str) -> None:
        self.song_var.set(song)
        label = self.song_reverse.get(song, "\u5361\u519c")
        self.song_combo.set(label)
        instrument = self._selected_instrument_value()
        self.instrument_var.set(instrument)
        self._send_command(f"melody {song} {instrument}")

    def _stop_song(self) -> None:
        self._send_command("melody stop")

    def _start_upload(self) -> None:
        if self.upload_thread and self.upload_thread.is_alive():
            return
        port = self.port_var.get().strip()
        environment = self.environment_var.get().strip()
        if not port:
            messagebox.showerror("\u7f3a\u5c11\u4e32\u53e3", "\u8bf7\u5148\u9009\u62e9\u4e32\u53e3\u3002")
            return
        if not environment:
            messagebox.showerror("\u7f3a\u5c11\u73af\u5883", "\u8bf7\u5148\u586b\u5199 PlatformIO \u73af\u5883\u540d\u3002")
            return

        self._save_preferences()
        self.upload_button.configure(state="disabled")
        self._set_status("\u6b63\u5728\u70e7\u5f55\u56fa\u4ef6")

        self.upload_thread = threading.Thread(
            target=self._upload_worker,
            args=(port, environment, self.auto_reconnect_var.get()),
            daemon=True,
        )
        self.upload_thread.start()

    def _upload_worker(self, port: str, environment: str, auto_reconnect: bool) -> None:
        reopen_after_upload = self.connected
        if self.connected:
            self.root.after(0, self._disconnect_serial)
            time.sleep(0.5)

        self._stop_external_monitor_processes(port)
        pio = Path(os.environ.get("USERPROFILE", "")) / ".platformio" / "penv" / "Scripts" / "platformio.exe"
        if not pio.exists():
            self.log_queue.put(("error", f"\u672a\u627e\u5230 PlatformIO: {pio}"))
            self.root.after(0, lambda: self.upload_button.configure(state="normal"))
            return

        command = [str(pio), "run", "-e", environment, "-t", "upload", "--upload-port", port]
        self.log_queue.put(("upload", f"$ {' '.join(command)}"))

        try:
            process = subprocess.Popen(
                command,
                cwd=str(PROJECT_ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            assert process.stdout is not None
            for line in process.stdout:
                text = line.rstrip()
                if text:
                    self.log_queue.put(("upload", text))
            return_code = process.wait()
        except Exception as exc:
            self.log_queue.put(("error", f"\u542f\u52a8\u70e7\u5f55\u5931\u8d25: {exc}"))
            return_code = 1

        if return_code == 0:
            self.log_queue.put(("system", f"\u70e7\u5f55\u5b8c\u6210: {port}"))
            self.root.after(0, lambda: self._set_status("\u70e7\u5f55\u6210\u529f"))
            if auto_reconnect and reopen_after_upload:
                time.sleep(1.5)
                self.root.after(0, self._connect_serial)
        else:
            self.log_queue.put(("error", f"\u70e7\u5f55\u5931\u8d25\uff0c\u9000\u51fa\u7801 {return_code}"))
            self.root.after(0, lambda: self._set_status("\u70e7\u5f55\u5931\u8d25"))

        self.root.after(0, lambda: self.upload_button.configure(state="normal"))

    def _stop_external_monitor_processes(self, port: str) -> None:
        ps_script = f"""
$port = '{port}'
Get-CimInstance Win32_Process |
  Where-Object {{
    $_.CommandLine -match 'device monitor' -and
    $_.CommandLine -match [regex]::Escape($port)
  }} |
  ForEach-Object {{
    try {{
      Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
      Write-Output ('Stopped PID ' + $_.ProcessId)
    }} catch {{}}
  }}
"""
        try:
            result = subprocess.run(
                ["powershell", "-NoProfile", "-Command", ps_script],
                cwd=str(PROJECT_ROOT),
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=10,
                check=False,
            )
            for line in result.stdout.splitlines():
                if line.strip():
                    self.log_queue.put(("system", line.strip()))
        except Exception as exc:
            self.log_queue.put(("error", f"\u6e05\u7406\u5916\u90e8\u76d1\u89c6\u8fdb\u7a0b\u5931\u8d25: {exc}"))

    def _append_log(self, tag: str, text: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {text}\n", tag)
        if self.auto_scroll_var.get():
            self.log_text.see("end")

    def _drain_log_queue(self) -> None:
        while True:
            try:
                tag, text = self.log_queue.get_nowait()
            except queue.Empty:
                break
            self._append_log(tag, text)
        self.root.after(120, self._drain_log_queue)

    def _clear_log(self) -> None:
        self.log_text.delete("1.0", "end")
        self._set_status("\u65e5\u5fd7\u5df2\u6e05\u7a7a")

    def _set_status(self, text: str) -> None:
        self.status_var.set(text)

    def _save_preferences(self) -> None:
        self.preferences.port = self.port_var.get().strip()
        self.preferences.baud = self.baud_var.get().strip()
        self.preferences.environment = self.environment_var.get().strip()
        self.preferences.auto_reconnect = self.auto_reconnect_var.get()
        self.preferences.song = self._selected_song_value()
        self.preferences.instrument = self._selected_instrument_value()
        self.preferences.volume = self.volume_var.get()
        self.preferences.musicbox_accompaniment = self.musicbox_accompaniment_var.get()
        self.preferences.save()

    def _on_close(self) -> None:
        self._save_preferences()
        self._disconnect_serial()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    app = SerialConsoleApp(root)
    app._append_log("system", f"\u9879\u76ee\u76ee\u5f55: {PROJECT_ROOT}")
    app._append_log("system", "\u51c6\u5907\u5c31\u7eea\u3002\u5148\u8fde\u63a5\u4e32\u53e3\uff0c\u518d\u53d1\u547d\u4ee4\u6216\u70e7\u5f55\u56fa\u4ef6\u3002")
    root.mainloop()


if __name__ == "__main__":
    main()
