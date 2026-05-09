#include <randomx.h>

#include <boost/asio.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <openssl/sha.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;
using boost::property_tree::ptree;

namespace {
std::mutex g_log_mutex;

void LogLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << line << std::endl;
}

/** Boost.PropertyTree JSON often stores numbers/bools as strings; normalize id/result. */
std::optional<int64_t> PtreeJsonInt(const ptree& root, const char* key)
{
    const auto opt = root.get_child_optional(key);
    if (!opt) return std::nullopt;
    const ptree& ch = *opt;
    try {
        return ch.get_value<int64_t>();
    } catch (...) {
    }
    try {
        const std::string s = ch.data();
        if (!s.empty()) return std::stoll(s);
    } catch (...) {
    }
    return std::nullopt;
}

bool PtreeJsonResultOk(const ptree& root)
{
    const auto opt = root.get_child_optional("result");
    if (!opt) return false;
    const ptree& r = *opt;
    const std::string d = r.data();
    if (d == "true" || d == "1") return true;
    if (d == "false" || d == "0") return false;
    if (d.empty() && r.empty()) return false;
    try {
        return r.get_value<bool>(false);
    } catch (...) {
        return false;
    }
}

bool PtreeJsonErrorPresent(const ptree& root)
{
    const auto opt = root.get_child_optional("error");
    if (!opt) return false;
    const ptree& e = *opt;
    if (e.empty()) {
        const std::string d = e.data();
        return !(d.empty() || d == "null");
    }
    return true;
}

constexpr unsigned char BYZE_RANDOMX_KEY_V1[] = {
    0x48, 0x45, 0x52, 0x4D, 0x48, 0x65, 0x72, 0x6D, 0x65, 0x73, 0x20, 0x43, 0x6F, 0x69, 0x6E, 0x20,
    0x52, 0x61, 0x6E, 0x64, 0x6F, 0x6D, 0x58, 0x20, 0x4E, 0x65, 0x74, 0x77, 0x6F, 0x72, 0x6B, 0x00};

struct Config {
    std::string host;
    std::string port;
    std::string wallet;
    std::string worker;
    int threads{0};
    /** 0 = use pool mining.set_difficulty only; else override until server sends difficulty. */
    uint32_t pool_difficulty_override{0};
};

struct Job {
    std::string id;
    uint32_t version{0};
    std::array<uint8_t, 32> prevhash{};
    uint32_t bits{0};
    uint32_t curtime{0};
    uint32_t mintime{0};
    uint64_t coinbase_value{0};
    uint32_t height{0};
    std::array<uint8_t, 32> target{};
    std::array<uint8_t, 32> target_be{};
};

struct ShareSubmitResult {
    bool accepted{false};
    std::string reason;
};

std::vector<uint8_t> HexToBytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) return out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        std::stringstream ss;
        ss << std::hex << hex.substr(i, 2);
        ss >> v;
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

std::string BytesToHex(const std::vector<uint8_t>& data)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t b : data) ss << std::setw(2) << static_cast<int>(b);
    return ss.str();
}

void WriteLE32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void WriteLE64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

void WriteVarInt(std::vector<uint8_t>& out, uint64_t v)
{
    if (v < 0xfd) out.push_back(static_cast<uint8_t>(v));
    else if (v <= 0xffff) { out.push_back(0xfd); out.push_back(v & 0xff); out.push_back((v >> 8) & 0xff); }
    else { out.push_back(0xfe); WriteLE32(out, static_cast<uint32_t>(v)); }
}

std::vector<uint8_t> Sha256d(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> h1(SHA256_DIGEST_LENGTH), h2(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), h1.data());
    SHA256(h1.data(), h1.size(), h2.data());
    return h2;
}

struct CoinbaseTx {
    std::vector<uint8_t> with_witness;
    std::vector<uint8_t> no_witness;
};

CoinbaseTx BuildCoinbaseTx(const Job& job, const std::string& worker)
{
    CoinbaseTx out;
    std::vector<uint8_t>& txw = out.with_witness;
    std::vector<uint8_t>& txnw = out.no_witness;
    WriteLE32(txw, 2);
    WriteLE32(txnw, 2);
    txw.push_back(0x00); // marker
    txw.push_back(0x01); // flag
    WriteVarInt(txw, 1);
    WriteVarInt(txnw, 1);
    txw.insert(txw.end(), 32, 0x00);
    txnw.insert(txnw.end(), 32, 0x00);
    WriteLE32(txw, 0xffffffff);
    WriteLE32(txnw, 0xffffffff);

    std::vector<uint8_t> scriptsig;
    scriptsig.push_back(0x03);
    scriptsig.push_back(job.height & 0xff);
    scriptsig.push_back((job.height >> 8) & 0xff);
    scriptsig.push_back((job.height >> 16) & 0xff);
    const std::string tag = "/byze-miner/" + worker + "/";
    scriptsig.push_back(static_cast<uint8_t>(std::min<size_t>(tag.size(), 60)));
    scriptsig.insert(scriptsig.end(), tag.begin(), tag.begin() + std::min<size_t>(tag.size(), 60));
    WriteVarInt(txw, scriptsig.size());
    WriteVarInt(txnw, scriptsig.size());
    txw.insert(txw.end(), scriptsig.begin(), scriptsig.end());
    txnw.insert(txnw.end(), scriptsig.begin(), scriptsig.end());
    WriteLE32(txw, 0xffffffff);
    WriteLE32(txnw, 0xffffffff);

    // Build witness commitment according to BIP141:
    // witness_root (coinbase-only) = 32 zero bytes, reserved value = 32 zero bytes
    std::vector<uint8_t> witness_preimage(64, 0x00);
    const std::vector<uint8_t> witness_commitment = Sha256d(witness_preimage);

    WriteVarInt(txw, 2);
    WriteVarInt(txnw, 2);
    WriteLE64(txw, job.coinbase_value);
    WriteLE64(txnw, job.coinbase_value);
    txw.push_back(0x01);
    txnw.push_back(0x01);
    txw.push_back(0x51);
    txnw.push_back(0x51);

    // OP_RETURN 0x24 aa21a9ed <32-byte-commitment>
    WriteLE64(txw, 0);
    WriteLE64(txnw, 0);
    std::vector<uint8_t> wscript;
    wscript.push_back(0x6a);
    wscript.push_back(0x24);
    wscript.push_back(0xaa);
    wscript.push_back(0x21);
    wscript.push_back(0xa9);
    wscript.push_back(0xed);
    wscript.insert(wscript.end(), witness_commitment.begin(), witness_commitment.end());
    WriteVarInt(txw, wscript.size());
    WriteVarInt(txnw, wscript.size());
    txw.insert(txw.end(), wscript.begin(), wscript.end());
    txnw.insert(txnw.end(), wscript.begin(), wscript.end());

    WriteVarInt(txw, 1);
    WriteVarInt(txw, 32);
    txw.insert(txw.end(), 32, 0x00);

    WriteLE32(txw, 0);
    WriteLE32(txnw, 0);
    return out;
}

std::array<uint8_t, 32> ToArray32LE(const std::string& hex_be)
{
    auto b = HexToBytes(hex_be);
    std::array<uint8_t, 32> out{};
    if (b.size() != 32) return out;
    for (size_t i = 0; i < 32; ++i) out[i] = b[31 - i];
    return out;
}

bool HashMeetsTargetLE(const uint8_t hash_le[32], const std::array<uint8_t, 32>& target_le)
{
    for (int i = 31; i >= 0; --i) {
        if (hash_le[i] < target_le[i]) return true;
        if (hash_le[i] > target_le[i]) return false;
    }
    return true;
}

bool HashMeetsTargetBE(const uint8_t hash_le[32], const std::array<uint8_t, 32>& target_be)
{
    // Convert hash bytes to big-endian view for direct lexical numeric compare.
    for (size_t i = 0; i < 32; ++i) {
        const uint8_t hb = hash_le[31 - i];
        const uint8_t tb = target_be[i];
        if (hb < tb) return true;
        if (hb > tb) return false;
    }
    return true;
}

/** Pool share target: min(network_target_uint256 * mult, 2^256-1), same as stratum-adapter pow.share_targets. */
void ShareTargetsFromNetwork(const std::array<uint8_t, 32>& net_target_le, uint32_t mult,
    std::array<uint8_t, 32>& share_le, std::array<uint8_t, 32>& share_be)
{
    if (mult < 1) mult = 1;
    using boost::multiprecision::cpp_int;
    cpp_int n = 0;
    for (size_t i = 0; i < 32; ++i)
        n += cpp_int(net_target_le[i]) << static_cast<unsigned>(8 * i);
    cpp_int st = n * cpp_int(mult);
    const cpp_int cap = (cpp_int(1) << 256) - 1;
    if (st > cap) st = cap;
    cpp_int x = st;
    for (size_t i = 0; i < 32; ++i) {
        share_le[i] = static_cast<uint8_t>((x & 0xff).convert_to<unsigned>());
        x >>= 8;
    }
    for (size_t i = 0; i < 32; ++i) {
        share_be[i] = static_cast<uint8_t>(((st >> static_cast<unsigned>(8 * (31 - i))) & 0xff).convert_to<unsigned>());
    }
}

uint32_t ParseBits(const std::string& bits_hex)
{
    uint32_t v{0};
    std::stringstream ss;
    ss << std::hex << bits_hex;
    ss >> v;
    return v;
}

struct MinerState {
    std::mutex mtx;
    std::optional<Job> job;
    std::atomic<bool> running{true};
    std::atomic<bool> has_job{false};
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    /** Stratum mining.set_difficulty (truncated to uint32, min 1). Default 1024 matches typical pool. */
    std::atomic<uint32_t> pool_difficulty{1024};
};

std::vector<uint8_t> BuildBlock(const Job& job, uint32_t nonce, const CoinbaseTx& coinbase)
{
    std::vector<uint8_t> block;
    block.reserve(2048);
    WriteLE32(block, job.version);
    block.insert(block.end(), job.prevhash.begin(), job.prevhash.end());
    auto cb_hash = Sha256d(coinbase.no_witness);
    block.insert(block.end(), cb_hash.begin(), cb_hash.end()); // merkle root, only coinbase
    WriteLE32(block, job.curtime);
    WriteLE32(block, job.bits);
    WriteLE32(block, nonce);
    WriteVarInt(block, 1);
    block.insert(block.end(), coinbase.with_witness.begin(), coinbase.with_witness.end());
    return block;
}

} // namespace

int main(int argc, char** argv)
{
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc) {
            const std::string pool = argv[++i];
            const auto p = pool.find(':');
            if (p == std::string::npos) { std::cerr << "Invalid --pool (host:port)\n"; return 1; }
            cfg.host = pool.substr(0, p);
            cfg.port = pool.substr(p + 1);
        } else if (a == "--wallet" && i + 1 < argc) cfg.wallet = argv[++i];
        else if (a == "--worker" && i + 1 < argc) cfg.worker = argv[++i];
        else if (a == "--threads" && i + 1 < argc) cfg.threads = std::stoi(argv[++i]);
        else if (a == "--pool-difficulty" && i + 1 < argc) cfg.pool_difficulty_override = static_cast<uint32_t>(std::stoul(argv[++i]));
    }
    if (cfg.host.empty() || cfg.wallet.empty()) {
        std::cerr << "Usage: byze-miner --pool host:port --wallet ADDRESS [--worker name] [--threads N] [--pool-difficulty N]\n";
        return 1;
    }
    if (cfg.worker.empty()) cfg.worker = "cpu0";
    if (cfg.threads <= 0) cfg.threads = std::max(1u, std::thread::hardware_concurrency());

    randomx_flags flags = static_cast<randomx_flags>(randomx_get_flags() | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM);
    randomx_cache* cache = randomx_alloc_cache(flags);
    if (!cache) return 2;
    randomx_init_cache(cache, BYZE_RANDOMX_KEY_V1, sizeof(BYZE_RANDOMX_KEY_V1));
    randomx_dataset* dataset = randomx_alloc_dataset(flags);
    if (!dataset) return 2;
    randomx_init_dataset(dataset, cache, 0, randomx_dataset_item_count());
    std::vector<randomx_vm*> vms;
    vms.reserve(cfg.threads);
    for (int i = 0; i < cfg.threads; ++i) {
        randomx_vm* vm = randomx_create_vm(flags, cache, dataset);
        if (!vm) return 2;
        vms.push_back(vm);
    }

    MinerState state;
    if (cfg.pool_difficulty_override >= 1) state.pool_difficulty.store(cfg.pool_difficulty_override);
    boost::asio::io_context io;
    tcp::resolver resolver(io);
    tcp::socket socket(io);
    boost::asio::connect(socket, resolver.resolve(cfg.host, cfg.port));

    std::mutex send_mtx;
    auto send_json = [&](const std::string& s) {
        std::lock_guard<std::mutex> lk(send_mtx);
        boost::asio::write(socket, boost::asio::buffer(s + "\n"));
    };

    send_json(R"({"id":1,"method":"mining.subscribe","params":["byze-miner/0.1"]})");
    send_json(std::string(R"({"id":2,"method":"mining.authorize","params":[")") + cfg.wallet + "." + cfg.worker + R"(","x"]})");

    std::thread reader([&] {
        boost::asio::streambuf buf;
        while (state.running.load()) {
            boost::system::error_code ec;
            boost::asio::read_until(socket, buf, '\n', ec);
            if (ec) break;
            std::istream is(&buf);
            std::string line;
            std::getline(is, line);
            if (line.empty()) continue;
            std::stringstream js(line);
            ptree root;
            try { read_json(js, root); } catch (...) { continue; }

            if (root.get_optional<std::string>("method") && root.get<std::string>("method") == "mining.set_difficulty") {
                if (!cfg.pool_difficulty_override) {
                    try {
                        auto params = root.get_child("params");
                        auto pit = params.begin();
                        if (pit != params.end()) {
                            const double d = pit->second.get_value<double>(1024.0);
                            uint32_t m = static_cast<uint32_t>(d);
                            if (m < 1) m = 1;
                            state.pool_difficulty.store(m);
                        }
                    } catch (...) {
                    }
                }
                continue;
            }
            if (root.get_optional<std::string>("method") && root.get<std::string>("method") == "mining.notify") {
                auto params = root.get_child("params");
                Job j;
                auto it = params.begin();
                if (it == params.end()) continue;
                j.id = it->second.get_value<std::string>();
                ++it;
                if (it == params.end()) continue;
                auto obj = it->second;
                j.version = obj.get<uint32_t>("template.version");
                j.prevhash = ToArray32LE(obj.get<std::string>("previousblockhash"));
                j.bits = ParseBits(obj.get<std::string>("bits"));
                j.curtime = obj.get<uint32_t>("curtime");
                j.mintime = obj.get<uint32_t>("template.mintime", j.curtime);
                j.coinbase_value = obj.get<uint64_t>("template.coinbasevalue");
                j.height = obj.get<uint32_t>("height");
                j.target = ToArray32LE(obj.get<std::string>("target"));
                auto tbytes = HexToBytes(obj.get<std::string>("target"));
                if (tbytes.size() == 32) {
                    for (size_t k = 0; k < 32; ++k) j.target_be[k] = tbytes[k];
                }
                {
                    std::lock_guard<std::mutex> g(state.mtx);
                    state.job = j;
                }
                state.has_job.store(true);
            } else {
                const auto rid = PtreeJsonInt(root, "id");
                if (rid && *rid >= 1000) {
                    const bool ok = PtreeJsonResultOk(root) && !PtreeJsonErrorPresent(root);
                    if (ok) state.accepted.fetch_add(1);
                    else state.rejected.fetch_add(1);
                }
            }
        }
        state.running.store(false);
    });

    std::atomic<uint32_t> submit_id{1000};
    std::vector<std::thread> miners;
    for (int t = 0; t < cfg.threads; ++t) {
        miners.emplace_back([&, t] {
            while (state.running.load()) {
                if (!state.has_job.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
                Job job;
                {
                    std::lock_guard<std::mutex> g(state.mtx);
                    if (!state.job.has_value()) continue;
                    job = *state.job;
                }
                auto coinbase = BuildCoinbaseTx(job, cfg.worker);
                LogLine("step1:block-candidate-built job=" + job.id + " height=" + std::to_string(job.height));
                for (uint32_t nonce = static_cast<uint32_t>(t); state.running.load(); nonce += static_cast<uint32_t>(cfg.threads)) {
                    std::array<uint8_t, 80> hdr{};
                    std::memcpy(hdr.data(), &job.version, 4);
                    std::memcpy(hdr.data() + 4, job.prevhash.data(), 32);
                    auto cb_hash = Sha256d(coinbase.no_witness);
                    std::memcpy(hdr.data() + 36, cb_hash.data(), 32);
                    std::memcpy(hdr.data() + 68, &job.curtime, 4);
                    std::memcpy(hdr.data() + 72, &job.bits, 4);
                    std::memcpy(hdr.data() + 76, &nonce, 4);
                    uint8_t hash[32];
                    randomx_calculate_hash(vms[t], hdr.data(), hdr.size(), hash);
                    if ((nonce & 0x3ffff) == 0) {
                        LogLine("step2:randomx-hashing job=" + job.id + " thread=" + std::to_string(t));
                    }
                    state.hashes.fetch_add(1, std::memory_order_relaxed);
                    const uint32_t pd = state.pool_difficulty.load(std::memory_order_relaxed);
                    std::array<uint8_t, 32> share_le{};
                    std::array<uint8_t, 32> share_be{};
                    ShareTargetsFromNetwork(job.target, pd, share_le, share_be);
                    const bool hit_share = HashMeetsTargetLE(hash, share_le) || HashMeetsTargetBE(hash, share_be);
                    const bool hit_block = HashMeetsTargetLE(hash, job.target) || HashMeetsTargetBE(hash, job.target_be);
                    if (hit_share) {
                        LogLine(std::string("step3:valid-share-detected job=") + job.id + " nonce=" + std::to_string(nonce) +
                                (hit_block ? " kind=block" : " kind=pool_share") +
                                " pdiff=" + std::to_string(pd));
                        auto block = BuildBlock(job, nonce, coinbase);
                        auto block_hex = BytesToHex(block);
                        const uint32_t id = submit_id.fetch_add(1);
                        std::ostringstream req;
                        req << "{\"id\":" << id << ",\"method\":\"mining.submit\",\"params\":[\""
                            << cfg.wallet << "." << cfg.worker << "\",\"" << block_hex << "\"]}";
                        LogLine("step4:submit-mining-submit id=" + std::to_string(id));
                        send_json(req.str());
                    }
                    if ((nonce & 0x3fff) == 0) {
                        std::lock_guard<std::mutex> g(state.mtx);
                        if (!state.job || state.job->id != job.id) break;
                    }
                }
            }
        });
    }

    std::thread stats([&] {
        uint64_t last = 0;
        while (state.running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            uint64_t now = state.hashes.load();
            uint64_t hps = now - last;
            last = now;
            LogLine("hashrate=" + std::to_string(hps) + " H/s accepted=" + std::to_string(state.accepted.load()) +
                    " rejected=" + std::to_string(state.rejected.load()));
        }
    });

    reader.join();
    state.running.store(false);
    for (auto& t : miners) if (t.joinable()) t.join();
    if (stats.joinable()) stats.join();

    for (auto* vm : vms) randomx_destroy_vm(vm);
    randomx_release_dataset(dataset);
    randomx_release_cache(cache);
    return 0;
}
