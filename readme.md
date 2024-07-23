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

# Docker
```bash
docker build . -t ttrek:latest
# On Linux
docker run --network host ttrek:latest
docker run -p 8080:8080 --entrypoint sh -it ttrek:latest
docker run -p 8080:8080 -v $(pwd):/ttrek -it ttrek:latest
```

```bash
mkdir build
cd build

curl -L -O https://sourceforge.net/projects/tcl/files/Tcl/8.6.14/tcl8.6.14-src.tar.gz
tar -xzvf tcl8.6.14-src.tar.gz
cd tcl8.6.14/unix
./configure --prefix=$(pwd)/../../static-tcl8.6.14 --disable-shared
make
make install
cd ../../
cmake .. \
  -DTCL_LIBRARY=$(pwd)/static-tcl8.6.14/lib/libtcl8.6.a \
  -DTCL_INCLUDE_DIR=$(pwd)/static-tcl8.6.14/include
```
