/**
 * Consensus RandomX over 80-byte Bitcoin-style header (Byze BYZE_RANDOMX_KEY_V1).
 * Must match flags + key in src/main.cpp.
 *
 * Modes:
 *   byze-rxhash --stdio     (default): print READY\\n then read lines of 160 hex chars -> print 64 hex + \\n (daemon for pool).
 *   byze-rxhash --once HEX  One hash, stdout only (tests / CI).
 */
#include <randomx.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr unsigned char BYZE_RANDOMX_KEY_V1[] = {
    0x48, 0x45, 0x52, 0x4D, 0x48, 0x65, 0x72, 0x6D, 0x65, 0x73, 0x20, 0x43, 0x6F, 0x69, 0x6E, 0x20,
    0x52, 0x61, 0x6E, 0x64, 0x6F, 0x6D, 0x58, 0x20, 0x4E, 0x65, 0x74, 0x77, 0x6F, 0x72, 0x6B, 0x00};

bool HexToBytes80(const std::string& hex, uint8_t out[80])
{
    if (hex.size() != 160)
        return false;
    for (size_t i = 0; i < 80; ++i) {
        char c1 = hex[2 * i];
        char c2 = hex[2 * i + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nibble(c1);
        int lo = nibble(c2);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

void PrintHex32(const uint8_t h[32])
{
    static const char* x = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        std::cout << x[h[i] >> 4];
        std::cout << x[h[i] & 15];
    }
    std::cout.put('\n');
    std::cout.flush();
}

struct RxEngine {
    randomx_cache* cache{nullptr};
    randomx_dataset* dataset{nullptr};
    randomx_vm* vm{nullptr};
    randomx_flags flags{};
};

RxEngine alloc_engine()
{
    RxEngine eng;
    eng.flags = static_cast<randomx_flags>(randomx_get_flags() | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM);
    eng.cache = randomx_alloc_cache(eng.flags);
    if (!eng.cache)
        return eng;
    randomx_init_cache(eng.cache, BYZE_RANDOMX_KEY_V1, sizeof(BYZE_RANDOMX_KEY_V1));
    eng.dataset = randomx_alloc_dataset(eng.flags);
    if (!eng.dataset)
        return eng;
    randomx_init_dataset(eng.dataset, eng.cache, 0, randomx_dataset_item_count());
    eng.vm = randomx_create_vm(eng.flags, eng.cache, eng.dataset);
    return eng;
}

void free_engine(RxEngine& eng)
{
    if (eng.vm)
        randomx_destroy_vm(eng.vm);
    if (eng.dataset)
        randomx_release_dataset(eng.dataset);
    if (eng.cache)
        randomx_release_cache(eng.cache);
}

bool ComputeHash(RxEngine& eng, const uint8_t hdr[80], uint8_t hash[32])
{
    if (!eng.vm)
        return false;
    randomx_calculate_hash(eng.vm, hdr, 80, hash);
    return true;
}

bool RunStdioDaemon()
{
    RxEngine eng = alloc_engine();
    if (!eng.vm) {
        std::cerr << "byze-rxhash: RandomX init failed\n";
        return false;
    }
    std::cout << "READY\n";
    std::cout.flush();

    std::string line;
    uint8_t hdr[80];
    uint8_t hash[32];
    while (std::getline(std::cin, line)) {
        // strip all whitespace from line (header hex must be contiguous 160 chars)
        line.erase(
            std::remove_if(
                line.begin(),
                line.end(),
                [](unsigned char ch) { return std::isspace(ch); }),
            line.end());

        if (line.empty())
            continue;
        if (!HexToBytes80(line, hdr)) {
            std::cerr << "byze-rxhash: expected 160 hex chars per line\n";
            std::cerr.flush();
            std::cout << "ERROR_hex\n";
            std::cout.flush();
            continue;
        }
        if (!ComputeHash(eng, hdr, hash)) {
            std::cerr << "byze-rxhash: hash failed\n";
            std::cerr.flush();
            std::cout << "ERROR_hash\n";
            std::cout.flush();
            continue;
        }
        PrintHex32(hash);
    }
    free_engine(eng);
    return true;
}

bool RunOnce(const char* hex160)
{
    std::string line = hex160;
    line.erase(
        std::remove_if(
            line.begin(),
            line.end(),
            [](unsigned char ch) { return std::isspace(ch); }),
        line.end());
    uint8_t hdr[80];
    if (!HexToBytes80(line, hdr)) {
        std::cerr << "byze-rxhash: --once expects 160 hex chars\n";
        return false;
    }
    RxEngine eng = alloc_engine();
    if (!eng.vm) {
        std::cerr << "byze-rxhash: RandomX init failed\n";
        return false;
    }
    uint8_t hash[32];
    if (!ComputeHash(eng, hdr, hash)) {
        free_engine(eng);
        return false;
    }
    PrintHex32(hash);
    free_engine(eng);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(true);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        std::cerr << "Usage:\n"
                     "  byze-rxhash [--stdio]              daemon: READY then lines of 160 header hex bytes\n"
                     "  byze-rxhash --once <160-char-hex>  single hash stdout\n";
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--once") == 0)
        return RunOnce(argv[2]) ? 0 : 1;
    // Legacy: argv[1] is exactly the 160 hex chars
    if (argc == 2 && argv[1][0] != '-')
        return RunOnce(argv[1]) ? 0 : 1;
    /* default first arg optional --stdio */
    return RunStdioDaemon() ? 0 : 1;
}
