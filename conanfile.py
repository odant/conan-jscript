# jscript Conan package
# Dmitriy Vetutnev, Odant, 2018-2021


from conan import ConanFile, tools
import os, glob, re, platform


class JScriptConan(ConanFile):
    name = "jscript"
    version = "22.18.0.0"
    license = "Node.js https://raw.githubusercontent.com/nodejs/node/master/LICENSE"
    description = "Odant Jscript"
    url = "https://github.com/odant/conan-jscript"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "dll_sign": [False, True],
        "ninja": [False, True],
        "cmake": [False, True],
        "with_unit_tests": [False, True],
        "disable_v8_slow_dcheck": [False, True],
        "disable_sys_random": [False, True]
    }
    default_options = {
        "dll_sign": True,
        "ninja": False,
        "cmake": False,
        "with_unit_tests": False,
        "disable_v8_slow_dcheck": True,
        "disable_sys_random": False
    }
    exports_patches = [
        "oda.patch",
        "use_nodepath_for_esm.patch",
        "add_v8_options.patch"
    ]
    exports_sources = [
        "src/*",
        "win_delay_load_hook.cc",
        *exports_patches,
        "fix_no_optimization_build.patch",
        "disable_v8_slow_dcheck.patch",
        "fix_gen_node_def.patch",
        "fix_deps_undici.patch",
        "libuv_win7support.patch",
        "fix_v8_windows_build.patch",
        "disable_sys_random.patch",
        "fix_using_shared_openssl.patch"
    ]
    no_copy_source = False
    build_policy = "missing"
    package_type = "shared-library"
    python_requires = "windows_signtool/[>=1.2]@odant/stable"
    _internal_build_type = None
    
    def config_options(self):
        if self.settings.os != "Windows":
            self.options.ninja = True
            self.options.rm_safe("dll_sign")
        else:
            self.options.rm_safe("disable_sys_random")
        #
        if self.settings.os == "Windows" and not self.options.ninja and not self.options.cmake:
            self._internal_build_type = "Release" if self.settings.build_type == "RelWithDebInfo" else self.settings.build_type
        else:
            self._internal_build_type = self.settings.build_type

    def configure(self):
        if self.settings.os != "Windows":
            self.options.rm_safe("dll_sign")
        else:
            self.options.rm_safe("disable_sys_random")
        if self._internal_build_type != "Debug":
            self.options.rm_safe("disable_v8_slow_dcheck")

    def requirements(self):
        self.requires("openssl/[>=3.0.16]@%s/stable" % self.user)
        self.requires("zlib-ng/[>=2.2.4]@%s/stable" % self.user)

    def build_requirements(self):
        if self.options.ninja:
            self.build_requires("ninja/[>=1.12.1]")

    def source(self):
        self.patch_version()
        for p in self.exports_patches:
            tools.files.patch(self, patch_file=p)
        if platform.system() == "Windows":
            tools.files.patch(self, patch_file="fix_gen_node_def.patch")
            tools.files.patch(self, patch_file="libuv_win7support.patch")
            tools.files.patch(self, patch_file="fix_v8_windows_build.patch")
            tools.files.patch(self, patch_file="fix_using_shared_openssl.patch")
        tools.files.patch(self, patch_file="fix_deps_undici.patch")
            
    def patch_version(self):
        build_version = self.version.split(".")[3]
        content = tools.files.load(self, "oda.patch")
        r = re.compile(r"\+#define NODE_BUILD_VERSION \d+")
        content = r.sub("+#define NODE_BUILD_VERSION %s" % build_version, content)
        tools.files.save(self, "oda.patch", content)
        
    def generate(self):
        benv = tools.env.VirtualBuildEnv(self)
        if self.settings.compiler == "gcc":
            env = benv.environment()
            env.append("CFLAGS", "-Wno-unused-but-set-parameter")
            env.append("CXXFLAGS", "-Wno-unused-but-set-parameter")
            env.append("CXXFLAGS", "-Wno-template-id-cdtor")
            env.append("CXXFLAGS", "-Wno-deprecated-declarations")
            env.append("CXXFLAGS", "-Wno-unused-variable")
            env.append("CXXFLAGS", "-Wno-unused-value")
        benv.generate()
        renv = tools.env.VirtualRunEnv(self)
        renv.generate()
        if self.settings.os == "Windows" and (self.settings.compiler == "msvc" or ( self.settings.compiler == "clang" and self.settings.compiler.get_safe("runtime_version"))):
            if not self.options.ninja and not self.options.cmake:
                msbuild_tc = tools.microsoft.MSBuildToolchain(self)
                msbuild_tc.configuration = self._internal_build_type
                msbuild_tc.generate()
            else:    
                vc = tools.microsoft.VCVars(self)
                vc.generate()

    @property
    def _msvc_ide_version(self):
        compiler_version = str(self.settings.compiler.version)
        return {"194": "2022",
                "193": "2022",
                "192": "2019",
                "191": "2017",
                "190": "2015",
                "180": "2013"}.get(compiler_version) 
    @property
    def _msvc_ide_path(self):
        ide_version = self._msvc_ide_version
        ide_path = os.environ.get(f"VS{ide_version}INSTALLDIR", "")
        if not ide_path:
            ide_path = {"2019": "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community",
                        "2022": "C:\Program Files\Microsoft Visual Studio\2022\Community" }.get(ide_version)
        return ide_path
                    
    def build(self):
        if self.settings.os != "Windows" and self.options.get_safe("disable_sys_random"):
            tools.files.patch(self, patch_file="disable_sys_random.patch")
            
        if self._internal_build_type == "Debug":
#            if self.settings.os == "Windows":
#                tools.patch(patch_file="fix_no_optimization_build.patch")
            if self.options.disable_v8_slow_dcheck:    
                tools.files.patch(self, patch_file="disable_v8_slow_dcheck.patch")
        output_name = "jscript"
        if self.settings.os == "Windows":
            if self._internal_build_type == "Debug":
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
        openssl_includes = self.dependencies["openssl"].cpp_info.includedirs[0]
        openssl_libpath = self.dependencies["openssl"].cpp_info.libdirs[0]
        flags.extend([
            "--shared-openssl",
            '--shared-openssl-includes="%s"' % openssl_includes,
            '--shared-openssl-libpath="%s"' % openssl_libpath
        ])
        if self.settings.os == "Windows":
            flags.append("--shared-openssl-libname=libcrypto.lib,libssl.lib")
        # External zlib
        zlib_includes = self.dependencies["zlib-ng"].cpp_info.includedirs[0]
        zlib_libpath = self.dependencies["zlib-ng"].cpp_info.libdirs[0]
        flags.extend([
            "--shared-zlib",
            '--shared-zlib-includes="%s"' % zlib_includes,
            '--shared-zlib-libpath="%s"' % zlib_libpath
        ])
        if self.settings.os == "Windows":
            zlib_libname = ",".join(self.dependencies["zlib-ng"].cpp_info.aggregated_components().libs)
            flags.append("--shared-zlib-libname=%s" % zlib_libname)
            if self.settings.arch == "x86":
                flags.append("--no-cross-compiling")
        # Build type, debug/release
        if self._internal_build_type == "Debug":
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
        if self.settings.os == "Windows" and self.settings.compiler == "clang" and self.settings.compiler.get_safe("runtime_version"):
            flags.append("--clang-cl=%s" % self.settings.compiler.version)
        
        env = tools.env.Environment()
        if self.settings.os == "Windows" and self.settings.compiler == "msvc":
             env.define("GYP_MSVS_VERSION", self._msvc_ide_version)
             env.define("GYP_MSVS_OVERRIDE_PATH", self._msvc_ide_path)

        # Run build
        with tools.files.chdir(self, os.path.join(self.build_folder, "src")), env.vars(self).apply():
            self.run("python --version")
            #
            configure_flags = " ".join(flags)
            self.run("python configure.py %s" % configure_flags)
            if self.options.ninja:
                self.run("ninja --version")
                self.run("ninja -C out/%s" % str(self._internal_build_type))
            elif self.options.cmake:
                cmake_src_folder = os.path.join(self.build_folder, "src", "out", str(self._internal_build_type))
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
            elif self.settings.os == "Windows" and (self.settings.compiler == "msvc" or ( self.settings.compiler == "clang" and self.settings.compiler.get_safe("runtime_version"))):
                # import dll dependencies to bin folder
                output_folder = os.path.join(self.build_folder, "src/out/%s" % str(self._internal_build_type))
                tools.files.mkdir(self, output_folder)
                for dependency in self.dependencies.values():
                    if not dependency.is_build_context:
                        deps_cpp_info = dependency.cpp_info.aggregated_components()
                        for bindir in deps_cpp_info.bindirs:
                            tools.files.copy(self, "*.dll", src=bindir, dst=output_folder, keep_path=False)
                
                msbuild = tools.microsoft.MSBuild(self)
                # use Release instead of the RelWithDebInfo
                msbuild.build_type = self._internal_build_type
                # use Win32 instead of the default value when building x86
                msbuild.platform = "Win32" if self.settings.arch == "x86" else msbuild.platform                
                cmd = msbuild.command("node.sln", targets=["Build"])
                props_file = os.path.join(self.build_folder, "conantoolchain.props")
                if os.path.isfile(props_file):
                    cmd += ' -p:ForceImportBeforeCppTargets="%s"' % props_file
                cmd += ' -clp:NoItemAndPropertyList;PerformanceSummary;Verbosity=minimal -nologo'
                self.output.info(f"Run MSBuild command: {cmd}")    
                self.run(cmd)
            else:
                self.run("make -j %s" % tools.cpu_count())
            # Tests
            if self.options.with_unit_tests:
                shell = str(self._internal_build_type) + "/" + output_name
                if self.options.ninja:
                    shell = "out/" + shell
                if self.settings.os == "Windows":
                    shell += ".exe"
                self.run("python tools/test.py --shell=%s --progress=color --time --report -j %s" % (shell, tools.cpu_count()))

    def patch_cmake_script(self, cmake_script_path):
        content = tools.files.load(cmake_script_path)
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
        tools.files.save(cmake_script_path, content);

    def package(self):
        # Headers
        tools.files.copy(self, "jscript.h", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder,"src/oda"))
        tools.files.copy(self, "*.h", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder, "src/src"), keep_path=True)
        tools.files.copy(self, "*.h", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder, "src/deps/v8/include"), keep_path=True)
        tools.files.copy(self, "*.h", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder, "src/deps/uv/include"), keep_path=True)
        #
        tools.files.copy(self, "win_delay_load_hook.cc", dst=os.path.join(self.package_folder, "include"), src=self.export_sources_folder, keep_path=False)
        # Libraries
        output_folder = os.path.join(self.build_folder, "src/out/%s" % str(self._internal_build_type))
        if self.settings.os == "Windows":
            tools.files.copy(self, "jscript.dll.lib", dst=os.path.join(self.package_folder, "lib"), src=output_folder, keep_path=False)
            tools.files.copy(self, "libjscript.lib", dst=os.path.join(self.package_folder, "lib"), src=output_folder, keep_path=False)
            tools.files.copy(self, "jscript.dll", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            tools.files.copy(self, "libjscriptd.lib", dst=os.path.join(self.package_folder, "lib"), src=output_folder, keep_path=False)
            tools.files.copy(self, "jscriptd.dll.lib", dst=os.path.join(self.package_folder, "lib"), src=output_folder, keep_path=False)
            tools.files.copy(self, "jscriptd.dll", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            # PDB
            tools.files.copy(self, "jscript.dll.pdb", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            tools.files.copy(self, "libjscript.pdb", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            tools.files.copy(self, "jscriptd.dll.pdb", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            tools.files.copy(self, "libjscriptd.pdb", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            # interpreter
            tools.files.copy(self, "jscript.exe", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            tools.files.copy(self, "jscriptd.exe", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
        if self.settings.os == "Linux":
            tools.files.copy(self, "libjscript.so.*", dst=os.path.join(self.package_folder, "lib"), src=os.path.join(output_folder, "lib"), keep_path=False, excludes="*.TOC")
            tools.files.copy(self, "libjscript.so.*", dst=os.path.join(self.package_folder, "lib"), src=os.path.join(output_folder, "lib.target"), keep_path=False, excludes="*.TOC")
            tools.files.copy(self, "libjscript.so.*", dst=os.path.join(self.package_folder, "lib"), src=output_folder, keep_path=False, excludes="*.TOC")
            tools.files.copy(self, "jscript", dst=os.path.join(self.package_folder, "bin"), src=output_folder, keep_path=False)
            # Symlink
            lib_folder = os.path.join(self.package_folder, "lib")
            if os.path.isdir(lib_folder):
                with tools.files.chdir(self, lib_folder):
                    for fname in os.listdir("."):
                        extension = ".so"
                        symlink = fname[0:fname.rfind(extension) + len(extension)]
                        self.run("ln --symbolic --force \"%s\" \"%s\"" % (fname, symlink))
        # Sign DLL
        if self.options.get_safe("dll_sign"):
            self.python_requires["windows_signtool"].module.sign(self, [os.path.join(self.package_folder, "bin", "*.dll")])

    def package_id(self):
        self.info.options.ninja = "any"
        self.info.options.cmake = "any"
        self.info.options.with_unit_tests = "any"

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_file_name", "JScript")
        self.cpp_info.set_property("cmake_target_name", "JScript::JScript")
        self.cpp_info.requires = ["openssl::ssl", "openssl::crypto", "zlib-ng::zlib-ng"]
        self.cpp_info.libs = tools.files.collect_libs(self)
