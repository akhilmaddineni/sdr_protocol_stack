# Plugin Authoring Guide

A practical, end-to-end guide to writing, building, and loading your own protocol decoder. For the theory (dynamic linking, name mangling, security), read `docs/04_plugin_arch.md` first; this page is the cookbook.

## 1. The Contract

A plugin is a dynamic library (`*.so` on Linux, `*.dylib` on macOS) that exports exactly one C-linkage factory:

```cpp
extern "C" sdr::IDecoder* create_decoder();
```

The returned object must implement `include/core/IDecoder.hpp`:

```cpp
class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual std::string get_name() const = 0;
    virtual void accept_samples(const std::complex<float>* data, size_t len) = 0;
};
```

Rules the manager relies on:

| Rule | Why |
|---|---|
| Factory has C linkage | avoids C++ name mangling so `dlsym("create_decoder")` resolves |
| `get_name()` unique per plugin | it is the key in `PluginManager`'s decoder map |
| Destructor virtual | instances are owned and deleted via base pointer |
| `accept_samples()` non-blocking, allocation-light | intended to run in the DSP hot path |
| Never call `load_plugins()`/`unload_all()` from inside a decoder | manager APIs are main-thread-only (deadlock/UAF risk) |

## 2. Minimal Decoder Template

```cpp
// plugins/cw/cw_decoder.cpp — minimal skeleton
#include "core/IDecoder.hpp"
#include <string>
#include <complex>
#include <cstddef>
#include <cmath>

namespace sdr {

class CwDecoder : public IDecoder {
public:
    std::string get_name() const override { return "cw"; }

    void accept_samples(const std::complex<float>* data, size_t len) override {
        // Envelope detection placeholder.
        if (len > 0) {
            float mag = std::abs(data[0]);
            (void)mag; // TODO: envelope -> tone detect -> timing decode
        }
    }
};

} // namespace sdr

extern "C" sdr::IDecoder* create_decoder() {
    return new sdr::CwDecoder();
}
```

Reference implementation: `plugins/adsb/adsb_decoder.cpp` (logs sample counts; PPM demodulation is TODO).

## 3. Building the Library

There is **no CMake target for plugins yet** — compile manually. From the repo root:

```bash
# Linux (.so)
g++ -std=c++20 -shared -fPIC -O2 -I include \
    plugins/cw/cw_decoder.cpp -o plugins/cw/cw_decoder.so

# macOS (.dylib)
c++ -std=c++20 -shared -fPIC -O2 -I include \
    plugins/cw/cw_decoder.cpp -o plugins/cw/cw_decoder.dylib
```

Notes:

- Do not link against the host executable; decoders only need headers.
- `-fPIC` is mandatory for shared libraries.
- For multi-file decoders, list all sources or add a per-plugin `add_library(cw_decoder SHARED ...)` in CMake (roadmap).

## 4. Getting It Loaded

`Orchestrator::start()` scans `"plugins/"` relative to the **current working directory**, not the executable path. Running from the repo root matches the default layout:

```bash
cp tests/mock_data.iq mock_data.iq
./build/src/sdr_main          # PluginManager scans ./plugins/
rm mock_data.iq
```

Failure output you may see (`src/core/PluginManager.cpp`) — both are logged and skipped, never fatal:

```
PluginManager: dlopen failed for plugins/foo.so: <dlerror text>   # unloadable library
PluginManager: dlsym failed for plugins/foo.so                    # no create_decoder export
```

## 5. Lifecycle & Ownership

- `PluginManager::load_plugins()` → `dlopen(RTLD_NOW)` → `dlsym("create_decoder")` → factory call → instance stored as `unique_ptr` keyed by `get_name()`.
- The manager **owns** every decoder; callers of `get_decoder(name)` borrow the pointer and must not delete it.
- Same-name plugins overwrite silently (last scanned wins) — keep names unique.
- Unload order on `unload_all()`/destructor: instances deleted before handles are relevant; keep nothing static inside the plugin that outlives the library.
- Load/unload only from the orchestrator thread; see `docs/04_plugin_arch.md` §6 for threading rationale.

## 6. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `dlsym failed ... create_decoder` | missing `extern "C"` | add C linkage to the factory |
| `Symbol not found` at load (macOS) | undefined symbols, `RTLD_NOW` rejects | compile with all deps, check `-I include` |
| Decoder never loads | wrong extension or directory | `.so`/`.dylib` filename inside `plugins/` under CWD |
| Crash inside decoder | exception escaping `accept_samples` | catch internally; hot path must not throw |

## 7. Security Reminder

Loading arbitrary libraries executes their constructor/factory code with full process privileges. Keep the plugin directory writable only by trusted users; see `docs/04_plugin_arch.md` §7.
