#include <node.h>

#include <iostream>
#include <cstdlib>
#include <string>

int main(int, char**) {
    std::cout << "--------Running--------" << std::endl;
    
    char* argv[] = {
        "test_node",
        "-e",
        "console.log(\"xyz\");"
    };
    node::Start(3, argv);
    
    std::cout << "Main thread done" << std::endl;
    return EXIT_SUCCESS;
}
