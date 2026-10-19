// A stand-in for `gh` and `glab` in heap_git_remote_tests: no network.
//
// Copied under the name it plays (gh, glab). Each run appends one line to
// $FAKE_FORGE_LOG: its own name, then its arguments, space-separated. The
// answer comes from $FAKE_FORGE_RULES, read on every run, one rule a line:
//
//   <tool>\t<needle>\t<exit code>\t<stdout file or ->\t<sleep ms>
//
// The first rule whose tool is this one (or "*") and whose needle is part of
// the argument line wins: it sleeps, prints the file and exits with the
// code. No rule: exit 1.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string baseName(std::string p) {
  const auto slash = p.find_last_of("/\\");
  if(slash != std::string::npos) {
    p = p.substr(slash + 1);
  }
  const auto dot = p.rfind('.');
  if(dot != std::string::npos) {
    p = p.substr(0, dot);
  }
  return p;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for(const char c : line) {
    if(c == '\t') {
      out.push_back(cur);
      cur.clear();
    } else if(c != '\r') {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string self = baseName(argv[0]);
  std::string args;
  for(int i = 1; i < argc; ++i) {
    if(i > 1) {
      args += ' ';
    }
    args += argv[i];
  }
  if(const char* log = std::getenv("FAKE_FORGE_LOG")) {
    std::ofstream f(log, std::ios::app | std::ios::binary);
    f << self << ' ' << args << '\n';
  }
  const char* rules = std::getenv("FAKE_FORGE_RULES");
  if(rules == nullptr) {
    return 1;
  }
  std::ifstream in(rules, std::ios::binary);
  std::string line;
  while(std::getline(in, line)) {
    const std::vector<std::string> r = splitTabs(line);
    if(r.size() < 3 || (r[0] != "*" && r[0] != self) || args.find(r[1]) == std::string::npos) {
      continue;
    }
    if(r.size() > 4 && !r[4].empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(r[4].c_str())));
    }
    if(r.size() > 3 && !r[3].empty() && r[3] != "-") {
      std::ifstream out(r[3], std::ios::binary);
      std::stringstream ss;
      ss << out.rdbuf();
      const std::string body = ss.str();
      std::fwrite(body.data(), 1, body.size(), stdout);
    }
    return std::atoi(r[2].c_str());
  }
  return 1;
}
