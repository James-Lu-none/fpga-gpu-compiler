#include "FpgaGpuAssembler.h"
#include <iostream>
#include <sstream>
#include <vector>

static bool expect(const char *source, const std::vector<uint32_t> &expected) {
    fpgagpu::Assembler assembler;
    std::istringstream input(source);
    std::vector<uint32_t> actual;
    if (!assembler.assemble(input, actual) || actual != expected) {
        std::cerr << "Unexpected encoding for: " << source << '\n';
        return false;
    }
    return true;
}

static bool reject(const char *source) {
    fpgagpu::Assembler assembler;
    std::istringstream input(source);
    std::vector<uint32_t> actual;
    if (assembler.assemble(input, actual)) {
        std::cerr << "Unexpected success for: " << source << '\n';
        return false;
    }
    return true;
}

int main() {
    bool ok = true;
    ok &= expect("SSY 7\nSSY -2\nSSY 0x10\nSSY -0x10\nSSY 0\n",
                 {0xE1000007, 0xE107FFFE, 0xE1000010, 0xE107FFF0, 0xE1000000});
    // A leading EXIT makes the SSY PC nonzero, distinguishing relative from absolute.
    ok &= expect("start: EXIT\nSSY end\nSSY start\nend: SYNC\n",
                 {0xFF000000, 0xE1000002, 0xE107FFFE, 0xE0000000});
    ok &= expect("self: SSY self ; zero offset\nBR NZP, self\n",
                 {0xE1000000, 0xC03FFFFF});
    ok &= reject("SSY missing\n");
    ok &= reject("SSY\n");
    ok &= reject("SSY 1 extra\n");
    ok &= reject("SSY 12garbage\n");
    ok &= reject("SSY 99999999999999999999\n");
    // Disassembler output must assemble back into identical SSY words.
    for (uint32_t word : {0xE1000007u, 0xE107FFFEu, 0xE1000000u}) {
        const auto source = fpgagpu::Assembler::disassemble(word);
        ok &= expect(source.c_str(), {word});
    }
    if (ok) std::cout << "PASS SSY assembler regression\n";
    return ok ? 0 : 1;
}
