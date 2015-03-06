#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;

const int E = -2;
const int G = -1;

struct stripe_config
{
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

void gen_stripe(uint64_t seed, const stripe_t &first_stripe, stripe_t &result,
                const stripe_config &config)
{
    result = first_stripe;
    mt19937_64 generator(seed);
    for (size_t i = config.stripe_length() - 1; i >= 1; i--) {
        size_t j = uniform_int_distribution<size_t>(0, i)(generator);
        swap(result[j], result[i]);
    }
}

void shift_stripe(uint64_t offset, const stripe_t &first_stripe,
                  stripe_t &result, const stripe_config &config)
{
    offset += (offset / config.disks);
    for (long long int i = 0; i < config.stripe_length(); i++) {
        result[(i + offset) % config.stripe_length()] = first_stripe[i];
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

uint64_t linux_hash(uint64_t val, unsigned int bits)
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

    /* High bits are more random, so use them. */
    return hash >> (64 - bits);
}

void hash_gen_stripe(uint64_t seed, const stripe_t &first_stripe,
                     stripe_t &result, const stripe_config &config)
{
    result = first_stripe;
    for (size_t i = config.stripe_length() - 1; i >= 1; i--) {
        seed = linux_hash(seed, 64);
        size_t j = seed % (i + 1);
        swap(result[j], result[i]);
    }
}

void add(vector<uint64_t> &sum, const stripe_t &stripe, int stripe_offset,
         const stripe_config &config)
{
    assert(stripe_offset < config.disks);
    int failed_index = (config.disks - stripe_offset) % config.disks;
    int failed = stripe[failed_index];
    for (int i = 0; i < config.stripe_length(); i++) {
        int source = stripe[i];
        sum[(stripe_offset + i) % config.disks] +=
            (i != failed_index) &&
            ((failed > 0 && source == failed) || (failed == G && source > 0));
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

int main()
{
    stripe_config config;
    config.disks = 255;
    config.local_groups = 15;
    config.local_group_size = 16;
    config.global_parities = 1;

    uint64_t kb = 1024;
    uint64_t mb = 1024 * kb;
    uint64_t gb = 1024 * mb;
    uint64_t tb = 1024 * gb;

    uint64_t stripe_width = 128 * kb;
    uint64_t stripe_size = stripe_width * config.stripe_length();

    uint64_t disk_size = 73 * gb;
    uint64_t array_size = disk_size * config.disks;

    uint64_t stripes = array_size / stripe_size;

    stripe_t first_stripe = gen_first_stripe(config);
    stripe_t curr_stripe;
    vector<uint64_t> sum(config.disks, 0);

    cout << "Calculating for " << stripes << " stripes" << endl;
    for (uint64_t i = 0; i < stripes; i++) {
        gen_stripe(fnv_hash(i), first_stripe, curr_stripe, config);
        //hash_gen_stripe(linux_hash(i, 64), first_stripe, curr_stripe, config);
        add(sum, curr_stripe, i % config.disks, config);
    }

    print_sum(sum);
    return 0;
}
