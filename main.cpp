#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;

const int E = -2;
const int G = -1;

struct stripe_config {
    long long int disks;
    long long int local_groups;
    long long int local_group_size;
    long long int global_parities;

    long long int stripe_length() const
    {
        return local_groups * local_group_size + global_parities + 1;
    }
};

typedef vector<int> stripe_t;

class xorshift
{
  public:
    xorshift(uint64_t seed);
    uint64_t operator()();

  private:
    uint32_t gen32();
    uint32_t x, y, z, w;
};

xorshift::xorshift(uint64_t seed)
    : x(seed >> 32), y(seed), z(seed >> 32), w(seed)
{
}

uint64_t xorshift::operator()()
{
    uint64_t result = gen32();
    result <<= 32;
    result |= gen32();
    return result;
}

uint32_t xorshift::gen32()
{
    uint32_t t = x ^ (x << 11);
    x = y;
    y = z;
    z = w;
    w = w ^ (w >> 19) ^ (t ^ (t >> 8));
    return w;
}

class xorshift_plus
{
  public:
    xorshift_plus(uint64_t seed);
    uint64_t operator()();

  private:
    uint64_t s0, s1;
};

xorshift_plus::xorshift_plus(uint64_t seed) : s0(seed), s1(seed) {}

uint64_t xorshift_plus::operator()()
{
    uint64_t x = s0;
    uint64_t const y = s1;
    s0 = y;
    x ^= x << 23;
    x ^= x >> 17;
    x ^= y ^ (y >> 26);
    s1 = x;
    return x + y;
}

stripe_t gen_first_stripe(const stripe_config &config)
{
    stripe_t result;
    result.reserve(config.stripe_length());
    for (int local_group = 1; local_group <= config.local_groups;
         local_group++) {
        for (int local_strip = 0; local_strip < config.local_group_size - 1;
             local_strip++) {
            result.push_back(local_group);
        }
        result.push_back(local_group + config.local_groups);
    }
    for (int global_parity = 1; global_parity <= config.global_parities;
         global_parity++) {
        result.push_back(G);
    }
    result.push_back(E);
    return result;
}

template <uint64_t gen_func()>
uint64_t uniform_distribution(uint64_t rangeLow, uint64_t rangeHigh)
{
    uint64_t range = rangeHigh - rangeLow + 1;
    uint64_t copies = RAND_MAX / range;
    uint64_t limit = range * copies;
    uint64_t myRand = limit;
    while (myRand >= limit) {
        myRand = gen_func();
    }
    return myRand / copies + rangeLow;
}

template <typename generator_t>
void gen_stripe(uint64_t seed, const stripe_t &first_stripe, stripe_t &result,
                const stripe_config &config)
{
    result = first_stripe;
    generator_t generator(seed);
    for (size_t i = config.stripe_length() - 1; i >= 1; i--) {

        size_t j = generator() % (i + 1);

        // size_t j = uniform_distribution<bind(generator::operator(), _1)>(0,
        // i);
        swap(result[j], result[i]);
    }
}

uint64_t fnv_hash(uint64_t number)
{
    uint64_t fnv_offset_basis = 0xcbf29ce484222325ULL;
    uint64_t fnv_prime = 0x100000001b3ULL;

    uint64_t result = fnv_offset_basis;
    for (size_t offset = 0; offset < 64; offset += 8) {
        uint64_t byte = (number >> offset) & 0xFFULL;
        result ^= byte;
        result *= fnv_prime;
    }
    return result;
}

uint64_t linux_hash(uint64_t val)
{
    uint64_t hash = val;

    /*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
    uint64_t n = hash;
    n <<= 18;
    hash -= n;
    n <<= 33;
    hash -= n;
    n <<= 3;
    hash += n;
    n <<= 3;
    hash -= n;
    n <<= 4;
    hash += n;
    n <<= 2;
    hash += n;

    return hash;
}

string stripe_elem(int code, const stripe_config &config)
{
    if (code == E) {
        return " E";
    } else if (code == G) {
        return " G";
    } else if (code == 0) {
        return "00";
    } else if (code <= config.local_groups) {
        string s = " ";
        s.push_back('0' + code);
        return s;
    } else {
        string s = "S";
        s.push_back('0' + (code - config.local_groups));
        return s;
    }
}

bool same_local_group(int b1, int b2, const stripe_config &config)
{
    return b1 > 0 && b2 > 0 && (b1 == b2 || b1 == b2 + config.local_groups ||
                                b2 == b1 + config.local_groups);
}

void add(vector<uint64_t> &sum, const stripe_t &stripe, int stripe_offset,
         const stripe_config &config, bool debug_print = false)
{
    assert(stripe_offset < config.disks);
    int failed_index = (config.disks - stripe_offset) % config.disks;
    if (failed_index >= config.stripe_length()) {
        return;
    }
    int failed = stripe[failed_index];
    for (int i = 0; i < config.stripe_length(); i++) {
        int source = stripe[i];
        sum[(stripe_offset + i) % config.disks] +=
            (i != failed_index) && (same_local_group(source, failed, config) ||
                                    (failed == G && source > 0));
    }

    if (debug_print) {
        for (long long int i = 0; i < stripe_offset; i++) {
            cout << "   ";
        }
        for (size_t i = 0; i < stripe.size(); i++) {
            if ((i + stripe_offset) % config.disks == 0) {
                cout << endl;
            }
            cout << stripe_elem(stripe[i], config) << " ";
        }
        cout << endl;
        for (size_t i = 0; i < sum.size(); i++) {
            cout << sum[i] << " ";
        }
        cout << endl;
    }
}

void print_sum(const vector<uint64_t> &sum)
{
    uint64_t min = sum[1];
    uint64_t max = sum[1];
    for (size_t i = 0; i < sum.size(); i++) {
        cout << sum[i] << ' ';
        if (i != 0) {
            min = ::min(min, sum[i]);
            max = ::max(max, sum[i]);
        }
    }
    double deviation = (max - min) * 100.0 / max;
    cout << endl;
    cout << "Min: " << min << ", max: " << max << endl;
    cout << "Diff: " << (max - min) << ", (max-min)/max: " << deviation << "%"
         << endl;
}

template <uint64_t hash(uint64_t), typename generator_t>
void test(uint64_t stripes, stripe_config config, string description)
{
    cout << description << endl;

    stripe_t first_stripe = gen_first_stripe(config);
    stripe_t curr_stripe;
    vector<uint64_t> sum(config.disks, 0);

    for (uint64_t i = 0; i < stripes; i++) {
        gen_stripe<generator_t>(hash(i), first_stripe, curr_stripe, config);
        add(sum, curr_stripe, (i * config.stripe_length()) % config.disks,
            config);
    }

    print_sum(sum);
    cout << endl;
}

int main()
{
    stripe_config config;
    config.disks = 24;
    config.local_groups = 3;
    config.local_group_size = 7;
    config.global_parities = 1;

    const uint64_t kb = 1024;
    const uint64_t mb = 1024 * kb;
    const uint64_t gb = 1024 * mb;
    const uint64_t tb = 1024 * gb;

    const uint64_t stripe_width = 128 * kb;
    const uint64_t stripe_size = stripe_width * config.stripe_length();

    const uint64_t disk_size = 73 * gb;
    const uint64_t array_size = disk_size * config.disks;

    const uint64_t stripes = array_size / stripe_size;
    cout << "Calculating for " << stripes << " stripes" << endl << endl;

#define TEST_HELPER(description, hash, gen_t)                                  \
    test<hash, gen_t>(stripes, config, description)
#define TEST(hash, gen_t) TEST_HELPER(#hash ", " #gen_t, hash, gen_t)

    TEST(fnv_hash, mt19937_64);
    TEST(linux_hash, mt19937_64);
    TEST(fnv_hash, xorshift);
    TEST(linux_hash, xorshift);
    TEST(fnv_hash, xorshift_plus);
    TEST(linux_hash, xorshift_plus);

    return 0;
}
