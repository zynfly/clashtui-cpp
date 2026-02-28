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
│ [S]Sub  [I]Install  [L]Log  [C]Config  [F1-F3]Mode  [Q]Quit │
└──────────────────────────────────────────────────────────────┘
```

## Features

- **Proxy Management** — Switch nodes, test latency, view group details
- **Profile-Based Subscriptions** — Download, switch, auto-update profiles
- **Real-Time Logs** — Colored, filterable, freeze/export
- **Install Wizard** — Download mihomo binary, SHA256 verify, systemd setup
- **Daemon Mode** — `--daemon` manages mihomo process lifecycle via IPC
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

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `F1` `F2` `F3` | Switch mode: Global / Rule / Direct |
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

display:
  language: "zh"  # "en" or "zh"

mihomo:
  config_path: "~/.config/mihomo/config.yaml"
  binary_path: "/usr/local/bin/mihomo"
  service_name: "mihomo"
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
```

- **TUI mode**: Direct REST API to mihomo for proxy/log/config operations
- **Daemon mode** (`--daemon`): Manages mihomo process lifecycle, handles profile switching, auto-updates subscriptions via Unix socket IPC
- **Degraded mode**: TUI works without daemon, using local ProfileManager

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
