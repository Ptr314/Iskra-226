#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>
#include "viewers/viewer_basic_iskra226.h"

int main(int argc, char** argv)
{
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    dsk_tools::ViewerBASIC_Iskra226 v;
    std::string s = v.process_as_text(data, "koi8_r");
    fwrite(s.data(), 1, s.size(), stdout);
    return 0;
}
