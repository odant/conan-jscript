#include <jscript.h>

#include <iostream>
#include <cstdlib>

int main(int, char**) {
    
    const wchar_t* argv[] = {
        L"test_jscript",
        L"--version"
    };
    jscript::Initialize(2, argv);
    jscript::Uninitilize();
    
    return EXIT_SUCCESS;
}
