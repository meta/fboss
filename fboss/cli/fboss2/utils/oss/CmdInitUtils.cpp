// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/utils/CmdInitUtils.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdCompletion.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"

namespace facebook::fboss::utils {

// Hidden `fboss2 __completion <word>...` entry point used by the shell
// completion script (fboss/oss/scripts/fboss2_completion.bash): the words are
// the command line typed so far, the last one being the word under the
// cursor. Prints the candidate completions one per line and exits. This is
// the OSS equivalent of internal FastCLI's `__metadata` endpoint: the shell
// asks the binary itself, so completion never goes stale as commands are
// added.
void postAppInit(int argc, char* argv[], CLI::App& app) {
  if (argc >= 2 && std::string(argv[1]) == "__completion") {
    std::vector<std::string> words;
    for (int i = 2; i < argc; ++i) {
      words.emplace_back(argv[i]);
    }
    if (words.empty()) {
      words.emplace_back();
    }
    for (const auto& candidate :
         completeCommandLine(app, *CmdSubcommands::getInstance(), words)) {
      std::cout << candidate << "\n";
    }
    // Single-threaded here: nothing has been parsed yet and no client or
    // thread has been started, so exiting is safe.
    std::exit(0); // NOLINT(concurrency-mt-unsafe)
  }
}

} // namespace facebook::fboss::utils
