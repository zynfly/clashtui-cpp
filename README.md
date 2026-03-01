# clashtui-cpp

Terminal UI for managing [Clash/Mihomo](https://github.com/MetaCubeX/mihomo) proxy on headless Linux servers. No browser or web UI needed.

```
┌──────────────────────────────────────────────────────────────┐
│  clashtui-cpp   [Global] [Rule] [Direct]    EN/中    ● 已连接 │
├─────────────────┬────────────────────┬───────────────────────┤
│  [URL-TEST]     │  ▶ Node A [12ms]  │  Type: vmess          │
│  [SELECT]       │    Node B [45ms]  │  Server: xxx          │
│  [FALLBACK]     │    Node C [?]     │  Latency: ▂▄▆▃▅      │
├─────────────────┴────────────────────┴───────────────────────┤
│ [S]Sub  [I]Install  [L]Log  [C]Config  [Alt+1-3]Mode [Q]Quit│
└──────────────────────────────────────────────────────────────┘
```

## Features

- **Proxy Management** — Switch nodes, test latency, view group details
- **Profile-Based Subscriptions** — Download, switch, auto-update profiles
- **Real-Time Logs** — Colored, filterable, freeze/export
- **Install Wizard** — Download mihomo binary, SHA256 verify, systemd setup
- **Daemon Mode** — `--daemon` manages mihomo process lifecycle via IPC
- **CLI Proxy Control** — `proxy on/off` sets shell environment variables, persists across sessions
- **CLI Update** — `update self/mihomo/all` to update clashtui-cpp and mihomo from CLI
- **CLI Profile Management** — `profile list/add/rm/update/switch` for subscription management
- **Bilingual** — English / 中文, runtime switchable (Ctrl+L)
- **Auto-Update Check** — Status bar notification when new version available

## Install

Pre-built binaries for Linux (glibc 2.35+):

**x86_64:**
```bash
curl -fsSL https://github.com/zynfly/clashtui-cpp/releases/latest/download/clashtui-cpp-linux-x86_64.tar.gz | sudo tar xz -C /usr/local/bin
```

**aarch64:**
```bash
curl -fsSL https://github.com/zynfly/clashtui-cpp/releases/latest/download/clashtui-cpp-linux-aarch64.tar.gz | sudo tar xz -C /usr/local/bin
```

> No sudo? Replace `/usr/local/bin` with `~/.local/bin` and drop `sudo`.

## Usage

```bash
# TUI mode (interactive)
clashtui-cpp

# Daemon mode (manages mihomo process)
clashtui-cpp --daemon
```

### Shell Integration (Proxy Control)

Add to your `~/.bashrc` or `~/.zshrc` (one-time setup):

```bash
# For bash
eval "$(clashtui-cpp init bash)"

# For zsh
eval "$(clashtui-cpp init zsh)"
```

Then use:

```bash
clashtui-cpp proxy on      # Set proxy env vars, new shells auto-enable too
clashtui-cpp proxy off     # Unset proxy env vars, new shells stay clean
clashtui-cpp proxy status  # Show current proxy ports and env vars
```

Without shell init, use eval manually:

```bash
eval "$(clashtui-cpp proxy env)"
```

### CLI Commands

```
clashtui-cpp                Launch TUI (default)
clashtui-cpp daemon         Run as background daemon
clashtui-cpp proxy on       Enable proxy (sets env vars + remembers)
clashtui-cpp proxy off      Disable proxy (unsets env vars + remembers)
clashtui-cpp proxy env      Print export commands (no state change)
clashtui-cpp proxy status   Show proxy ports and env var status
clashtui-cpp status         Show daemon and mihomo status
clashtui-cpp update         Update clashtui-cpp and mihomo (default: all)
clashtui-cpp update check   Check for updates without applying
clashtui-cpp update self    Update clashtui-cpp binary only
clashtui-cpp update mihomo  Update mihomo binary only
clashtui-cpp profile list   List subscription profiles
clashtui-cpp profile add <name> <url>  Add a profile
clashtui-cpp profile rm <name>         Remove a profile
clashtui-cpp profile update [name]     Update one or all profiles
clashtui-cpp profile switch <name>     Switch active profile
clashtui-cpp init <shell>   Print shell init function (bash/zsh)
clashtui-cpp version        Show version
clashtui-cpp help           Show help
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Alt+1` `Alt+2` `Alt+3` | Switch mode: Global / Rule / Direct |
| `S` | Subscription / Profile panel |
| `I` | Install wizard |
| `L` | Log viewer |
| `C` | Config panel |
| `Ctrl+L` | Toggle language EN/中 |
| `Q` / `Ctrl+C` | Quit |

**Proxy panel:**

| Key | Action |
|-----|--------|
| `↑↓` / `jk` | Navigate |
| `Tab` / `←→` | Switch columns |
| `Enter` | Select proxy |
| `T` | Test latency |
| `A` | Test all latency |
| `R` | Refresh |

**Log panel:**

| Key | Action |
|-----|--------|
| `1-4` | Filter: All / INFO / WARN / ERROR |
| `F` | Freeze/unfreeze scroll |
| `X` | Export to file |

## Configuration

Config file: `~/.config/clashtui-cpp/config.yaml`

```yaml
api:
  host: "127.0.0.1"
  port: 9090
  secret: ""
  timeout_ms: 5000

display:
  language: "zh"  # "en" or "zh"
  theme: "default"

mihomo:
  config_path: "~/.config/clashtui-cpp/mihomo/config.yaml"
  binary_path: "/usr/local/bin/mihomo"
  service_name: "mihomo"

proxy:
  enabled: false  # remembered on/off state for shell init

profiles:
  active: ""  # name of the currently active profile
```

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌─────────┐
│ clashtui-cpp │────►│ clashtui-cpp │────►│ mihomo  │
│   (TUI)     │ IPC │  (--daemon)  │fork │ process │
└─────────────┘     └──────────────┘     └─────────┘
       │                    │
       │ REST API           │ REST API
       ▼                    ▼
  ┌─────────┐         ┌─────────┐
  │ mihomo  │         │ mihomo  │
  │  API    │         │  API    │
  └─────────┘         └─────────┘

Shell integration:
  eval "$(clashtui-cpp init bash)"
  clashtui-cpp proxy on  →  stdout: export http_proxy=...
                         →  eval sets env vars in current shell
  clashtui-cpp proxy off →  stdout: unset http_proxy ...
```

- **TUI mode**: Direct REST API to mihomo for proxy/log/config operations
- **Daemon mode** (`--daemon`): Manages mihomo process lifecycle, handles profile switching, auto-updates subscriptions via Unix socket IPC
- **Degraded mode**: TUI works without daemon, using local ProfileManager
- **CLI mode**: `proxy on/off/env/status` for headless shell environments; `update` for self/mihomo updates; `profile` for subscription CRUD

## Build from Source

Requirements: CMake 3.20+, C++17 compiler, [vcpkg](https://github.com/microsoft/vcpkg)

```bash
git clone https://github.com/zynfly/clashtui-cpp.git
cd clashtui-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
```

Run tests:

```bash
cd build && ctest --output-on-failure -E '^E2E'
```

## License

MIT
