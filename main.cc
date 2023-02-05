#include "iceberg_table.h"
#include <time.h>
#include <thread>
#include <immintrin.h>
#include <tmmintrin.h>
#include <openssl/rand.h>
#include <unistd.h>
#include <chrono>
#include <random>
#include <algorithm>
#include <string.h>
#include <assert.h>
#include <iostream>
#include <fstream>

#include <sys/time.h>
#include <sys/resource.h>

using namespace std::chrono;

//vectors of key/value pairs in the table and not in the table
std::vector<std::pair<uint64_t, uint64_t>> in_table, not_in_table;

iceberg_table table;

double elapsed(high_resolution_clock::time_point t1, high_resolution_clock::time_point t2) {
  return (duration_cast<duration<double>>(t2 - t1)).count();
}

void do_inserts(uint8_t id, char **keys, ValueType *values, uint64_t start, uint64_t n) {
#ifdef LATENCY
  std::vector<double> times;
#endif
  for(uint64_t i = start; i < start + n; ++i) {
#ifdef LATENCY
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
#endif
    if(!iceberg_put(&table, keys[i], values[i], id)) {
      printf("Failed insert\n");
      exit(0);
    }
    //printf("\rInsert %ld", i);
    //fflush(stdout);
#ifdef LATENCY
    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    times.emplace_back(duration_cast<nanoseconds>(t2-t1).count());
#endif
    /*
       uint64_t *val;
       for(uint64_t j = start; j < i; ++j) {
       if (iceberg_get_value(&table, keys[j], &val, id) != true) {
       printf("False negative query key: " "%" PRIu64 "\n", keys[j]);
       exit(0);
       }
       }
       */
  }
#ifdef LATENCY
  std::ofstream f;
  f.open("insert_times_" + std::to_string(id) + ".log");
  for (auto time : times) {
    f << time << '\n';
  }
  f.close();
#endif
}

void do_queries(uint8_t id, char **keys, uint64_t start, uint64_t n, bool positive) {

  ValueType *val;
#ifdef LATENCY
  std::vector<double> times;
#endif
  for(uint64_t i = start; i < start + n; ++i) {
#ifdef LATENCY
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
#endif
    if (iceberg_get_value(&table, keys[i], &val, id) != positive) {
      if(positive)
        printf("False negative query key: " "%s\n", keys[i]);
      else
        printf("False positive query\n");
      exit(0);
    }
#ifdef LATENCY
    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    times.emplace_back(duration_cast<nanoseconds>(t2-t1).count());
#endif
  }
#ifdef LATENCY
  std::ofstream f;
  f.open("query_times_" + std::to_string(positive) + "_" + std::to_string(id) + ".log");
  for (auto time : times) {
    f << time << '\n';
  }
  f.close();
#endif
}

void do_removals(uint8_t id, char **keys, uint64_t start, uint64_t n, bool force_remove) {
  //uint64_t val;
  for(uint64_t i = start; i < start + n; ++i) {
    if (force_remove) {
      if(!iceberg_force_remove(&table, keys[i], id)) {
	printf("Failed removal\n");
	exit(0);
      }
    } else {
      iceberg_remove(&table, keys[i], id);
    }
  }
}

void safe_rand_bytes(unsigned char *v, size_t n) {
  while (n > 0) {
    size_t round_size = n >= INT_MAX ? INT_MAX - 1 : n;
    RAND_bytes(v, round_size);
    v += round_size;
    n -= round_size;
  }

  //for (uint64_t i = 0; i < n; ++i) {
  //v[i] = rand();
  //}
     
}

int main (int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    fprintf(stderr, "Specify the log of the number of slots in the table and the number of threads to use.\n");
    exit(1);
  }

  bool is_benchmark = false;
  if (argc == 4) {
    assert(strcmp(argv[3], "-b") == 0);
    is_benchmark = true;
  }

  uint64_t tbits = atoi(argv[1]);
  uint64_t threads = atoi(argv[2]);
  uint64_t N = 10000000; //(1ULL << tbits) * 1.07;
  //uint64_t N = (1ULL << tbits) * 1.07 * 1.90;

  high_resolution_clock::time_point t1 = high_resolution_clock::now();

  if (iceberg_init(&table, tbits)) {
    fprintf(stderr, "Can't allocate iceberg table.\n");
    exit(EXIT_FAILURE);
  }

  high_resolution_clock::time_point t2 = high_resolution_clock::now();
  if (!is_benchmark) {
    printf("Creation time: %f\n", elapsed(t1, t2));
  }

  srand(time(NULL));
  //srand(0);

  //Generating vectors of size N for data contained and not contained in the tablea
  uint64_t splits = 1;
#ifdef INSTAN_THRPT
  splits = 19;
#endif

  uint64_t size = N / splits / threads;

  N = N / size * size;

#ifdef INSTAN_THRPT
  uint64_t total_alloc = (N * sizeof(uint64_t) * 4)/1024;
#endif
  if (!is_benchmark) {
    printf("%ld\n", N * 2 * sizeof(uint64_t));
  }

  char *in_keys = (char *)malloc(N * KEY_SIZE);
  if(!in_keys) {
    printf("Malloc in_keys failed\n");
    exit(0);
  }
  safe_rand_bytes((unsigned char *)in_keys, KEY_SIZE * N);

  char **in_key_ptrs = (char **)malloc(N * sizeof(char **));
  if(!in_key_ptrs) {
    printf("Malloc in_keys failed\n");
    exit(0);
  }
  for (uint64_t i = 0; i < N; i++) {
    in_key_ptrs[i] = &in_keys[KEY_SIZE * i];
  }

  ValueType *in_values = (ValueType *)malloc(N * sizeof(ValueType));
  if(!in_values) {
    printf("Malloc in_values failed\n");
    exit(0);
  }
  safe_rand_bytes((unsigned char *)in_values, sizeof(*in_values) * N);
  for (uint64_t i = 0; i < N; i++) {
    in_values[i].refcount = 0;
  }

  char *out_keys = (char *)malloc(N * KEY_SIZE);
  if(!out_keys) {
    printf("Malloc out_keys failed\n");
    exit(0);
  }
  safe_rand_bytes((unsigned char *)out_keys, KEY_SIZE * N);

  char **out_key_ptrs = (char **)malloc(N * sizeof(char **));
  if(!out_key_ptrs) {
    printf("Malloc out_keys failed\n");
    exit(0);
  }
  for (uint64_t i = 0; i < N; i++) {
    out_key_ptrs[i] = &out_keys[KEY_SIZE * i];
  }

  if (!is_benchmark) {
    printf("INSERTIONS\n");
  }

  //	exit(0);

  t1 = high_resolution_clock::now();

  std::vector<std::thread> thread_list;
#ifdef INSTAN_THRPT
  struct rusage usage;
#endif
  for(uint64_t i = 0; i < splits; ++i) {
#ifdef INSTAN_THRPT
    high_resolution_clock::time_point t1, t2;
    t1 = high_resolution_clock::now();
#endif
    for(uint64_t j = 0; j < threads; j++)
      thread_list.emplace_back(do_inserts, j, in_key_ptrs, in_values, (i * threads + j) * size, size);
    for(uint64_t j = 0; j < threads; j++)
      thread_list[j].join();

#ifdef INSTAN_THRPT
    auto num = i * size + size;
    getrusage(RUSAGE_SELF, &usage);
    printf("Num: %ld MaxRSS: %ld\n", num, usage.ru_maxrss-total_alloc);
    t2 = high_resolution_clock::now();
    printf("%f\n", size * threads / elapsed(t1, t2));
#endif
    thread_list.clear();
  }

#ifdef ENABLE_RESIZE
  iceberg_end(&table);
#endif
  t2 = high_resolution_clock::now();

  //double insert_throughput = N / elapsed(t1, t2);
  //	exit(0);

  if (!is_benchmark) {
    printf("Insertions: %f\n", N / elapsed(t1, t2));

    printf("Load factor: %f\n", iceberg_load_factor(&table));
    printf("Number level 1 inserts: %ld\n", lv1_balls(&table));
    printf("Number level 2 inserts: %ld\n", lv2_balls(&table));
    printf("Number level 3 inserts: %ld\n", lv3_balls(&table));
    printf("Total inserts: %ld\n", tot_balls(&table));
  }

  // uint64_t max_size = 0, sum_sizes = 0;
  // for(uint64_t i = 0; i < table.metadata.nblocks; ++i) {
  //   max_size = std::max(max_size, table.metadata.lv3_sizes[0][i]);
  //   sum_sizes += table.metadata.lv3_sizes[0][i];
  // }

  // if (!is_benchmark) {
  //   printf("Average list size: %f\n", sum_sizes / (double)table.metadata.nblocks);
  //   printf("Max list size: %ld\n", max_size);

  // }

  std::mt19937 g(__builtin_ia32_rdtsc());

  //	exit(0);

  if (!is_benchmark) {
    printf("\nREFCOUNT INCREMENTS\n");
  }

  for(uint64_t i = 0; i < splits; ++i) {
    for(uint64_t j = 0; j < threads; j++)
      thread_list.emplace_back(do_inserts, j, in_key_ptrs, in_values, (i * threads + j) * size, size);
    for(uint64_t j = 0; j < threads; j++)
      thread_list[j].join();
    thread_list.clear();
  }

#ifdef ENABLE_RESIZE
  iceberg_end(&table);
#endif
  t2 = high_resolution_clock::now();

  //	exit(0);

  if (!is_benchmark) {
    printf("Increments: %f\n", N / elapsed(t1, t2));

    printf("Load factor: %f\n", iceberg_load_factor(&table));
    printf("Number level 1 inserts: %ld\n", lv1_balls(&table));
    printf("Number level 2 inserts: %ld\n", lv2_balls(&table));
    printf("Number level 3 inserts: %ld\n", lv3_balls(&table));
    printf("Total inserts: %ld\n", tot_balls(&table));
  }

  // for(uint64_t i = 0; i < table.metadata.nblocks; ++i) {
  //   max_size = std::max(max_size, table.metadata.lv3_sizes[0][i]);
  //   sum_sizes += table.metadata.lv3_sizes[0][i];
  // }

  if (!is_benchmark) {
    printf("REFCOUNT DECREMENTS\n");
  }

  for(uint64_t i = 0; i < splits; ++i) {
    for(uint64_t j = 0; j < threads; j++)
      thread_list.emplace_back(do_removals, j, in_key_ptrs, (i * threads + j) * size, size, false);
    for(uint64_t j = 0; j < threads; j++)
      thread_list[j].join();
    thread_list.clear();
  }
  if (!is_benchmark) {
    printf("Decrements: %f /sec\n", N / elapsed(t1, t2));
  }

  t1 = high_resolution_clock::now();
  printf("\nQUERIES AFTER DECREMENTS\n");
  std::shuffle(&in_key_ptrs[0], &in_key_ptrs[N], g);

  for(uint64_t i = 0; i < threads; ++i)
    thread_list.emplace_back(do_queries, i, in_key_ptrs, i * (N / threads), N / threads, true);
  for(uint64_t i = 0; i < threads; ++i)
    thread_list[i].join();

  t2 = high_resolution_clock::now();
  if (!is_benchmark) {
    printf("Positive queries: %f /sec\n", N / elapsed(t1, t2));
  }
  thread_list.clear();

  if (!is_benchmark) {
    printf("\nREMOVALS\n");
  }

  uint64_t num_removed = N / 2 / threads * threads;
  char **removed = in_key_ptrs;
  char **non_removed = removed + num_removed;

  shuffle(&removed[0], &removed[num_removed], g);
  shuffle(&non_removed[0], &non_removed[N - num_removed], g);

  t1 = high_resolution_clock::now();

  for(uint64_t i = 0; i < threads; ++i)
    thread_list.emplace_back(do_removals, i, removed, i * (N / 2 / threads), N / 2 / threads, true);
  for(uint64_t i = 0; i < threads; ++i)
    thread_list[i].join();

  t2 = high_resolution_clock::now();
  //double removal_throughput = num_removed / elapsed(t1, t2);
  if (!is_benchmark) {
    printf("Removals: %f /sec\n", num_removed / elapsed(t1, t2));
    printf("Load factor: %f\n", iceberg_load_factor(&table));
  }
  thread_list.clear();

  shuffle(&removed[0], &removed[num_removed], g);

  t1 = high_resolution_clock::now();

  if (is_benchmark) {
    //printf("%f %f %f %f\n", insert_throughput, negative_throughput, positive_throughput, removal_throughput);
    return 0;
  }

  for(uint64_t i = 0; i < threads; ++i)
    thread_list.emplace_back(do_queries, i, removed, i * (N / 2 / threads), N / 2 / threads, false);
  for(uint64_t i = 0; i < threads; ++i)
    thread_list[i].join();

  t2 = high_resolution_clock::now();
  printf("Negative queries after removals: %f /sec\n", num_removed / elapsed(t1, t2));
  thread_list.clear();

  t1 = high_resolution_clock::now();

  for(uint64_t i = 0; i < threads; ++i)
    thread_list.emplace_back(do_queries, i, non_removed, i * (N / 2 / threads), N / 2 / threads, true);
  for(uint64_t i = 0; i < threads; ++i)
    thread_list[i].join();

  t2 = high_resolution_clock::now();
  printf("Positive queries after removals: %f /sec\n", num_removed / elapsed(t1, t2));
  thread_list.clear();

#if PMEM
  iceberg_dismount(&table);
  t1 = high_resolution_clock::now();
  iceberg_mount(&table, tbits, 0);
  t2 = high_resolution_clock::now();
  printf("throughput: mount: %f ops/sec\n", N / 2 / elapsed(t1, t2));
#endif

#if 0
  // insert removed items again
  for(uint64_t i = 0; i < threads; i++)
    thread_list.emplace_back(do_inserts, i, removed, in_values, i * (N / 2 / threads), N / 2 / threads);
  for(uint64_t i = 0; i < threads; i++)
    thread_list[i].join();
  thread_list.clear();
  printf("Load factor after re-insertion: %f\n", iceberg_load_factor(&table));

  if (!is_benchmark)
    printf("\nMIXED WORKLOAD, HIGH LOAD FACTOR\n");

  uint64_t mixed_N = N / 2;
  uint64_t *m_keys = (uint64_t *)malloc(mixed_N * sizeof(uint64_t));
  if(!m_keys) {
    printf("Malloc m_keys failed\n");
    exit(0);
  }
  uint64_t *m_vals = (uint64_t *)malloc(mixed_N * sizeof(uint64_t));
  if(!m_vals) {
    printf("Malloc m_vals failed\n");
    exit(0);
  }

  uint64_t half = mixed_N * sizeof(uint64_t) / 2;
  memcpy(m_keys, in_keys, half);
  memcpy(m_keys + half/8, out_keys, half);

  memcpy(m_vals, in_values, half);
  memcpy(m_vals + half/8, out_values, half);

  shuffle(&m_keys[0], &m_keys[mixed_N], g);

  t1 = high_resolution_clock::now();

  for(uint64_t i = 0; i < threads; ++i)
    thread_list.emplace_back(do_mixed, i, m_keys, m_vals, i * (mixed_N / threads), mixed_N / threads);
  for(uint64_t i = 0; i < threads; ++i)
    thread_list[i].join();

  t2 = high_resolution_clock::now();

  printf("Mixed operations at high load factor: %f /sec\n", 4 * mixed_N / elapsed(t1, t2));
  thread_list.clear();

  if (!is_benchmark) {
    max_size = sum_sizes = 0;
    for(uint64_t i = 0; i < table.metadata.nblocks; ++i) {
      max_size = std::max(max_size, table.metadata.lv3_sizes[0][i]);
      sum_sizes += table.metadata.lv3_sizes[0][i];
    }

    printf("Load factor: %f\n", iceberg_load_factor(&table));
    printf("Number level 1 inserts: %ld\n", lv1_balls(&table));
    printf("Number level 2 inserts: %ld\n", lv2_balls(&table));
    printf("Number level 3 inserts: %ld\n", lv3_balls(&table));
    printf("Total inserts: %ld\n", tot_balls(&table));

    printf("Average list size: %f\n", sum_sizes / (double)table.metadata.nblocks);
    printf("Max list size: %ld\n", max_size);
  }
#endif
}
