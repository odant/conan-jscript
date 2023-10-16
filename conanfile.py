# jscript Conan package
# Dmitriy Vetutnev, Odant, 2018-2021


from conans import ConanFile, tools, MSBuild, CMake
import os, glob, re


class JScriptConan(ConanFile):
    name = "jscript"
    version = "18.18.2.0"
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
        "cmake": [False, True],
        "with_unit_tests": [False, True],
        "disable_v8_slow_dcheck": [False, True]
    }
    default_options = {
        "dll_sign": True,
        "ninja": False,
        "cmake": False,
        "with_unit_tests": False,
        "disable_v8_slow_dcheck": True
    }
    exports_patches = [
        "oda.patch",
        "experimental.patch",
        "use_nodepath_for_esm.patch",
        "add_v8_options.patch"
    ]
    exports_sources = [
        "src/*",
        "FindJScript.cmake",
        "win_delay_load_hook.cc",
        *exports_patches,
        "fix_vs2022_build.patch",
        "fix_no_optimization_build.patch",
        "disable_v8_slow_dcheck.patch",
        "disable_gen_node_def.patch",
        "fix_deps_undici.patch"
    ]
    no_copy_source = False
    build_policy = "missing"
    short_paths = True
    #
    _openssl_version = "3.0.8+0"
    _openssl_channel = "stable"
    _zlib_version = "1.2.12+1"
    _zlib_channel = "stable"

    def configure(self):
        if self.settings.os != "Windows":
            del self.options.dll_sign
            self.options.ninja = True
        if self.settings.build_type != "Debug":
            del self.options.disable_v8_slow_dcheck

    def requirements(self):
        self.requires("openssl/%s@%s/%s" % (self._openssl_version, self.user, self._openssl_channel))
        self.requires("zlib/%s@%s/%s" % (self._zlib_version, self.user, self._zlib_channel))

    def build_requirements(self):
        if self.options.ninja:
            self.build_requires("ninja/[>=1.10.2]")
        if self.options.get_safe("dll_sign"):
            self.build_requires("windows_signtool/[>=1.2]@%s/stable" % self.user)

    def source(self):
        self.patch_version()
        for p in self.exports_patches:
            tools.patch(patch_file=p)
        if self.settings.os == "Windows":
            tools.patch(patch_file="fix_vs2022_build.patch")
            tools.patch(patch_file="disable_gen_node_def.patch")
        if self.settings.build_type == "Debug":
#            if self.settings.os == "Windows":
#                tools.patch(patch_file="fix_no_optimization_build.patch")
            if self.options.disable_v8_slow_dcheck:    
                tools.patch(patch_file="disable_v8_slow_dcheck.patch")
        tools.patch(patch_file="fix_deps_undici.patch")
            
    def patch_version(self):
        build_version = self.version.split(".")[3]
        content = tools.load("oda.patch")
        r = re.compile(r"\+#define NODE_BUILD_VERSION \d+")
        content = r.sub("+#define NODE_BUILD_VERSION %s" % build_version, content)
        tools.save("oda.patch", content)

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
        # External zlib
        zlib_includes = self.deps_cpp_info["zlib"].include_paths[0].replace("\\", "/")
        zlib_libpath = self.deps_cpp_info["zlib"].lib_paths[0].replace("\\", "/")
        flags.extend([
            "--shared-zlib",
            "--shared-zlib-includes=%s" % zlib_includes,
            "--shared-zlib-libpath=%s" % zlib_libpath
        ])
        if self.settings.os == "Windows":
            zlib_libname = "zlibstatic.lib" if self.settings.build_type == "Release" else "zlibstaticd.lib"
            flags.append("--shared-zlib-libname=%s" % zlib_libname)
            if self.settings.arch == "x86":
                flags.append("--no-cross-compiling")
        # Build type, debug/release
        if self.settings.build_type == "Debug":
            flags.append("--debug")
            flags.append("--v8-non-optimized-debug")
            if self.settings.os == "Linux":
                flags.append("--gdb")
        #
        if self.options.ninja:
            flags.append("--ninja")
        elif self.options.cmake:
            flags.append("--cmake")
        #
        env = { }
        if self.settings.os == "Linux":
            env["LD_LIBRARY_PATH"] = openssl_libpath
            ld_env_path = os.environ.get("LD_LIBRARY_PATH")
            if ld_env_path is not None:
                env["LD_LIBRARY_PATH"] += ":" + ld_env_path
        if self.settings.compiler == "Visual Studio":
            env = tools.vcvars_dict(self, force=True)
            if not "PATH" in env:
                env["PATH"] = []
            versions = { "15":"2017", "16":"2019", "17":"2022" }
            toolsets = { "15":"v141", "16":"v142", "17":"v143" }
            compilerVersion = str(self.settings.compiler.version)
            env["GYP_MSVS_VERSION"] = versions[compilerVersion]
            env["PLATFORM_TOOLSET"] = toolsets[compilerVersion] if not self.settings.compiler.toolset or self.settings.compiler.toolset is None else str(self.settings.compiler.toolset)
            # Explicit use external Ninja
            if self.options.ninja:
                ninja_binpath = self.deps_cpp_info["ninja"].bin_paths[0].replace("\\", "/")
                env["PATH"].append(ninja_binpath)
            # OpenSSL DLL in PATH for run tests
            if self.options.with_unit_tests:
                openssl_binpath = self.deps_cpp_info["openssl"].bin_paths[0].replace("\\", "/")
                env["PATH"].append(openssl_binpath)
        if self.settings.compiler == "gcc":
            env["CFLAGS"] = "-Wno-unused-but-set-parameter"
            env["CXXFLAGS"] = "-Wno-unused-but-set-parameter"
        # Run build
        with tools.chdir("src"), tools.environment_append(env):
            self.run("python --version")
            #
            self.run("python configure.py %s" % " ".join(flags))
            if self.options.ninja:
                self.run("ninja --version")
                self.run("ninja -C out/%s" % str(self.settings.build_type))
            elif self.options.cmake:
                cmake_src_folder = os.path.join(self.build_folder, "src", "out", str(self.settings.build_type))
                self.patch_cmake_script(os.path.join(cmake_src_folder, "CMakeLists.txt"))
                #
                self.output.info("CMakeLists.txt and working folder: %s" % cmake_src_folder)
                #
                cmake_build_type = "RelWithDebInfo" if self.settings.build_type == "Release" else "Debug"
                cmake = CMake(self, build_type=cmake_build_type, msbuild_verbosity='normal')
                cmake.verbose = True
                cmake.configure(source_folder=cmake_src_folder, build_folder=cmake_src_folder)
                # Manual build target before use it. Otherwise parallel build failed.
                cmake.build(target="icudata__icupkg") 
                cmake.build()
            elif self.settings.os == "Windows" and self.settings.compiler == "Visual Studio":
                msbuild = MSBuild(self)
                defines = { "WINVER": "0x0601", "_WIN32_WINNT": "0x0601", "NTDDI_VERSION": "0x06010000" }
                msbuild.build("node.sln", targets=["Build"], upgrade_project=False, verbosity="normal", use_env=False, platforms={"x86" : "Win32"}, definitions=defines)
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

    def patch_cmake_script(self, cmake_script_path):
        content = tools.load(cmake_script_path)
        lines = content.splitlines()
        def pred(l):
            return False if "../../deps/v8/src/heap/remembered-set.h" in l else True
        lines = filter(pred, lines)
        def patch(l):
            if l == "add_library(v8 STATIC)":
                return "add_library(v8 STATIC IMPORTED)"
            elif l.startswith("set_target_properties(jscript PROPERTIES LINK_FLAGS"):
                return "set_target_properties(jscript PROPERTIES LINK_FLAGS \"-pthread -rdynamic -m64\")"
            else:
                return l
        lines = map(patch, lines)
        content = "\n".join(lines)
        tools.save(cmake_script_path, content);

    def package(self):
        if not self.in_local_cache:
            tools.rmdir(self.package_folder)
            tools.mkdir(self.package_folder)
        # CMake script
        self.copy("FindJScript.cmake", dst=".", src=".")
        # Headers
        self.copy("jscript.h", dst="include", src="src/oda")
        self.copy("*.h", dst="include", src="src/src", keep_path=True)
        self.copy("*.h", dst="include", src="src/deps/v8/include", keep_path=True)
        self.copy("*.h", dst="include", src="src/deps/uv/include", keep_path=True)
        #
        self.copy("win_delay_load_hook.cc", dst="include", src=".", keep_path=False)
        # Libraries
        output_folder = "src/out/%s" % str(self.settings.build_type)
        if self.settings.os == "Windows":
            self.copy("jscript.dll.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("libjscript.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscript.dll", dst="bin", src=output_folder, keep_path=False)
            self.copy("libjscriptd.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll.lib", dst="lib", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll", dst="bin", src=output_folder, keep_path=False)
            # PDB
            self.copy("jscript.dll.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("libjscript.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscriptd.dll.pdb", dst="bin", src=output_folder, keep_path=False)
            self.copy("libjscriptd.pdb", dst="bin", src=output_folder, keep_path=False)
            # interpreter
            self.copy("jscript.exe", dst="bin", src=output_folder, keep_path=False)
            self.copy("jscriptd.exe", dst="bin", src=output_folder, keep_path=False)
        if self.settings.os == "Linux":
            self.copy("libjscript.so.*", dst="lib", src=output_folder + "/lib", keep_path=False, excludes="*.TOC")
            self.copy("libjscript.so.*", dst="lib", src=output_folder + "/lib.target", keep_path=False, excludes="*.TOC")
            self.copy("libjscript.so.*", dst="lib", src=output_folder, keep_path=False, excludes="*.TOC")
            # Symlink
            lib_folder = os.path.join(self.package_folder, "lib")
            if not os.path.isdir(lib_folder):
                return
            with tools.chdir(lib_folder):
                for fname in os.listdir("."):
                    extension = ".so"
                    symlink = fname[0:fname.rfind(extension) + len(extension)]
                    self.run("ln --symbolic --force \"%s\" \"%s\"" % (fname, symlink))
            self.copy("jscript", dst="bin", src=output_folder, keep_path=False)
        # Local build
        if not self.in_local_cache:
            self.copy("conanfile.py", dst=".", keep_path=False)
        # Sign DLL
        if self.options.get_safe("dll_sign"):
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
        self.info.options.cmake = "any"
        self.info.options.with_unit_tests = "any"

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
