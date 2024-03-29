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
```