//"MIT License

//Copyright (c) 2021 Radhakrishnan Thangavel

//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:

//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.

//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

// Author: Radhakrishnan Thangavel (https://github.com/trkinvincible)

#include "cachemanager.h"
#include <gtest/gtest.h>
#include <unordered_map>

TEST(CacheManagerTest, PutGetCache) {

    // Int data
    LFUImplementation<short, uint, std::unordered_map> imp_int(4,"../InMemoryCacheForCpp/res/item_file.txt");
    imp_int.Put(1, 1000);
    uint v;
    imp_int.Get(1, v);
    ASSERT_EQ(1000, v);

    // Float data
    LFUImplementation<short, float, std::unordered_map> imp_float(4,"../InMemoryCacheForCpp/res/item_file.txt");
    imp_float.Put(10, 1000.1);
    float vf;
    imp_float.Get(10, vf);
    ASSERT_FLOAT_EQ(1000.1, vf);

    // Signed Int data
    LFUImplementation<short, int, std::unordered_map> imp_signed_int(4,"../InMemoryCacheForCpp/res/item_file.txt");
    imp_signed_int.Put(100, -1000);
    int vsi;
    imp_signed_int.Get(100, vsi);
    ASSERT_EQ(-1000, vsi);

    // Signed Int data
    LFUImplementation<short, int, std::unordered_map> imp_signed_int_truc1(4,"../InMemoryCacheForCpp/res/item_file.txt");
    imp_signed_int_truc1.Put(1000, -1000);
    imp_signed_int_truc1.Put(1000, -111);
    int vsi_truc1;
    imp_signed_int_truc1.Get(1000, vsi_truc1);
    ASSERT_EQ(-111, vsi_truc1);

    // Signed Int data
    LFUImplementation<short, int, std::unordered_map> imp_signed_int_truc2(4,"../InMemoryCacheForCpp/res/item_file.txt");
    imp_signed_int_truc2.Put(1000, -111);
    imp_signed_int_truc2.Put(1000, -1111111);
    int vsi_truc2;
    imp_signed_int_truc2.Get(1000, vsi_truc2);
    ASSERT_EQ(-1111111, vsi_truc2);
}

TEST(CacheManagerTest, CacheEvictionTest) {

    LFUImplementation<short, int, std::unordered_map> imp(4,"../InMemoryCacheForCpp/res/item_file.txt");
    bool r; int v;

    imp.Put(1, 1111);
    imp.Put(2, 2222);
    imp.Put(3, 3333);
    imp.Put(4, 4444);

    imp.Get(1, v); // ++ frequency for 1
    imp.Get(2, v); // ++ frequency for 2
    imp.Get(3, v); // ++ frequency for 3

    imp.Put(5, 5555); // 4 have frequency 1 rest all 2 so evict 4 and replace 5

    // Must evict 4 now so must return disk which is true (cache missed)
    r = imp.Get(4, v);

    const bool ifDataTakenFromDisk_CacheMiss = true;
    ASSERT_EQ(v, 4444);
    ASSERT_EQ(r, ifDataTakenFromDisk_CacheMiss);
}

int RunGTest(int argc, char **argv) {

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
