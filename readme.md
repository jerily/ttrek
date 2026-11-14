# ttrek

ttrek is a package manager for Tcl. Think of packages as building blocks for your
app: tell ttrek which ones you need, and it downloads and builds them, along with
any other pieces they need to work.

Packages stay in your project's folder, so different projects can use different
versions. ttrek also helps you update packages and run the tools you install.

This project is under active development.

## Get started

Visit [ttrek.sh](https://ttrek.sh) to browse packages and download ttrek.

Follow the [installation guide](docs/install.md) to install ttrek and its build
tools. Then create a project and add a package:

```sh
mkdir my-app
cd my-app
ttrek init -y
ttrek install -y tjson
```

`tjson` gives Tcl support for reading and writing JSON. Use `ttrek update` later
to update your project's packages.

## Learn more

- [Usage guide](docs/usage.md): find, install and use packages.
- Try a complete example: [a personal site](docs/example/readme.md), [OpenACS](docs/openacs/readme.md), or [Tk's widget demos](docs/tk/readme.md).
- [All documentation](docs/readme.md): registries, scripts, building ttrek and more.
