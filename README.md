# mapps

A C++ library and CLI for Meshtastic app development: `.mapp` packaging, signing, verification, and Berry runtime hosting.

**mapps** serves three purposes:

1. **Berry runtime framework** — owns the Berry VM, provides the abstract `AppRuntime` interface, and ships core bindings (starting with `app_state`). Consumers (firmware, device-ui) register platform-specific bindings for display, mesh networking, etc.
2. **PlatformIO library** — importable by the [Meshtastic firmware](https://github.com/meshtastic/firmware) via `lib_deps` for on-device `.mapp` parsing, signature verification, and app execution
3. **Standalone CLI** — builds as a single native binary on Linux/macOS for app creation, packaging, signing, and verification

## What are Meshtastic Apps?

Meshtastic apps are sandboxed programs written in [Berry](https://github.com/berry-lang/berry), a lightweight scripting language designed for embedded systems. They run inside the Meshtastic firmware on ESP32-based devices, extending the device with custom functionality — new UI screens, network tools, sensor integrations, and more.

Apps are executed by the firmware's **AppModule**, which hosts a Berry virtual machine with access to native modules for display rendering, HTTP networking, and JSON parsing. Each app runs with an instruction limit (100,000 per VM call) to prevent runaway scripts, and a permission system controls access to sensitive capabilities like network access.

### App lifecycle

1. The firmware discovers apps by scanning the `/apps/` directory (flash or SD card) for subdirectories containing an `app.json` manifest
2. If a signature file (`.sig`) is present, the firmware verifies it using Ed25519 + Blake2b-512
3. The device owner approves the developer's public key and the app's requested permissions
4. The Berry VM loads the app's entry script, calls `init()` once, then calls `draw()` in a loop to render frames

### Trust chain

```
Discovered → Signature Verified → Developer Trusted → Permissions Approved → Ready
```

## Structure of a Meshtastic App

A Meshtastic app is a directory containing at minimum a manifest (`app.json`) and one or more Berry scripts:

```
my_app/
├── app.json        # Manifest (required, must be first in .mapp)
├── bui.be          # Built-in UI entry point (OLED display)
├── mui.be          # Meshtastic UI entry point (TFT/LVGL, optional)
└── app.sig         # Ed25519 signature (added by `mapps sign`)
```

### app.json manifest

```json
{
  "name": "My App",
  "version": "0.1",
  "author": "Developer Name",
  "entry": {
    "bui": "bui.be",
    "mui": "mui.be"
  },
  "permissions": []
}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Display name shown on the device |
| `version` | string | Semantic version string |
| `author` | string | Developer or organization name |
| `entry` | object | Map of entry point names to Berry script filenames |
| `permissions` | array | List of permission strings the app requires |
| `signature` | string | Filename of the `.sig` file (added automatically by signing) |

#### Entry points

| Name | Purpose |
|---|---|
| `bui` | **Built-in User Interface** — entry point for the firmware's OLED display path. The firmware calls `draw()` in a loop with access to the `display` module. |
| `mui` | **Meshtastic UI** — entry point for the TFT/LVGL display path via [device-ui](https://github.com/meshtastic/device-ui). The script uses `ui` module bindings for LVGL widgets. |

The `entry` field must be an object mapping entry point names to script filenames.

#### Permissions

Apps declare the permissions they need. The device owner must approve them before the app can launch.

| Permission | Module | Description |
|---|---|---|
| `http-client` | `http` | Make HTTP requests to external servers |

The `display` and `json` modules are always available without permissions.

### Berry script examples

**bui.be** (Built-in UI — OLED display):

```berry
def draw()
    # Called each frame to render to the OLED display
    var w = display.width()
    var h = display.height()
    display.draw_string(w / 2 - 30, h / 2 - 5, "Hello Mesh!")
end
```

**mui.be** (Meshtastic UI — TFT/LVGL):

```berry
var root = ui.root()
var label = ui.label(root, "Hello Mesh!")
ui.set_pos(label, 10, 10)
```

## .mapp Binary Format

The `.mapp` format is a simple binary container that bundles all app files into a single distributable package. It is designed to be trivially parseable on constrained embedded devices.

### Layout

```
┌─────────────────────────────────────────────┐
│ Header (7 bytes)                            │
│   [0-3]  Magic: 0x4D415050 ("MAPP", LE)    │
│   [4]    Version: 0x01                      │
│   [5-6]  File count (uint16 LE)             │
├─────────────────────────────────────────────┤
│ File Entry 0 (must be "app.json")           │
│   [0-1]  Filename length (uint16 LE)        │
│   [2..N] Filename (UTF-8, no null term)     │
│   [N+0..N+3] Content length (uint32 LE)     │
│   [N+4..M] Content (raw bytes)              │
├─────────────────────────────────────────────┤
│ File Entry 1                                │
│   (same structure)                          │
├─────────────────────────────────────────────┤
│ ...                                         │
├─────────────────────────────────────────────┤
│ File Entry N-1                              │
│   (same structure)                          │
└─────────────────────────────────────────────┘
```

- All multi-byte integers are **little-endian**
- The first file entry **must** be `app.json`
- File entries are sequential with no padding or alignment
- Filenames are UTF-8 encoded without a null terminator
- After signing, an `app.sig` entry is appended and a `"signature"` field is added to the manifest

### Example hex dump

```
00000000: 5050 414d 0102 00    PPAM...       ← "MAPP" (LE) + v1 + 2 files
00000007: 0800 6170 702e 6a    ..app.j       ← filename len=8, "app.json"
0000000f: 736f 6e5d 0000 00    son]...       ← content len=93
00000016: 7b22 6e61 6d65 22    {"name"       ← manifest JSON content
...
```

## Signing and Verification

Meshtastic apps use **Ed25519 + Blake2b-512** for cryptographic signing, matching the firmware's signature verification implementation exactly. This ensures that apps signed with `mapps` are verifiable on-device.

### How signing works

```
                    ┌──────────────────┐
                    │   app.json       │──── strip "signature" field
                    │   (manifest)     │           │
                    └──────────────────┘           ▼
                                            ┌───────────┐
                    ┌──────────────────┐     │           │
                    │   main.be        │────►│  Blake2b  │
                    │   (+ other files)│     │   -512    │
                    └──────────────────┘     │           │
                                            └─────┬─────┘
                                                  │ 64-byte digest
                                                  ▼
                    ┌──────────────────┐     ┌───────────┐
                    │  Private Key     │────►│  Ed25519  │
                    │  (32 bytes)      │     │   Sign    │
                    └──────────────────┘     └─────┬─────┘
                                                  │ 64-byte signature
                                                  ▼
                                            ┌───────────┐
                                            │  app.sig  │
                                            │ (101 bytes)│
                                            └───────────┘
```

### Step 1: Digest computation (Blake2b-512)

The digest is computed over the **stripped manifest JSON** followed by **each file's name and content**, in order:

```
Blake2b-512(
    stripped_manifest_json
    + filename₁ + \0 + content₁ + \0
    + filename₂ + \0 + content₂ + \0
    + ...
)
```

- The manifest JSON has its `"signature"` field removed before hashing, using raw string manipulation (not parse-reserialize) to preserve the exact byte representation
- Only non-manifest, non-signature files are included in the file hash (i.e., `app.json` content goes through the stripped JSON path, and `*.sig` files are excluded)
- Files are hashed in the order they appear in the `.mapp` container
- Each filename and content is followed by a null byte (`\0`) separator

### Step 2: Signing (Ed25519)

The 64-byte Blake2b digest is signed with an Ed25519 private key, producing a 64-byte signature.

### Step 3: MSIG binary format (101 bytes)

The signature is stored as a binary `.sig` file in the MSIG format:

```
┌──────────────────────────────────────┐
│ [0-3]   Magic: 0x4D534947 ("MSIG")  │  4 bytes
│ [4]     Version: 0x01               │  1 byte
│ [5-36]  Public key (Ed25519)        │ 32 bytes
│ [37-100] Signature (Ed25519)        │ 64 bytes
└──────────────────────────────────────┘
                                Total: 101 bytes
```

The public key is embedded in the signature file so the firmware can extract the signer's identity and check it against the trust store.

### Verification

Verification reverses the process:

1. Parse the MSIG binary to extract the public key and signature
2. Strip the `"signature"` field from the manifest JSON
3. Recompute the Blake2b-512 digest over the stripped JSON + files
4. Verify the Ed25519 signature against the digest using the embedded public key
5. Optionally check the public key against a trust store of known developers

### Key files

| File | Size | Description |
|---|---|---|
| `dev.key` | 32 bytes | Ed25519 private key (raw binary, keep secret) |
| `dev.key.pub` | 32 bytes | Ed25519 public key (raw binary, distribute freely) |
| `app.sig` | 101 bytes | MSIG signature file (embedded in `.mapp` after signing) |

### Fingerprints

A key **fingerprint** is the first 8 bytes of the public key rendered as 16 hex characters (e.g., `36d9583dfd284524`). Fingerprints provide a short, human-readable identifier for verifying signer identity.

### Full workflow example

```bash
# 1. Create a new app
mapps create hello --name "Hello Mesh" --author "Dev"

# 2. Edit hello/bui.be and/or hello/mui.be with your Berry code

# 3. Package into .mapp
mapps build hello

# 4. Generate a signing key (once)
mapps keygen dev.key

# 5. Sign the package
mapps sign hello.mapp --key dev.key

# 6. Verify
mapps verify hello.mapp

# 7. Install to a connected device over serial
mapps install hello.mapp

# 8. List installed apps on device
mapps list

# 9. Remove an app from device
mapps uninstall hello
```

## Device Management

The CLI can install, list, and remove apps on a connected Meshtastic device over serial (USB). The serial port is auto-detected on macOS (`/dev/tty.usb*`) and Linux (`/dev/ttyUSB*`, `/dev/ttyACM*`), or can be specified with `--port`.

```bash
# Install a .mapp to the device (uploads files to /apps/<slug>/)
mapps install hello.mapp [--port /dev/ttyXXX] [--reboot]

# List installed apps on the device
mapps list [--port /dev/ttyXXX]

# Remove an app from the device
mapps uninstall hello [--port /dev/ttyXXX] [--reboot]
```

File transfer uses the Meshtastic serial protocol with XModem framing. The `--reboot` flag requests the device reboot after the operation so the firmware discovers the new/removed app.

### SD card installation

You can also install apps by unpacking a `.mapp` directly to an SD card. Extract to `/apps/<slug>/` on the card and insert it into the device — the firmware scans this directory on boot.

```bash
# Unpack to an SD card mounted at /Volumes/MESH_SD
mapps unpack hello.mapp --output /Volumes/MESH_SD/apps/hello
```

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e native
```

The binary is produced at `.pio/build/native/mapps`.

### Testing

```bash
pio test -e native
```

Runs the Unity test suite covering BerryRuntime lifecycle, binding registration, function calls, and `app_state` persistence.
