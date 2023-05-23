#include "iceberg_table.h"

#include <string.h>
#include <iostream>
#include <cassert>

int test_iceberg_sketch() {
    iceberg_table table;
    if (iceberg_init_with_sketch(&table, 20, 5, 10)) {
        fprintf(stderr, "Can't allocate iceberg table.\n");
        exit(EXIT_FAILURE);
    }

    const int num_keys = 10;
    std::string keys[num_keys];
    for (int i = 0; i < num_keys; ++i) {
        keys[i] = "hello" + std::to_string(i);
        for (int pad = keys[i].size(); pad < KEY_SIZE; ++pad) {
            keys[i] += "\0";
        }
    }

    for (int i = 0; i < num_keys; ++i) {
        iceberg_insert(&table, (KeyType)keys[i].c_str(), i + 1004, 0);
        // std::cout << keys[i] << " is inserted to the cache\n";
    }

    ValueType *value = nullptr;
    iceberg_get_value(&table, (KeyType)keys[0].c_str(), &value, 0);
    std::cout << "Get " << keys[0] << " from the cache\n";
    std::cout << (uint64_t)*value << "\n";

    iceberg_remove(&table, (KeyType)keys[0].c_str(), 0);
    std::cout << keys[0] << " is removed\n";

    value = nullptr;
    
    iceberg_get_value(&table, (KeyType)keys[0].c_str(), &value, 0);
    std::cout << "Get " << keys[0] << " from the sketch\n";
    std::cout << (uint64_t)*value << "\n";

    for (uint64_t r = 0; r < table.sktch->rows; ++r) {
        for (uint64_t c = 0; c < table.sktch->cols; ++c) {
            uint64_t i = r * table.sktch->cols + c;
            std::cout << (uint64_t)table.sktch->table[i] << " ";
        }
        std::cout << "\n";
    }

    return 0;
}

int test_iceberg_without_sketch() {
    iceberg_table table;
    if (iceberg_init(&table, 20)) {
        fprintf(stderr, "Can't allocate iceberg table.\n");
        exit(EXIT_FAILURE);
    }

    const int num_keys = 10;
    std::string keys[num_keys];
    for (int i = 0; i < num_keys; ++i) {
        keys[i] = "hello" + std::to_string(i);
        for (int pad = keys[i].size(); pad < KEY_SIZE; ++pad) {
            keys[i] += "\0";
        }
    }

    for (int i = 0; i < num_keys; ++i) {
        iceberg_insert(&table, (KeyType)keys[i].c_str(), i + 1004, 0);
        // std::cout << keys[i] << " is inserted to the cache\n";
    }

    ValueType *value = nullptr;
    iceberg_get_value(&table, (KeyType)keys[0].c_str(), &value, 0);
    std::cout << "Get " << keys[0] << " from the cache\n";
    std::cout << (uint64_t)*value << "\n";

    iceberg_remove(&table, (KeyType)keys[0].c_str(), 0);
    std::cout << keys[0] << " is removed\n";

    value = nullptr;
    
    assert(!iceberg_get_value(&table, (KeyType)keys[0].c_str(), &value, 0));
    std::cout << "Not found " << keys[0] << " from the cache\n";

    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        int sketch = std::stoi(argv[1]);
        return sketch ? test_iceberg_sketch() : test_iceberg_without_sketch();
    }

    std::cout << argv[0] << " [0: without sketch, 1: with sketch]\n";

    return 1;
}