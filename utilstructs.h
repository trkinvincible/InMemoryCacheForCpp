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

#ifndef UTIL_HPP
#define UTIL_HPP

using namespace std::chrono_literals;

enum class ALGO: int8_t{

    LFU = 0,
    MAX_POLICY
};

/*
 * These struct is designed to be atomic. every object will be placed in different cache line
 * do not change the order of elements std::atomic<CacheBuffer>{}.is_lock_free() must be true always
*/
template<typename Key = short, typename Value = signed int>
struct alignas (64) LFUCacheBuffer{

    short frequency;
    short status;
    Value data;
    //short counter_4_aba;
};

template<typename Key, typename Value>
class ICacheInterface {
public:

    virtual bool GetCachedValue(int p_Index, Value& p_Value) = 0;
    virtual void SetCachedValue(int p_Index,const Value& p_Value) = 0;
    virtual const bool Get(const Key& p_Position, Value& p_PositionValue) = 0;
    virtual void Put(const Key& p_Position, const Value& p_Value)  = 0;
    virtual void Flush() = 0;
};

template<ALGO policy, typename Key, typename Value> struct FreeListContentType { using type = LFUCacheBuffer<Key, Value>; };
template<typename Key, typename Value> struct FreeListContentType<ALGO::LFU, Key, Value> { using type = LFUCacheBuffer<Key, Value>; };


#endif /* UTIL_HPP */

