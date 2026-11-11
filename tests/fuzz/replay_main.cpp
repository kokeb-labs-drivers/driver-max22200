/**
 * @file replay_main.cpp
 * @brief Runs a fuzz target over saved inputs, for compilers without libFuzzer.
 *
 * `fuzz_replay_<target> <file-or-directory>...` feeds every file to `LLVMFuzzerTestOneInput()`
 * once. CTest runs it over tests/fuzz/corpus/<target>/, so each input the fuzzer ever kept, and
 * each crash it ever found, stays a regression test on GCC and Clang alike.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace {

bool RunFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    (void)std::fprintf(stderr, "cannot open %s\n", path.string().c_str());
    return false;
  }
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
  LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
  return true;
}

} // namespace

int main(int argc, char** argv) {
  int files = 0;
  for (int i = 1; i < argc; ++i) {
    const std::filesystem::path arg(argv[i]);
    std::error_code ec;
    if (std::filesystem::is_directory(arg, ec)) {
      for (const auto& entry : std::filesystem::directory_iterator(arg, ec)) {
        if (entry.is_regular_file(ec) && entry.path().filename().string()[0] != '.') {
          files += RunFile(entry.path()) ? 1 : 0;
        }
      }
    } else if (RunFile(arg)) {
      files++;
    }
  }
  (void)std::printf("replayed %d inputs\n", files);
  return files > 0 ? 0 : 1;
}
