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
    int disks;
    int local_groups;
    int local_group_size;
    int global_parities;

    int size() const
    {
        return local_groups * local_group_size + global_parities + 1;
    }
};

typedef vector<int> stripe_t;

stripe_t gen_first_stripe(const stripe_config &config)
{
    stripe_t result;
    result.reserve(config.size());
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
    for (size_t i = config.size() - 1; i >= 1; i--) {
        size_t j = uniform_int_distribution<size_t>(0, i)(generator);
        swap(result[j], result[i]);
    }
}

void shift_stripe(uint64_t offset, const stripe_t &first_stripe,
                  stripe_t &result, const stripe_config &config)
{
    offset += (offset / config.disks);
    for (size_t i = 0; i < config.size(); i++) {
        result[(i + offset) % config.size()] = first_stripe[i];
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

void add(vector<uint64_t> &sum, const stripe_t &stripe, int stripe_offset,
         const stripe_config &config)
{
    assert(stripe_offset < config.disks);
    int failed_index = (config.disks - stripe_offset) % config.disks;
    int failed = stripe[failed_index];
    for (int i = 0; i < config.size(); i++) {
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
    config.disks = 24;
    config.local_groups = 3;
    config.local_group_size = 7;
    config.global_parities = 1;

    uint64_t kb = 1024;
    uint64_t mb = 1024 * kb;
    uint64_t gb = 1024 * mb;
    uint64_t tb = 1024 * gb;

    uint64_t stripe_width = 128 * kb;
    uint64_t disk_size = 73 * gb;

    uint64_t stripes = disk_size / stripe_width;

    stripe_t first_stripe = gen_first_stripe(config);
    stripe_t curr_stripe;
    vector<uint64_t> sum(config.disks, 0);

    for (uint64_t i = 0; i < stripes; i++) {
        gen_stripe(fnv_hash(i), first_stripe, curr_stripe, config);
        add(sum, curr_stripe, i % config.disks, config);
    }

    print_sum(sum);
    return 0;
}
