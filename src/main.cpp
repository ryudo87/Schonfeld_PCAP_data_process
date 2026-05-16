#include "app.h"

int main(int argc, char* argv[]) {
    AppConfig config;

    if (!parseArguments(argc, argv, config)) {
        printUsage(argv[0]);
        return static_cast<int>(ExitCode::InvalidArguments);
    }

    return runApp(config);
}
