
PACKAGE=%s
VERSION=%s
SOURCE_DIR=%s

ARCHIVE_FILE="${PACKAGE}-${VERSION}.archive"
BUILD_DIR="$ROOT_BUILD_DIR/build/${PACKAGE}-${VERSION}"
BUILD_LOG_DIR="$ROOT_BUILD_DIR/logs/${PACKAGE}-${VERSION}"

if [ -z "$SOURCE_DIR" ]; then
    SOURCE_DIR="$ROOT_BUILD_DIR/source/${PACKAGE}-${VERSION}"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
rm -rf "$BUILD_LOG_DIR"
mkdir -p "$BUILD_LOG_DIR"
