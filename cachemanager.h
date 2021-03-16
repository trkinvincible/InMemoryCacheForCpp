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

#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <string>
#include <memory>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/filtered.hpp>

#include "fileutility.h"
#include "utilstructs.h"
#include "config.h"

template<ALGO policy, typename Key, typename Value, template<class, class> class HashMapStrorage = std::unordered_map>
class ICacheInterfaceImp : public ICacheInterface<Key, Value>
{
protected:
    using key_type = typename HashMapStrorage<Key, Value>::key_type;
    using value_type = typename HashMapStrorage<Key, Value>::mapped_type;
    using kernel_parameter_cache_size = std::size_t;
    using CacheBufferType = typename FreeListContentType<policy, Key, Value>::type;
    using freebuffer_list_type = std::vector<std::atomic<CacheBufferType>>;
    using buffer_cache_index = signed int;

public:
    enum class BUFFER_STATUS: int8_t{

        // life cycle of cache buffer in free list
        FREE=0,
        BUSY,
        DIRTY,
        VALID,
    };

    explicit ICacheInterfaceImp(kernel_parameter_cache_size p_Maxsize, const std::string& p_FileName)
        :mNumberOfBuffers(p_Maxsize), mFileUtility(p_FileName){}

    /*
     * @brief       This Method will perform provided eviction algorithm
     *              # - find the buffer least frequently used
     *              # - flush the data to physical file if its status is DIRTY
     *              # - set the buffer status to FREE
     *
     * @return      buffer free list index which is free to use
    */
    buffer_cache_index GetNewBufferFromCache(){

        //this for loop is required because if CAS fail need to recompute all over again
        for(;;){

            buffer_cache_index least_frequently_used_buffer_index = mEvictionAlgo();
            if (least_frequently_used_buffer_index == INVALID_INDEX){

                //std::cout << "All buffers are BUSY" << std::endl;
                std::this_thread::sleep_for(30ms);
                continue;
            }
            assert(least_frequently_used_buffer_index < mNumberOfBuffers);

            std::atomic<CacheBufferType>& cache = mFreeList[least_frequently_used_buffer_index];
            CacheBufferType buf_to_evict = cache.load(std::memory_order_acquire);
            CacheBufferType old_cache = buf_to_evict;
            BUFFER_STATUS old_status = (BUFFER_STATUS)buf_to_evict.status;
            buf_to_evict.status = (short)BUFFER_STATUS::FREE;
            if(!cache.compare_exchange_strong(old_cache,buf_to_evict)){

                //some other thread must have already modified this buffer so recompute again
                std::this_thread::sleep_for(30ms);
                continue;
            }

            std::unique_lock lk(mHashMapMutex);
            auto itr = std::find_if(mCachedMemBlocks.begin(), mCachedMemBlocks.end(),
                                    [least_frequently_used_buffer_index](auto &item){

                return(item.second == least_frequently_used_buffer_index);
            });

            //check if buffer have cached data of some mem block but not yet flushed to physical file
            std::pair<key_type, std::string> dirtyCacheData{-1,"-1"};
            if (itr != mCachedMemBlocks.end()){

                if(old_status == BUFFER_STATUS::DIRTY){

                    //backup data to flush
                    dirtyCacheData = std::make_pair((*itr).first, std::to_string(old_cache.data));
                    //also erase the cache reference from hash map
                    mCachedMemBlocks.erase(itr);
                }
            }

            lk.unlock();

            /*
             * Its safe to get the buffer from free list because if the status was set BUSY previously
             * No writes will be done
            */
            auto &updated_cache = mFreeList[least_frequently_used_buffer_index];
            CacheBufferType updated_buf = updated_cache.load(std::memory_order_acquire);
            CacheBufferType new_buf;
            do{
                new_buf.data = 0;
                new_buf.status = (short)BUFFER_STATUS::BUSY;
                new_buf.frequency = 0;
            }while(!updated_cache.compare_exchange_weak(updated_buf,new_buf));

            // Flush dirty cache
            auto fl = [dirtyCacheData, this](){

                if (dirtyCacheData.first != -1)
                {
                    mFileUtility.InsertDataAtIndex(dirtyCacheData);
                }
            };
            std::thread t(fl);
            t.detach();

            return least_frequently_used_buffer_index;
        }
    }

    /*
     * @brief       This Method will get the value from cache if cache miss happens
     *              data is loaded from physical file and update the cache
     *
     * @return      data at specified line p_Position
    */
    virtual const bool Get(const key_type& p_Position, value_type& p_PositionValue) {

        bool cache_miss_happened = false;

        std::shared_lock lk(mHashMapMutex);
        for (;;){

            auto itr = mCachedMemBlocks.find(p_Position);
            if(itr == mCachedMemBlocks.end()){

                cache_miss_happened = true;
                lk.unlock();

                while(true){

                    buffer_cache_index new_buf_index = this->GetNewBufferFromCache();
                    assert(new_buf_index < this->mNumberOfBuffers);

                    std::unique_lock ulk(mHashMapMutex);

                    auto &new_cache = mFreeList.at(new_buf_index);
                    CacheBufferType new_buf = new_cache.load(std::memory_order_acquire);
                    CacheBufferType to_update_buf;
                    //read the value from file
                    p_PositionValue = to_update_buf.data = mFileUtility.ReadFileAtIndex(p_Position);
                    to_update_buf.status = (short)BUFFER_STATUS::DIRTY;
                    to_update_buf.frequency = 1;
                    if(!new_cache.compare_exchange_strong(new_buf,to_update_buf) == true){

                        //std::cout <<"buf consumed re-comute" << std::endl;
                        ulk.unlock();
                        std::this_thread::sleep_for(10ms);
                        continue;
                    }

                    // Update quick tracker
                    mCachedMemBlocks[p_Position] = new_buf_index;
                    break;
                }
            }else{

                // Atomic read no need explicit lock;
                if (!this->GetCachedValue(itr->second, p_PositionValue))
                    continue;
            }
            break;
        }

        return cache_miss_happened;
    }

    /*
     * @brief       This Method will put the value to the cache and update frequency
     *              if cache miss happens data is loaded from physical file and cache is updated
     *
     * @return      void
    */
    virtual void Put(const key_type& p_Position, const value_type& p_Value){

        std::shared_lock lk(mHashMapMutex);
        auto itr = mCachedMemBlocks.find(p_Position);
        if(itr == mCachedMemBlocks.end()){

            lk.unlock();

            while(true){

                buffer_cache_index new_buf_index = this->GetNewBufferFromCache();
                assert(new_buf_index < this->mNumberOfBuffers);

                std::unique_lock ulk(mHashMapMutex);

                auto &new_cache = mFreeList.at(new_buf_index);
                CacheBufferType new_buf = new_cache.load(std::memory_order_acquire);
                CacheBufferType to_update_buf;
                to_update_buf.data = p_Value;
                to_update_buf.status = (short)BUFFER_STATUS::DIRTY;
                to_update_buf.frequency = 1;
                if(!new_cache.compare_exchange_strong(new_buf,to_update_buf) == true){

                    //std::cout <<"buf consumed re-comute" << std::endl;
                    ulk.unlock();
                    std::this_thread::sleep_for(10ms);
                    continue;
                }

                // Update quick tracker
                mCachedMemBlocks[p_Position] = new_buf_index;
                break;
            }
        }else{

            // Atomic update no need explicit lock;
            this->SetCachedValue((*itr).second, p_Value);
        }
    }

    /*
     * @brief       This Method will periodically flush the dirty cache to storage
     *
     * @return      void
    */
    void Flush(){

        for (const auto& item : mFreeList | boost::adaptors::indexed(0)){

            CacheBufferType temp = item.value().load(std::memory_order_acquire);
            CacheBufferType temp_updated = temp;
            if(temp.status == (short)BUFFER_STATUS::DIRTY){

                temp.status = (short)BUFFER_STATUS::VALID;
                if(item.value().compare_exchange_strong(temp_updated,temp)){

                    std::lock_guard lk(mHashMapMutex);
                    auto itr = std::find_if(mCachedMemBlocks.begin(), mCachedMemBlocks.end(),
                                            [item](auto &i){

                        return(i.second == item.index());
                    });
                    if (itr != mCachedMemBlocks.end()){

                        //std::cout << "Inserting to file: " << (*itr).first << ","<< temp.data << std::endl;
                        mFileUtility.InsertDataAtIndex(std::make_pair((*itr).first, std::to_string(temp.data)));
                    }else{

                        //std::cout << "Buf taken up phew!!";
                    }
                }else{

                    //std::cout << "Buf taken up phew!!";
                }
            }
        }
    }

protected:
    const buffer_cache_index INVALID_INDEX = -1;
    kernel_parameter_cache_size mNumberOfBuffers;                        //buffer cache size - NBUF
    freebuffer_list_type mFreeList{mNumberOfBuffers};                    //cache buffers
    FileUtility mFileUtility;
    HashMapStrorage<int, int> mCachedMemBlocks;                          //quick tracker
    std::shared_mutex mHashMapMutex;
    std::function<buffer_cache_index()> mEvictionAlgo;
};

template<typename Key, typename Value, template<class, class> class HashMapStrorage=std::unordered_map>
class LFUImplementation : public ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>
{
    // Make dependent names for derived class
    using value_type = typename ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::value_type;
    using key_type = typename ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::key_type;
    using CacheBufferType = typename ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::CacheBufferType;
    using buffer_cache_index = typename ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::buffer_cache_index;
    using BUFFER_STATUS = typename ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::BUFFER_STATUS;
    using ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::mFreeList;
    using ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::INVALID_INDEX;
    using ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::mFileUtility;
    using ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::mEvictionAlgo;
public:

    explicit LFUImplementation(int max_size, const std::string& p_FileName)
        :ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>(max_size, p_FileName){

        ICacheInterfaceImp<ALGO::LFU, Key, Value, HashMapStrorage>::mEvictionAlgo = [this]()->buffer_cache_index{

                // When Get/Put happening do not judge free list
                buffer_cache_index least_frequently_used_buffer_index = INVALID_INDEX;
                short least_count = std::numeric_limits<short>::max();
                for (const auto& item : mFreeList | boost::adaptors::indexed(0)){

                    CacheBufferType temp = item.value().load(std::memory_order_acquire);
                    //some other thread must be trying to acquire the same buffer
                    if(temp.status != (short)BUFFER_STATUS::BUSY){

                        least_count = std::min(least_count,temp.frequency);
                        if(least_count == temp.frequency){

                            least_frequently_used_buffer_index = (buffer_cache_index)item.index();
                        }
                    }
                }

                return least_frequently_used_buffer_index;
        };
    }

    /*
     * @brief       this method will return value stored in buffer cache
     *              if the buffer has NOT been populated yet return false
     *
     * @return      true if data is valid false otherwise
     *
     * @pram        p_Index is index in free list to query, p_Value found
    */
    bool GetCachedValue(buffer_cache_index p_Index, value_type& p_Value){

        assert(p_Index < this->mNumberOfBuffers);
        bool ret_val;
        auto &old_val = mFreeList.at(p_Index);
        CacheBufferType temp = old_val.load(std::memory_order_acquire);
        //if status is free value in it must be out-dated
        if(temp.status == (short)BUFFER_STATUS::FREE){

            ret_val = false;
        }else{

            CacheBufferType new_buf;
            do{

                new_buf = temp;
                new_buf.frequency++;
            }while(!old_val.compare_exchange_weak(temp,new_buf));

            p_Value = temp.data;
            ret_val = true;
        }

        return ret_val;
    }

    /*
     * @brief       this method will update the cache buffer with updated value
     *
     * @return      void
     *
     * @pram        p_Index is index in free list to query, p_Value is value to set
    */
    void SetCachedValue(buffer_cache_index p_Index,const value_type& p_Value){

        assert(p_Index < this->mNumberOfBuffers);
        auto &old_val = mFreeList.at(p_Index);
        CacheBufferType temp = old_val.load(std::memory_order_acquire);

        // if BUSY cache is waiting to be over-written also stale data was flushed to file.
        if(temp.status != (short)BUFFER_STATUS::BUSY){

            CacheBufferType new_buf;
            do{

                new_buf = temp;
                new_buf.data = p_Value;
                new_buf.frequency++;
                new_buf.status = (short)BUFFER_STATUS::DIRTY;
            }while(!old_val.compare_exchange_weak(temp,new_buf));
        }
    }

public:


    static constexpr auto mCacheBufType = ALGO::LFU;
};

template<typename Key, typename Value, template<class, class> class HashMapStrorage=std::unordered_map>
class CacheManager : public std::enable_shared_from_this<CacheManager<Key, Value, HashMapStrorage>>
{
    using cache_impl_type = std::unique_ptr<ICacheInterface<Key, Value>>;
    using self_type = CacheManager<Key, Value, HashMapStrorage>;
    using self_type_ptr = std::shared_ptr<self_type>;
    using kernel_parameter_time_seconds = std::chrono::seconds;

public:
    using key_type = Key;
    using value_type = Value;

    CacheManager(const cache_config& p_Config) noexcept
        :mCacheConfig(p_Config){

        mCacheTimeOut = std::chrono::seconds(mCacheConfig.data().cache_timeout);
        ALGO s = (mCacheConfig.data().stratergy == 0 ? ALGO::LFU : ALGO::LFU);
        try{

            setStratergy(s, mCacheConfig.data().cache_size);
        }
        catch(...){

            std::cout << "cache manager failed retry..";
        }
    }
    CacheManager(const CacheManager &rhs) = delete;
    CacheManager& operator=(const CacheManager &rhs) = delete;

    ~CacheManager(){

        /*
         * Exit the thread flusing cache to file
         * memory mapped file will be unmapped once FileUtility object gets deleted
        */
        mDone.store(true, std::memory_order_release);
    }

    operator bool(){

        // Without settings Algorithm CacheManager is Invalid
        return (mImplementor != nullptr);
    }

    self_type_ptr Self(){

        return this->shared_from_this();
    }

    const bool Get(const Key& p_Key, Value& p_Value){

        return mImplementor->Get(p_Key, p_Value);
    }

    void Put(const Key& p_Key, const Value& p_Value){

        mImplementor->Put(p_Key, p_Value);
    }

    const cache_config& getConfig(){

        return mCacheConfig;
    }

private:
    void setStratergy(ALGO p_Policy, int p_MaxSize){

        switch(p_Policy){

            case ALGO::LFU:{

                mImplementor.reset(new LFUImplementation<Key, Value, HashMapStrorage>(p_MaxSize, mCacheConfig.data().items_file_name));
            }
            break;
            default:{
                assert(false);
            }
        }

        auto func_flush_cache = [this](){

            while(!mDone.load(std::memory_order_relaxed)){

                mImplementor->Flush();
                std::this_thread::sleep_for(mCacheTimeOut);
            }
            mImplementor->Flush();
        };
        std::thread thread_cache_invalidator(func_flush_cache);
        thread_cache_invalidator.detach();
    }

private:
    std::atomic_bool mDone = false;
    cache_impl_type mImplementor;
    const cache_config& mCacheConfig;
    kernel_parameter_time_seconds mCacheTimeOut;        //buffer cache flush timeout - BDFLUSHR
    kernel_parameter_time_seconds mDelayedWriteTimeout; //delayed write flush timeout - NAUTOUP
};

#endif // CACHEMANAGER_H
