<!-- markdownlint-disable MD033 -->

# narnia

Terminal utility built with ncurses for looking up Nix store paths in binary
caches and viewing their NAR info metadata.

## Features

- Very small binary (< 30kb)
- Interactive TUI for browsing narinfo contents
- Support for multiple binary caches
- Direct executable lookup without prompts
- Clipboard integration for copying narinfo lines

## Usage

```bash
# Interactive mode
narnia

# Direct lookup
narnia /nix/store/path/to/executable # absolute path
narnia git # from PATH

# With additional caches
narnia -c https://mycache.cachix.org git
narnia -c https://cache1.example.com -c https://cache2.example.com
```

### Options

```plaintext
-c, --cache URL    Add cache URL (can be used multiple times)
-h, --help         Show help message
```

### Controls

Narnia provides a set of keyboard controls while you are in the interactive TUI.

- <kbd>Tab</kbd>/<kbd>Shift-Tab</kbd>: Switch between cache results
- <kbd>Up</kbd>/<kbd>Down</kbd>: Navigate narinfo lines
- <kbd>PgUp</kbd>/<kbd>PgDn</kbd>: Jump pages
- <kbd>Enter</kbd>: Copy selected line to clipboard
- <kbd>r</kbd>: Retry with new input
- <kbd>q</kbd>: Quit

### Cache Resolution

The tool automatically uses <https://cache.nixos.org> as the default cache.
Additional caches can be specified with the `-c` flag and will be queried in the
order provided.

For executables found in `PATH`, the tool resolves the full Nix store path,
extracts the hash, and queries each cache for the corresponding nar info file.

## Building

### Dependencies

This program depends on **curl** for network requests and **ncurses** for the
terminal UI. Narnia _used_ to depend on libclipboard, but it has proven
ineffective on Wayland systems and has been replaced with a local implementation
for cross-protocol copy feature.

- `libcurl`
- `ncurses`

Once you confirm that you have acquired the relevant dependencies, build with
Zig.

```bash
zig build -Doptimize=ReleaseSmall
```

This will automatically optimize the binary for release, with a focus on smaller
binary sizes. You may experiment with release modes if you'd like, e.g., a
"faster" binary but the speed doesn't seem to change much.

You can, of course, choose to build with `gcc` if that is what you prefer.

```bash
gcc -o narnia main.c -lcurl -lncurses -lclipboard
```

## Installing

The recommended way of using and installing Narnia is through the
[Nix build tool](https://nixos.org). This much should be obvious since it is,
well, to query Nix Archives (NARs). In most cases you are recommended to use one
of

1. `nix profile add`
2. `environment.systemPackages`

using the [flake](./flake.nix) provided by this repository in order to make the
`narnia` command available in your system. In the case you want to install this
tool through traditional means, you may run:

```bash
zig build install --prefix /usr/local
```
