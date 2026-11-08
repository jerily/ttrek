"""Exercise ABI rejection cases without needing foreign-architecture executables."""
from pathlib import Path
import runpy
import unittest

validate = runpy.run_path(str(Path(__file__).resolve().parents[1] / "bin/check-linux-release.py"))["validate"]


class ReleaseABI(unittest.TestCase):
    def reports(self, architecture="amd64"):
        machine, loader = {
            "amd64": ("Advanced Micro Devices X86-64", "/lib64/ld-linux-x86-64.so.2"),
            "arm64": ("AArch64", "/lib/ld-linux-aarch64.so.1"),
        }[architecture]
        return [
            "  Class: ELF64\n  Data: 2's complement, little endian\n  Machine: " + machine,
            "  INTERP 0x00\n      [Requesting program interpreter: " + loader + "]",
            "  0x0000000000000001 (NEEDED) Shared library: [libc.so.6]\n"
            "  0x0000000000000001 (NEEDED) Shared library: [libm.so.6]",
            "  Name: GLIBC_2.28 Flags: none Version: 2\n"
            "  Name: GLIBC_2.9 Flags: none Version: 3",
        ]

    def test_accepts_baseline_on_both_architectures(self):
        for arch in ("amd64", "arm64"):
            self.assertIn("GLIBC_2.28", validate(*self.reports(arch), arch, "glibc"))

    def test_rejects_newer_and_private_glibc_symbols(self):
        for version in ("2.29", "2.35", "2.100", "PRIVATE", "ABI_DT_RELR"):
            reports = self.reports()
            reports[3] += "\n Name: GLIBC_" + version
            with self.subTest(version=version), self.assertRaisesRegex(ValueError, "exceeds"):
                validate(*reports, "amd64", "glibc")

    def test_rejects_wrong_architecture_or_word_size(self):
        for index, old, new in ((0, "X86-64", "80386"), (0, "ELF64", "ELF32"),
                                (0, "little endian", "big endian"),
                                (1, "/lib64/ld-linux-x86-64.so.2", "/build/ld.so")):
            reports = self.reports()
            reports[index] = reports[index].replace(old, new)
            with self.assertRaises(ValueError):
                validate(*reports, "amd64", "glibc")

    def test_rejects_external_runtime_libraries_and_search_paths(self):
        for tag in ("(NEEDED) Shared library: [libstdc++.so.6]",
                    "(NEEDED) Shared library: [libgcc_s.so.1]",
                    "(NEEDED) Shared library: [libssl.so.3]",
                    "(RUNPATH) Library runpath: [/install/lib]",
                    "(RPATH) Library rpath: [/build/lib]"):
            reports = self.reports()
            reports[2] += "\n " + tag
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                validate(*reports, "amd64", "glibc")

    def test_requires_dynamic_glibc_and_version_information(self):
        for index in (1, 2, 3):
            reports = self.reports()
            reports[index] = ""
            with self.assertRaises(ValueError):
                validate(*reports, "amd64", "glibc")

    def test_musl_must_be_static(self):
        for arch in ("amd64", "arm64"):
            reports = self.reports(arch)
            with self.assertRaisesRegex(ValueError, "fully static"):
                validate(*reports, arch, "musl")
            reports[1:] = ["", "", ""]
            self.assertEqual(validate(*reports, arch, "musl"), "fully static musl")
            reports[1] = " INTERP 0x00"
            with self.assertRaises(ValueError):
                validate(*reports, arch, "musl")


if __name__ == "__main__":
    unittest.main()
