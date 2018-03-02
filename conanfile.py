from conans import ConanFile, tools
import os


def convert_arch(arch):
    ret = {
        "x86": "x86",
        "x86_64": "x64"
    }.get(str(arch))
    return ret


class JScriptConan(ConanFile):
    name = "jscript"
    version = "6.10.1.3"
    license = "Node.js https://raw.githubusercontent.com/nodejs/node/master/LICENSE"
    description = "Odant Jscript"
    url = "https://github.com/odant/conan-jscript"
    settings = {
        "os": ["Windows", "Linux"],
        "compiler": ["Visual Studio", "gcc"],
        "build_type": ["Debug", "Release"],
        "arch": ["x86_64", "x86"]
    }
    exports_sources = "src/*"
    no_copy_source = False
    build_policy = "missing"
    short_paths = True

#    def build_requirements(self):
#        self.build_requires("ninja_installer/[~=1.8.2]@bincrafters/stable")

    def build(self):
        flags = [
            #"--ninja",
            "--shared"
        ]
        arch = convert_arch(self.settings.arch)
        flags.append("--dest-cpu=%s" % arch)
        if self.settings.build_type == "Debug":
            flags.append("--debug")
        env = {}
        if self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
            env = tools.vcvars_dict(self.settings)
            env["GYP_MSVS_VERSION"] = {
                "14": "2015",
                "15": "2017"
            }.get(str(self.settings.compiler.version))
        self.run("python --version")
        with tools.chdir("src"), tools.environment_append(env):
            self.run("set")
            self.run("python configure %s" % " ".join(flags))
            build_type = str(self.settings.build_type)
            self.run("msbuild node.sln /m /t:Build /p:Configuration=%s /p:Platform=%s" % (build_type, arch))
            
    def package(self):
        self.copy("*jscript.h", dst="include", keep_path=False)
        src_headers = os.path.join(self.build_folder, "src", "deps", "v8", "include")
        self.copy("*.h", src=src_headers, dst="include", keep_path=True)
        self.copy("*node.lib", dst="lib", keep_path=False)
        self.copy("*node.dll", dst="bin", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if self.settings.os == "Windows":
            self.cpp_info.defines = ["USE_JSCRIPT"]
