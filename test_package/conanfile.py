# Test Conan package
# Dmitriy Vetutnev, Odant 2019 - 2020


from conans import ConanFile, CMake


class PackageTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = "ninja/[>=1.10.2]"

    def imports(self):
        self.copy("*.pdb", dst="bin", src="bin")
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.so*", dst="bin", src="lib")
        #
        self.copy("jscript", dst="bin", src="bin")
        self.copy("jscriptd", dst="bin", src="bin")
        self.copy("jscript.exe", dst="bin", src="bin")
        self.copy("jscriptd.exe", dst="bin", src="bin")

    def build(self):
        cmake = CMake(self, generator="Ninja", msbuild_verbosity='normal')
        cmake.verbose = True
        cmake.configure()
        cmake.build()
        self.cmake_is_multi_configuration = cmake.is_multi_configuration

    def test(self):
        if self.cmake_is_multi_configuration:
            self.run("ctest --verbose --build-config %s" % self.settings.build_type)
        else:
            self.run("ctest --verbose")

