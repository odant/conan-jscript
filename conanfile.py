# jscript Conan package manager
# Dmitriy Vetutnev, Odant, 2018


from conans import ConanFile, tools
from conans.errors import ConanException
import os, glob


def get_safe(options, name):
    try:
        return getattr(options, name, None)
    except ConanException:
        return None


class JScriptConan(ConanFile):
    name = "jscript"
    version = "11.12.0.0"
    license = "Node.js https://raw.githubusercontent.com/nodejs/node/master/LICENSE"
    description = "Odant Jscript"
    url = "https://github.com/odant/conan-jscript"
    settings = {
        "os": ["Windows", "Linux"],
        "compiler": ["Visual Studio", "gcc"],
        "build_type": ["Debug", "Release"],
        "arch": ["x86_64", "x86", "mips"]
    }
    options = {
        "dll_sign": [False, True]
    }
    default_options = "dll_sign=True"
    exports_sources = "src/*", "build.patch", "source.patch", "FindJScript.cmake"
    no_copy_source = False
    build_policy = "missing"
    short_paths = True
    #
    _openssl_version = "1.1.0h"

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
        # DLL sign, only Windows
        if self.settings.os != "Windows":
            del self.options.dll_sign

    def requirements(self):
        self.requires("openssl/%s@%s/stable" % (self._openssl_version, self.user))
        
    def build_requirements(self):
        self.build_requires("ninja_installer/1.9.0@bincrafters/stable")
        if self.settings.os == "Windows":
            self.build_requires("nasm/2.13.01@conan/stable")
        if get_safe(self.options, "dll_sign"):
            self.build_requires("windows_signtool/[~=1.0]@%s/stable" % self.user)

    def source(self):
        tools.patch(patch_file="build.patch")
        return
        tools.patch(patch_file="source.patch")

    def build(self):
        output_name = "jscript"
        if self.settings.os == "Windows":
            output_name += {
                            "x86": "",
                            "x86_64": "64"
                        }.get(str(self.settings.arch))
            if self.settings.build_type == "Debug":
                output_name += "d"
        #
        flags = [
            "--ninja",
            "--verbose",
            "--shared",
            "--dest-os=%s" % {
                                "Windows": "win",
                                "Linux": "linux"
                            }.get(str(self.settings.os)),
            "--dest-cpu=%s" % {
                                "x86": "ia32",
                                "x86_64": "x64",
                                "mips": "mipsel"
                            }.get(str(self.settings.arch)),
            "--node_core_target_name=%s" % output_name
        ]
        openssl_includes = self.deps_cpp_info["openssl"].include_paths[0].replace("\\", "/")
        openssl_lib_path = self.deps_cpp_info["openssl"].lib_paths[0].replace("\\", "/")
        openssl_libs = []
        for l in ["libcrypto", "libssl"]:
            if self.settings.os == "Windows":
                l = os.path.join(openssl_lib_path, l).replace("\\", "/") + ".lib"
            else:
                l = os.path.join(openssl_lib_path, l) + ".so.1.1"
            openssl_libs.append(l)
        flags.extend([
            "--shared-openssl",
            "--shared-openssl-includes=%s" % openssl_includes,
        ])
        if self.settings.os == "Windows":
            flags.append("--shared-openssl-libname=%s" % ",".join(openssl_libs))
        else:
            flags.append("--shared-openssl-libpath=%s" % openssl_lib_path)
        #
        if self.settings.build_type == "Debug":
            flags.append("--debug")
            if self.settings.os == "Linux":
                flags.append("--gdb")
        # Run build
        env = {}
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            env = tools.vcvars_dict(self.settings, force=True)
            env["GYP_MSVS_VERSION"] = "2017"
            env["PLATFORM_TOOLSET"] = "v141"
            # Use external ninja
            ninjaDir = self.deps_cpp_info["ninja_installer"].bin_paths[0].replace("\\", "/")
            env["PATH"].insert(0, ninjaDir)
        with tools.chdir("src"), tools.environment_append(env):
            self.run("python --version")
            self.run("ninja --version")
            #
            self.run("python configure %s" % " ".join(flags))
            self.run("ninja -w dupbuild=warn -d keeprsp --verbose -C out/%s" % str(self.settings.build_type))

    def package(self):
        # CMake script
        self.copy("FindJScript.cmake", dst=".", src=".", keep_path=False)
        # Headers
        self.copy("*oda/jscript.h", dst="include/oda", keep_path=False)
        self.copy("*node.h", dst="include", keep_path=False)
        self.copy("*node_version.h", dst="include", keep_path=False)
        #self.copy("*node_internals.h", dst="include", keep_path=False)
        src_v8_headers = os.path.join(self.build_folder, "src", "deps", "v8", "include")
        self.copy("*.h", src=src_v8_headers, dst="include", keep_path=True)
        # Libraries
        self.copy("*jscript.dll.lib", dst="lib", keep_path=False)
        self.copy("*jscript.dll", dst="bin", keep_path=False)
        self.copy("*jscriptd.dll.lib", dst="lib", keep_path=False)
        self.copy("*jscriptd.dll", dst="bin", keep_path=False)
        self.copy("*jscript64.dll.lib", dst="lib", keep_path=False)
        self.copy("*jscript64.dll", dst="bin", keep_path=False)
        self.copy("*jscript64d.dll.lib", dst="lib", keep_path=False)
        self.copy("*jscript64d.dll", dst="bin", keep_path=False)
        if self.settings.os == "Linux":
            src_lib_folder = os.path.join(self.build_folder, "src", "out", str(self.settings.build_type), "lib")
            self.copy("libjscript.so.*", src=src_lib_folder, dst="lib", keep_path=False, excludes="*.TOC")
            # Symlink
            lib_folder = os.path.join(self.package_folder, "lib")
            if not os.path.isdir(lib_folder):
                return
            with tools.chdir(lib_folder):
                for fname in os.listdir("."):
                    extension = ".so"
                    symlink = fname[0:fname.rfind(extension) + len(extension)]
                    self.run("ln -s \"%s\" \"%s\"" % (fname, symlink))
        # PDB
        self.copy("*jscript.dll.pdb", dst="bin", keep_path=False)
        self.copy("*jscriptd.dll.pdb", dst="bin", keep_path=False)
        self.copy("*jscript64.dll.pdb", dst="bin", keep_path=False)
        self.copy("*jscript64d.dll.pdb", dst="bin", keep_path=False)
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

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.defines = ["USE_JSCRIPT"]
