# murmur-notif-webhook

### Requirements

* libsystemd
* libcurl
* c compiler, pkgconf, meson and ninja to build

### Build instructions

```sh
meson build
ninja -C build
```

### Usage

Configured by environment variable:

`MNW_USE_SYSTEM=1` to connect to Murmur with system bus instead of session bus.

`MNW_WEBHOOK_DISPLAYNAME` to change webhook display name if wanted.

`MNW_WEBHOOK`: webhook URL, required.


