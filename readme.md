# ttrek

The package manager for TCL

**Note** that this project is under active development.

# Build

* You need to have [Rust installed on your system](https://www.rust-lang.org/tools/install).
* Run the following command:
```bash
./build-ttrek.sh
```

# Run

The executable binary will be available in the `build` subdirectory of the source directory.

Before invoking `ttrek` commands, please make sure that `ttrek.sh` is running localy. Check [https://github.com/jerily/ttrek.sh](https://github.com/jerily/ttrek.sh) repository for instructions on how to build and run `ttrek.sh`.

You can create a project directory, cd into it and use the executable binary. E.g.:
```bash
mkdir -p ~/my-project
cd ~/my-project
<path to source tree>/build/ttrek help
<path to source tree>/build/ttrek init
<path to source tree>/build/ttrek install tcl
```
