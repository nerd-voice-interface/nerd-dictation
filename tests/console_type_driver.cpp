// Drives InputSimulator("auto") from the command line, for tty-i3-keys'
// end-to-end test of typing on the text console:
//   console_type_driver type <text>
//   console_type_driver key <combination>
// Build: make console_type_driver (not part of the default build).
#include "nerd_dictation/input/input_simulator.hpp"
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    nerd_dictation::input::InputSimulator sim("auto");
    std::string mode = argv[1];
    if (mode == "type") {
        sim.type_text(argv[2]);
    } else if (mode == "key") {
        sim.press_keys(argv[2]);
    } else {
        return 2;
    }
    return 0;
}
