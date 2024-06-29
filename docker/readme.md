On Linux:
```
cd ttrek/
docker build . -f docker/Dockerfile.alpine -t ttrek-alpine:latest --no-cache
mkdir dist
docker cp $(docker create ttrek-alpine:latest):/var/ttrek/build/ttrek ./dist/ttrek-alpine
```