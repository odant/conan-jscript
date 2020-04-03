# jscript Conan package
# Dmitriy Vetutnev, Odant, 2018-2020


from conans import ConanFile, tools, MSBuild
from conans.errors import ConanException
import os, glob


def get_safe(options, name):
    try:
        return getattr(options, name, None)
    except ConanException:
        return None


class JScriptConan(ConanFile):
    name = "jscript"
    version = "11.13.0.12"
    license = "Node.js https://raw.githubusercontent.com/nodejs/node/master/LICENSE"
    description = "Odant Jscript"
    url = "https://github.com/odant/conan-jscript"
    settings = {
        "os": ["Windows", "Linux"],
        "compiler": ["Visual Studio", "gcc"],
        "build_type": ["Debug", "Release"],
        "arch": ["x86_64", "x86", "mips", "armv7"]
    }
    options = {
        "dll_sign": [False, True],
        "ninja": [False, True],
        "with_unit_tests": [False, True]
    }
    default_options = "dll_sign=True", "ninja=True", "with_unit_tests=False"
    exports_sources = "src/*", "oda.patch", "add_const.patch", "FindJScript.cmake", "add_libatomic.patch"
    no_copy_source = False
    build_policy = "missing"
    short_paths = True
    #
    _openssl_version = "1.1.0l+2"

    def configure(self):
        if self.settings.os == "Windows":
            # Only MSVC 15
            if self.settings.compiler.get_safe("version") < "15":
                raise Exception("This package is only compatible with Visual Studio 15 2017")
            # Disable Windows XP support
            if not self.settings.compiler.get_safe("toolset") in [None, "'v141'"]:
                raise Exception("This package is compatible with compiler toolset None or v141")
        # Only C++11
        if self.settings.compiler.get_safe("libcxx") == "libstdc++":
            raise Exception("This package is only compatible with libstdc++11")
        # DLL sign, only on Windows
        if self.settings.os != "Windows":
            del self.options.dll_sign

    def requirements(self):
        self.requires("openssl/%s@%s/stable" % (self._openssl_version, self.user))

    def build_requirements(self):
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            self.build_requires("nasm/2.13.01@conan/stable")
        if self.settings.arch == "x86_64" or self.settings.arch == "x86":
            if self.options.ninja:
                self.build_requires("ninja_installer/1.9.0@bincrafters/stable")
        if get_safe(self.options, "dll_sign"):
            self.build_requires("windows_signtool/[~=1.1]@%s/stable" % self.user)

    def source(self):
        tools.patch(patch_file="oda.patch")
        tools.patch(patch_file="add_const.patch")
        if self.settings.arch == "mips" or self.settings.arch == "armv7":
            tools.patch(patch_file="add_libatomic.patch")

    def build(self):
        output_name = "jscript"
        if self.settings.os == "Windows":
            if self.settings.build_type == "Debug":
                output_name += "d"
        #
        flags = [
            "--verbose",
            "--shared",
            "--dest-os=%s" % {
                                "Windows": "win",
                                "Linux": "linux"
                            }.get(str(self.settings.os)),
            "--dest-cpu=%s" % {
                                "x86": "ia32",
                                "x86_64": "x64",
                                "mips": "mipsel",
                                "armv7": "arm"
                            }.get(str(self.settings.arch)),
            "--node_core_target_name=%s" % output_name
        ]
        # !!! Without PCH build faild with MSBuild
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            flags.append("--with-pch")
        # External OpenSSL
        openssl_includes = self.deps_cpp_info["openssl"].include_paths[0].replace("\\", "/")
        openssl_libpath = self.deps_cpp_info["openssl"].lib_paths[0].replace("\\", "/")
        flags.extend([
            "--shared-openssl",
            "--shared-openssl-includes=%s" % openssl_includes,
            "--shared-openssl-libpath=%s" % openssl_libpath
        ])
        if self.settings.os == "Windows":
            flags.append("--shared-openssl-libname=libcrypto.lib,libssl.lib")
        # Build type, debug/release
        if self.settings.build_type == "Debug":
            flags.append("--debug")
            if self.settings.os == "Linux":
                flags.append("--gdb")
        #
        if self.options.ninja:
            flags.append("--ninja")
        #
        env = {}
        if self.settings.os == "Linux":
            env["LD_LIBRARY_PATH"] = openssl_libpath
            ld_env_path = os.environ.get("LD_LIBRARY_PATH")
            if ld_env_path is not None:
                env["LD_LIBRARY_PATH"] += ":" + ld_env_path
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            env = tools.vcvars_dict(self.settings, force=True)
            if not "GYP_MSVS_VERSION" in os.environ:
                env["GYP_MSVS_VERSION"] = "2017"
                env["PLATFORM_TOOLSET"] = "v141"
            # Explicit use external Ninja
            if self.options.ninja:
                ninja_binpath = self.deps_cpp_info["ninja_installer"].bin_paths[0].replace("\\", "/")
                env["PATH"].insert(0, ninja_binpath)
            # OpenSSL DLL in PATH for run tests
            if self.options.with_unit_tests:
                openssl_binpath = self.deps_cpp_info["openssl"].bin_paths[0].replace("\\", "/")
                env["PATH"].insert(0, openssl_binpath)
        if self.settings.compiler == "gcc":
            env["CFLAGS"] = "-Wno-unused-but-set-parameter"
            env["CXXFLAGS"] = "-Wno-unused-but-set-parameter"
        # Run build
        with tools.chdir("src"), tools.environment_append(env):
            self.run("python --version")
            #
            self.run("python configure %s" % " ".join(flags))
            if self.options.ninja:
                self.run("ninja --version")
                self.run("ninja -C out/%s" % str(self.settings.build_type))
            elif self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
                msbuild = MSBuild(self)
                msbuild.build("node.sln", targets=["Build"], upgrade_project=False, verbosity="normal", use_env=False)
            else:
                self.run("make -j %s" % tools.cpu_count())
            # Tests
            if self.options.with_unit_tests:
                shell = str(self.settings.build_type) + "/" + output_name
                if self.options.ninja:
                    shell = "out/" + shell
                if self.settings.os == "Windows":
                    shell += ".exe"
                self.run("python tools/test.py --shell=%s --progress=color --time --report -j %s" % (shell, tools.cpu_count()))

    def package(self):
        # CMake script
        self.copy("FindJScript.cmake", dst=".", src=".", keep_path=False)
        # Headers
        self.copy("jscript.h", dst="include/oda", src="src/oda", keep_path=False)
        self.copy("node.h", dst="include/oda", src="src/src", keep_path=False)
        self.copy("node_version.h", dst="include/oda", src="src/src", keep_path=False)
        self.copy("*.h", dst="include/oda", src="src/deps/v8/include", keep_path=True)
        # Libraries
        output_folder = "src"
        if self.options.ninja:
            output_folder += "/out"
        output_folder += "/%s" % str(self.settings.build_type)
        if self.settings.os == "Windows":
            self.copy("jscript.dll.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscript.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscript.dll", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscriptd.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll", dst="bin", src=output_folder, keep_path=False)
            # PDB
            self.copy("jscript.dll.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscript.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscriptd.pdb", dst="bin", src=output_folder, keep_path=False)
        if self.settings.os == "Linux":
            output_folder += "/lib"
            self.copy("libjscript.so.*", dst="lib", src=output_folder, keep_path=False, excludes="*.TOC")
            # Symlink
            lib_folder = os.path.join(self.package_folder, "lib")
            if not os.path.isdir(lib_folder):
                return
            with tools.chdir(lib_folder):
                for fname in os.listdir("."):
                    extension = ".so"
                    symlink = fname[0:fname.rfind(extension) + len(extension)]
                    self.run("ln -s \"%s\" \"%s\"" % (fname, symlink))
        # Local build
        if not self.in_local_cache:
            self.copy("conanfile.py", dst=".", keep_path=False)
        # Sign DLL
        if get_safe(self.options, "dll_sign"):
            import windows_signtool
            pattern = os.path.join(self.package_folder, "bin", "*.dll")
            for fpath in glob.glob(pattern):
                fpath = fpath.replace("\\", "/")
                for alg in ["sha1", "sha256"]:
                    is_timestamp = True if self.settings.build_type == "Release" else False
                    cmd = windows_signtool.get_sign_command(fpath, digest_algorithm=alg, timestamp=is_timestamp)
                    self.output.info("Sign %s" % fpath)
                    self.run(cmd)

    def package_id(self):
        self.info.options.ninja = "any"

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.defines = ["USE_JSCRIPT"]
