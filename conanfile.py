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
    version = "6.10.1.4"
    license = "Node.js https://raw.githubusercontent.com/nodejs/node/master/LICENSE"
    description = "Odant Jscript"
    url = "https://github.com/odant/conan-jscript"
    settings = {
        "os": ["Windows", "Linux"],
        "compiler": ["Visual Studio", "gcc"],
        "build_type": ["Debug", "Release"],
        "arch": ["x86_64", "x86"]
    }
    options = {
        "dll_sign": [False, True]
    }
    default_options = "dll_sign=True"
    exports_sources = "src/*"
    no_copy_source = False
    build_policy = "missing"
    short_paths = True

    def configure(self):
        # Only C++11
        if self.settings.compiler.get_safe("libcxx") == "libstdc++":
            raise ConanException("This package is only compatible with libstdc++11")
        # DLL sign, only Windows
        if self.settings.os != "Windows":
            del self.options.dll_sign

    def build_requirements(self):
#        self.build_requires("ninja_installer/[~=1.8.2]@bincrafters/stable")
        if get_safe(self.options, "dll_sign"):
            self.build_requires("windows_signtool/[~=1.0]@%s/stable" % self.user)

    def build(self):
        output_name = "jscript"
        if self.settings.os == "Windows":
            output_name += {
                            "x86": "32",
                            "x86_64": "64"
                        }.get(str(self.settings.arch))
            if self.settings.build_type == "Debug":
                output_name += "d"
        #
        flags = [
            #"--ninja",
            "--shared",
            "--dest-os=%s" % {
                                "Windows": "win",
                                "Linux": "linux"
                            }.get(str(self.settings.os)),
            "--dest-cpu=%s" % {
                                "x86": "ia32",
                                "x86_64": "x64"
                            }.get(str(self.settings.arch)),
            "--node_core_target_name=%s" % output_name
        ]
        #
        if self.settings.build_type == "Debug":
            flags.append("--debug")
        # Run build
        env = {}
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            env = tools.vcvars_dict(self.settings)
            env["GYP_MSVS_VERSION"] = {
                                        "14": "2015",
                                        "15": "2017"
                                    }.get(str(self.settings.compiler.version))
        self.run("python --version")
        with tools.chdir("src"), tools.environment_append(env):
            self.run("python configure %s" % " ".join(flags))
            if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
                build_type = str(self.settings.build_type)
                arch = {
                        "x86": "Win32",
                        "x86_64": "x64"
                    }.get(str(self.settings.arch))
                self.run("msbuild node.sln /m /t:Build /p:Configuration=%s /p:Platform=%s" % (build_type, arch))
            else:
                self.run("make -j %s" % tools.cpu_count())

    def package(self):
        # Headers
        self.copy("*jscript.h", dst="include", keep_path=False)
        self.copy("*node.h", dst="include", keep_path=False)
        self.copy("*node_version.h", dst="include", keep_path=False)
        #self.copy("*node_internals.h", dst="include", keep_path=False)
        src_v8_headers = os.path.join(self.build_folder, "src", "deps", "v8", "include")
        self.copy("*.h", src=src_v8_headers, dst="include", keep_path=True)
        # Libraries
        self.copy("*jscript*.lib", dst="lib", keep_path=False)
        self.copy("*jscript*.dll", dst="bin", keep_path=False)
        if self.settings.os == "Linux":
            src_lib_folder = os.path.join(self.build_folder, "src", "out", str(self.settings.build_type), "lib.target")
            self.copy("*jscript.so.*", src=src_lib_folder, dst="lib", keep_path=False)
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
        self.copy("*node.pdb", dst="bin", keep_path=False)
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
