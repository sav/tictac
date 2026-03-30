#include "cli/cli_app.hpp"

int main(int argc, char* argv[]) {
    tictac::CliApp app;
    return app.run(argc, argv);
}
