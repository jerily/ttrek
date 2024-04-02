```bash
chmod +x install.sh start.sh
./install.sh
./start.sh
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

curl -L -O https://sourceforge.net/projects/tcl/files/Tcl/8.6.13/tcl8.6.13-src.tar.gz
tar -xzvf tcl8.6.13-src.tar.gz
cd tcl8.6.13/unix
./configure --prefix=$(pwd)/../../static-tcl8.6.13 --disable-shared
make
make install
cd ../../
cmake .. \
  -DTCL_LIBRARY=$(pwd)/static-tcl8.6.13/lib/libtcl8.6.a \
  -DTCL_INCLUDE_DIR=$(pwd)/static-tcl8.6.13/include
```