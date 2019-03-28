# conan-jscript
jscript (based on Node.js) Conan package

# Options

## dll_sign
Signing DLL (Windows only), default True

## ninja
Use Ninja build system, default True

# Manual build package (in local folder)
Set enviroment variable CONAN_USERNAME to "odant"

In source package folder (where conanfile.py):

    conan install . -if local -o jscript:dll_sign=False -o jscript:ninja=False -s arch=x86_64 -s build_type=Debug
    conan source . -sf local
    conan build . -bf local
    conan package . -bf local -pf local/p

Set package to **editable** mode:

    conan editable add local/p jscript/x.y.z.b@odant/editable

After change source in folder **local** (run commands in source package folder):

    conan build . -bf local && conan package . -bf local -pf local/p
    Build your test project using that jscript/x.y.z.b@odant/editable

Get patch (difference betwen source in package and local):

    git diff --diff-filter=a --no-index -- src/ src/local
    Correct filepath, replace b/local/src to b/src
