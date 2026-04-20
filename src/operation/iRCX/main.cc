#include <iostream>
#include <string>

#include "include/Version.hh"
#include "include/cxxopts.hpp"
#include "log/Log.hh"
#include "shell_cmd/rcxShellCmd.hh"
#include "tcl/UserShell.hh"
#include "time/Time.hh"
#include "usage/usage.hh"

using namespace ircx;

int registerCommands() {
  registerTclCmd(CmdReadCorner,  "read_corner");
  registerTclCmd(CmdReadMapping, "read_mapping");
  registerTclCmd(CmdRCXInit,     "init_rcx");
  registerTclCmd(CmdRCXRun,      "run_rcx");
  registerTclCmd(CmdRCXReport,   "report_rcx");

  return EXIT_SUCCESS;
}

using ieda::Stats;
using ieda::Time;

int main(int argc, char** argv) {
  ieda::Log::init(argv);
  // Start the timer
  Time::start();

  std::string hello_info =
      "\033[49;32m***************************\n"
      "  _  _____   _____ __   __\n"
      " (_)|  __ \\ /  __ \\\\ \\ / /\n"
      "  _ | |__) || |     \\ V / \n"
      " | ||  _  / | |      > <  \n"
      " | || | \\ \\ | |____ / . \\ \n"
      " |_||_|  \\_\\ \\_____/_/ \\_\\\n"
      "***************************\n"
      "WELCOME TO iRCX TCL-shell interface. \e[0m";

  // get an UserShell (singleton) instance
  auto shell = ieda::UserShell::getShell();

  // set call back for register commands
  shell->set_init_func(registerCommands);

  // if no args, run interactively
  cxxopts::Options options("iRCX", "iRCX command line help.");
  options.add_options()("v,version", "Print Git Version")(
      "h,help", "iRCX command usage.")("script", "Tcl script file",
                                       cxxopts::value<std::string>());

  try {
    options.parse_positional("script");

    auto argv_parse_result = options.parse(argc, argv);

    if (argv_parse_result.count("help")) {
      options.custom_help("script [OPTIONS...]");
      std::cout << std::endl;
      std::cout << options.help() << std::endl;
      return 0;
    }

    shell->displayHello(hello_info);

    if (argv_parse_result.count("version")) {
      std::cout << "\033[49;32mGit Version: " << GIT_VERSION << "\033[0m"
                << std::endl;
    }

    if (argc == 1) {
      shell->displayHelp();
      shell->userMain(argc, argv);
    } else if (argc == 2) {
      shell->userMain(argv[1]);
    } else {
      if (argv_parse_result.count("script")) {
        // discard the first arg from main()
        // pass the rest of the args to Tcl interpreter
        auto tcl_argc = argc - 1;
        auto tcl_argv = argv + 1;
        shell->userMain(tcl_argc, tcl_argv);
      }
    }
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "Error parsing options: " << e.what() << std::endl;
    return 1;
  }

  ieda::Log::end();

  return 0;
}
