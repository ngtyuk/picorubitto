# PicoRubitto

[English](README.md) | [日本語](README.ja.md)

A game platform powered by [PicoRuby](https://github.com/picoruby/picoruby).
Write games in Ruby using the same `Game` API on RP2350 hardware and in a web
browser through WebAssembly.

The project includes a game engine, firmware, a browser emulator, and Browser
Studio for editing Ruby, creating pixel art, and exporting games to a microSD
card. PicoRuby itself is built from a separate dependency checkout.

## Try PicoRubitto Studio

[Open PicoRubitto Studio](https://picorubitto.ngt-y.uk/) to create and run games in your browser.

- 320×240 RGB565 graphics, sprites, textures, and bitmap fonts
- Directional buttons, A/B, L/R, and START/SELECT
- Low-poly 3D with depth testing, WAV playback with up to four voices, saves,
  and rectangle collision detection
- Supported boards: Pimoroni Pico Plus 2 and WeAct RP2350B Core Board with 8 MB PSRAM

## Set up from scratch

The commands below use a macOS or Linux shell. Run project commands from the
repository root. Internet access is needed to download tools and dependencies.
Browser demos do not require a board, wiring, or a microSD card.

### 1. Install the host tools

You need Git, **Ruby 3.1 or newer**, Rake, a host C/C++ compiler, Make, CMake,
and Python 3. Python serves the browser pages; Ruby runs the build tools.

On macOS, install the Xcode Command Line Tools, then use
[Homebrew](https://brew.sh/) (install it first if needed):

```sh
xcode-select --install
```

After the installer finishes (skip it if the tools are already installed):

```sh
brew install git ruby@3.4 cmake python
export PATH="$(brew --prefix ruby@3.4)/bin:$PATH"
gem install rake
```

On Ubuntu 24.04, a starting point is:

```sh
sudo apt-get update
sudo apt-get install git ruby-full rake build-essential cmake python3
```

Check the selected tools before continuing:

```sh
ruby --version
ruby -S rake --version
cc --version
c++ --version
make --version
cmake --version
python3 --version
```

If Ruby is older than 3.1, select a newer Ruby with your version manager or follow
[the Ruby installation guide](https://www.ruby-lang.org/en/documentation/installation/).
The macOS system Ruby may be too old. The Homebrew PATH setting above applies to
this terminal; add it to your shell configuration if you want to keep using it.

### 2. Get the repository and pinned dependencies

```sh
git clone https://github.com/ngtyuk/picorubitto.git
cd picorubitto
ruby tools/build/workflow.rb setup
ruby tools/build/workflow.rb doctor
```

`setup` downloads PicoRuby **4.0.5**, its Funicular **0.5.2** submodule, Pico SDK
**2.3.1**, and the pinned FatFs SD port into `deps/`. It also initializes the
required submodules. You do not need to clone PicoRuby separately.

`doctor` reports dependency revisions; check that PicoRuby, Pico SDK, and FatFs
show `locked`. It does **not** check installed compilers or all host tools.
Versions and commits are recorded in
[`tools/build/dependencies.lock.json`](tools/build/dependencies.lock.json).

By default, builds use `deps/picoruby`. If your shell already sets `PICORUBY_ROOT`,
it overrides that default. To intentionally use an existing checkout, set
`export PICORUBY_ROOT=/absolute/path/to/picoruby`; `setup` does not update that
external checkout. See [dependency management](docs/dependencies.md) (Japanese).

### 3. Install Emscripten for the browser runtime

Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
outside this repository. The example uses **6.0.8**, the version present in the
current development environment; emsdk is not managed by the dependency lock.
If you already have emsdk, use its directory instead of cloning it again.

```sh
git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
"$HOME/emsdk/emsdk" install 6.0.8
"$HOME/emsdk/emsdk" activate 6.0.8
source "$HOME/emsdk/emsdk_env.sh"
export EM_NODE_JS="$EMSDK_NODE"
emcc --version
```

Run `source` and the `EM_NODE_JS` export again in each new terminal used to build
WASM. Keep using the same terminal for the following steps.

### 4. Build the engine

Choose one of the following paths.

**Browser only:** build Studio, which also builds the emulator runtime. An Arm
cross-compiler is not needed for this path.

```sh
ruby tools/build/workflow.rb studio
```

This generates `emulator/picoruby/` and the browser compiler in `studio/runtime/`.
For just the emulator, use `ruby tools/build/workflow.rb engine --wasm-only`;
that command does not build the Studio compiler or the default host ROM compiler.

**Hardware and browser:** install an
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
package for your host OS and CPU, with target **`arm-none-eabi`** (AArch32 bare-metal).
The current development environment uses **15.2.Rel1**. Set `ARM_TOOLCHAIN` to its
`bin` directory, not to the compiler executable. Replace the example path:

```sh
export ARM_TOOLCHAIN=/absolute/path/to/arm-none-eabi/bin
"$ARM_TOOLCHAIN/arm-none-eabi-gcc" --version
ruby tools/build/workflow.rb engine
```

The default board is Pimoroni Pico Plus 2. This produces:

| Output | Location |
| --- | --- |
| Firmware | `build/firmware-release/pico_ruby_gaming.uf2` |
| ELF / map | `build/firmware-release/pico_ruby_gaming.elf` / `.map` |
| Host ROM compiler | `build/mruby/mrbc/default/bin/mrbc` |
| Browser runtime | `emulator/picoruby/` |

For WeAct RP2350B Core Board with 8 MB PSRAM, run:

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core
```

Its UF2 is written to
`build/firmware-release-weact-studio-rp2350b-core/pico_ruby_gaming.uf2`.
To use Studio after either hardware build, also run
`ruby tools/build/workflow.rb studio`.

### 5. Run the browser demos

Start a server from the repository root and leave it running:

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

| Open in your browser | What to expect |
| --- | --- |
| [2D demo](http://localhost:8080/emulator/2d-example.html) | Graphics, input, sprites, and audio |
| [3D demo](http://localhost:8080/emulator/3d-example.html) | A wireframe flight game |
| [API contract test](http://localhost:8080/emulator/contract.html) | Green screen on success; red on failure |
| [Browser Studio](http://localhost:8080/studio/) | Ruby and pixel-art editors; requires the Studio build |

These demos work without `local-games/` or a separate game repository. Use HTTP;
opening the HTML files with `file://` does not work. Use a current Chrome, Edge,
or Firefox browser. Arrow keys move, `Z` is A, `X` is B, `P` is START, and `O` is
SELECT. Audio starts after a key press or an on-screen button click. See the
[sample guide](examples/README.md) for game-specific controls (Japanese).

### 6. Run a sample on hardware

You need a supported board, a wired ST7789V display, microSD interface, buttons,
and the audio circuit if you want sound. The boards alone do not provide these
peripherals. Follow the [wiring guide](docs/hardware.md) (Japanese) first.

After the hardware build, generate the sample ROM using the matching compiler:

```sh
ruby tools/build/workflow.rb game examples/games/2d
```

1. Connect the board over USB in BOOTSEL mode and copy its matching UF2 to the
   mounted drive.
2. Copy the **contents** of `build/games/2d/` to the root of a FAT32 microSD card.
3. Insert the card and restart the board.

The card should contain `game.mrb` and `assets/` directly at its root.
For the 3D sample, use `ruby tools/build/workflow.rb game examples/games/3d` and
copy the contents of `build/games/3d/` instead. Alternatively, Studio can export
an SD ZIP containing the compiled game and assets. Firmware is installed once;
changing games only requires replacing the ROM and assets.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Ruby version error / Rake missing | Check `ruby --version` and `ruby -S rake --version`; install Rake with the selected Ruby. |
| `doctor` says `not installed` or a revision is not locked | Run `setup`; it stops if a dependency checkout has local changes. Keep those changes and resolve the checkout separately. |
| Emscripten not found | Source your `emsdk_env.sh`; check `EMSDK`, `EMSDK_NODE`, and `EM_NODE_JS` in the build terminal. |
| Arm toolchain not found | Set `ARM_TOOLCHAIN` to the directory containing `arm-none-eabi-gcc`, `arm-none-eabi-ld`, and `arm-none-eabi-ar`. |
| Browser runtime / Studio compiler returns 404 | Build the corresponding runtime, and start the HTTP server from the repository root. |
| Emulator game list is empty | Open a bundled demo first; the main selector lists games in your local `local-games/` directory. |
| Hardware does not start | Check the board-specific UF2, wiring, FAT32 card, root-level ROM/assets, and USB serial boot log. |

## Documentation and development

The detailed guides below are currently in Japanese.

| Task | Guide |
| --- | --- |
| Setup details | [Getting started](docs/getting-started.md) |
| Write a game / build a ROM | [Game development](docs/game-development.md) |
| Edit in the browser | [Browser Studio](studio/README.md) |
| Wire and flash hardware | [Hardware](docs/hardware.md) |
| Check hardware operation | [Integrated test ROM](docs/compatibility-testing.md#実機の統合テストrom) |
| Use the API | [API reference](docs/api-reference.md) |
| Change the engine | [Development guide](docs/agent-development.md) |
| Architecture, compatibility, and performance | [Documentation index](docs/README.md) |

`src/`, `include/`, and `mrbgems/` contain the native backend and gems;
`emulator/` contains the WASM adapter and emulator; `studio/` and `web/` contain
the browser tools. Tests and build tools live in `tests/` and `tools/`.

Engine or shared API changes require **both UF2 and WASM builds**, the shared
contract tests, and corresponding documentation updates. See
[AGENTS.md](AGENTS.md) for the full contribution workflow.

Generated files in `build/`, `deps/`, `emulator/picoruby/`, and `studio/runtime/`
are not committed. Keep personal games in ignored `local-games/` or separate
repositories. The files in `examples/` are shared API samples.

The built-in ASCII, kana, and kanji glyphs use MaruMinya 1.500. See the [font license](tools/fonts/MARUMINYA-OFL.txt).

## License

This project is licensed under the [MIT License](LICENSE). Third-party components
have their own licenses, including [MaruMinya](tools/fonts/MARUMINYA-OFL.txt)
and [Prism](studio/vendor/prism/LICENSE).
