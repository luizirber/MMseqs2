#include "Debug.h"
#include "Util.h"
#include "Command.h"
#include "DistanceCalculator.h"
#include "Timer.h"

#ifndef NEON
#include <CpuInfo.h>
#endif

#include <iomanip>

extern const char *binary_name;
extern const char *tool_name;
extern const char *tool_introduction;
extern const char *main_author;
extern const char *version;
extern const char *show_extended_help;
extern const char *show_bash_info;
extern bool hide_base_commands;

extern std::vector<Command> commands;
extern std::vector<Command> baseCommands;
extern std::vector<Categories> categories;

void checkCpu() {
#ifndef NEON
    CpuInfo info;
    if (info.HW_x64 == false) {
        Debug(Debug::ERROR) << "64-bit system is required to run MMseqs2.\n";
        EXIT(EXIT_FAILURE);
    }
#ifdef SEE
    if(info.HW_SSE41 == false) {
        Debug(Debug::ERROR) << "SSE4.1 is required to run MMseqs2.\n";
        EXIT(EXIT_FAILURE);
    }
#endif
#ifdef AVX2
    if (info.HW_AVX2 == false) {
        Debug(Debug::ERROR) << "Your machine does not support AVX2.\n";
        if (info.HW_SSE41 == true) {
            Debug(Debug::ERROR) << "Please recompile with SSE4.1: cmake -DHAVE_SSE4_1=1 \n";
        } else {
            Debug(Debug::ERROR) << "SSE4.1 is the minimum requirement to run MMseqs2.\n";
        }
        EXIT(EXIT_FAILURE);
    }
#endif
#endif
}

Command *getCommandByName(const char *s) {
    for (size_t i = 0; i < commands.size(); i++) {
        Command &p = commands[i];
        if (!strcmp(s, p.cmd))
            return &p;
    }
    for (size_t i = 0; i < baseCommands.size(); i++) {
        Command &p = baseCommands[i];
        if (!strcmp(s, p.cmd))
            return &p;
    }
    return NULL;
}

int runCommand(Command *p, int argc, const char **argv) {
    Timer timer;
    int status = p->commandFunction(argc, argv, *p);
    Debug(Debug::INFO) << "Time for processing: " << timer.lap() << "\n";
    return status;
}

void printUsage(bool showExtended) {
    std::stringstream usage;
    usage << tool_introduction << "\n\n";
    usage << tool_name << " Version: " << version << "\n";
    usage << "© " << main_author << "\n";

    std::vector<int> showCategoryHeader(categories.size(), 0);
    for (size_t i = 0; i < categories.size(); ++i) {
        for (size_t j = 0; j < commands.size(); j++) {
            Command &p = commands[j];
            if (p.mode == categories[i].mode) {
                showCategoryHeader[i] = 1;
                break;
            }
        }
        if (hide_base_commands) {
            continue;
        }
        for (size_t j = 0; j < baseCommands.size(); j++) {
            Command &p = baseCommands[j];
            if (p.mode == categories[i].mode) {
                showCategoryHeader[i] = 1;
                break;
            }
        }
    }


    for (size_t i = 0; i < categories.size(); ++i) {
        if (showExtended == false
            && categories[i].mode != COMMAND_MAIN && categories[i].mode != COMMAND_EASY
            && categories[i].mode != COMMAND_FORMAT_CONVERSION && categories[i].mode != COMMAND_TAXONOMY) {
            // TODO not ready for prime time yet
            // && categories[i].mode != COMMAND_MULTIHIT) {
            continue;
        }

        if (showCategoryHeader[i] == 0) {
            continue;
        }

        usage << "\n" << std::setw(20) << categories[i].title << "\n";
        for (size_t j = 0; j < commands.size(); j++) {
            struct Command &p = commands[j];
            if (p.mode == categories[i].mode) {
                usage << std::left << std::setw(20) << "  " + std::string(p.cmd) << "\t" << p.shortDescription << "\n";
            }
        }
        if (hide_base_commands) {
            continue;
        }
        for (size_t j = 0; j < baseCommands.size(); j++) {
            struct Command &p = baseCommands[j];
            if (p.mode == categories[i].mode) {
                usage << std::left << std::setw(20) << "  " + std::string(p.cmd) << "\t" << p.shortDescription << "\n";
            }
        }
    }

    if (show_extended_help != NULL) {
        if (showExtended == false) {
            usage << "\n\nAn extended list of all tools can be obtained by calling '" << binary_name << " -h'.\n";
        }
    }
    if (show_bash_info != NULL) {
        usage  << "\nBash completion for tools and parameters can be installed by adding \"source MMSEQS_HOME/util/bash-completion.sh\" to your \"$HOME/.bash_profile\".\nInclude the location of the " << tool_name << " binary in your \"$PATH\" environment variable.";
    }
    Debug(Debug::INFO) << usage.str() << "\n";
}

int shellcompletion(int argc, const char **argv) {
    // mmseqs programs
    if (argc == 0) {
        for (size_t i = 0; i < commands.size(); i++) {
            struct Command &p = commands[i];
            if (p.mode == COMMAND_HIDDEN)
                continue;
            Debug(Debug::INFO) << p.cmd << " ";
        }
        if (hide_base_commands == false) {
            for (size_t i = 0; i < baseCommands.size(); i++) {
                struct Command &p = baseCommands[i];
                if (p.mode == COMMAND_HIDDEN)
                    continue;
                Debug(Debug::INFO) << p.cmd << " ";
            }
        }
        Debug(Debug::INFO) << "\n";
    }

    // mmseqs parameters for given program
    if (argc == 1) {
        for (size_t i = 0; i < commands.size(); i++) {
            struct Command &p = commands[i];
            if (strcmp(p.cmd, argv[0]) != 0) {
                continue;
            }
            if (p.params == NULL) {
                continue;
            }
            for (std::vector<MMseqsParameter *>::const_iterator it = p.params->begin(); it != p.params->end(); ++it) {
                Debug(Debug::INFO) << (*it)->name << " ";
            }
            Debug(Debug::INFO) << "\n";
            break;
        }
        if (hide_base_commands == false) {
            for (size_t i = 0; i < baseCommands.size(); i++) {
                struct Command &p = baseCommands[i];
                if (strcmp(p.cmd, argv[0]) != 0) {
                    continue;
                }
                if (p.params == NULL) {
                    continue;
                }
                for (std::vector<MMseqsParameter *>::const_iterator it = p.params->begin();
                     it != p.params->end(); ++it) {
                    Debug(Debug::INFO) << (*it)->name << " ";
                }
                Debug(Debug::INFO) << "\n";
                break;
            }
        }
        Debug(Debug::INFO) << "\n";
    }
    return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {
    checkCpu();

    if (argc < 2) {
        printUsage(false);
        return EXIT_SUCCESS;
    }

    if (argv[1][0] == '-' && argv[1][1] == 'h') {
        printUsage(true);
        return EXIT_SUCCESS;
    }

    setenv("MMSEQS", argv[0], true);
    Command *c = NULL;
    if (strncmp(argv[1], "shellcompletion", strlen("shellcompletion")) == 0) {
        return shellcompletion(argc - 2, argv + 2);
    } else if ((c = getCommandByName(argv[1])) != NULL) {
        EXIT(runCommand(c, argc - 2, argv + 2));
    } else {
        printUsage(true);
        Debug(Debug::INFO) << "\nInvalid Command: " << argv[1] << "\n";

        // Suggest some command that the user might have meant
        size_t index = SIZE_MAX;
        int maxDistance = 0;
        for (size_t i = 0; i < commands.size(); ++i) {
            struct Command &p = commands[i];
            if (p.mode == COMMAND_HIDDEN) {
                continue;
            }

            int distance = DistanceCalculator::localLevenshteinDistance(argv[1], p.cmd);
            if (distance > maxDistance) {
                maxDistance = distance;
                index = i;
            }
        }

        if (hide_base_commands == false) {
            for (size_t i = 0; i < baseCommands.size(); ++i) {
                struct Command &p = baseCommands[i];
                if (p.mode == COMMAND_HIDDEN)
                    continue;

                int distance = DistanceCalculator::localLevenshteinDistance(argv[1], p.cmd);
                if (distance > maxDistance) {
                    maxDistance = distance;
                    index = i;
                }
            }
        }

        if (index != SIZE_MAX) {
            Debug(Debug::WARNING) << "Did you mean \"" << argv[0] << " " << baseCommands[index].cmd << "\"?\n";
        }

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
