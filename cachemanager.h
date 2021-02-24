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
#include "config.h"

template<typename Key=short, typename Value=signed int, template<class, class> class HashMapStrorage=std::unordered_map>
class ICacheInterface
{
protected:
    using line_number = typename HashMapStrorage<Key, Value>::key_type;
    using value_type = typename HashMapStrorage<Key, Value>::mapped_type;
    using kernel_parameter_cache_size = std::size_t;
    //template <typename T>
    //using freebuffer_list_type = typename std::aligned_storage<sizeof(std::atomic<T>), alignof(std::atomic<T>)>::type;
    template <typename T>
    using freebuffer_list_type = std::vector<std::atomic<T>>;
    using buffer_cache_index = signed int;

public:
    enum class ALGO: int8_t{

        LFU = 0,
        MAX_POLICY
    };

    enum class BUFFER_STATUS: int8_t{

        FREE=0,
        LOCKED=1,
        VALID,
        DIRTY,
        BUSY
    };

    explicit ICacheInterface(kernel_parameter_cache_size p_Maxsize, const std::string& p_FileName)
        :mNumberOfBuffers(p_Maxsize), mFileUtility(p_FileName){}

    virtual const bool Get(const line_number& p_Position, value_type& p_PositionValue) = 0 ;

    virtual void Put(const line_number& p_Position, const value_type& p_Value) = 0;

    void Flush(){

        for (const auto& item : mFreeList | boost::adaptors::indexed(0)){

            CacheBuffer temp = item.value().load(std::memory_order_acquire);
            CacheBuffer temp_updated = temp;
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
    /*
     * This struct is designed to be atomic. every object will be placed in different cache line
     * do not change the order of elements as padding alignment will change
     * std::atomic<CacheBuffer>{}.is_lock_free() must be true always
    */
    struct alignas (64) CacheBuffer{

        short frequency;
        short status;
        value_type data;
        //short counter_4_aba;
    };
    const buffer_cache_index INVALID_INDEX = -1;
    kernel_parameter_cache_size mNumberOfBuffers;                        //buffer cache size - NBUF
    freebuffer_list_type<CacheBuffer> mFreeList{mNumberOfBuffers};       //cache buffers
    FileUtility mFileUtility;
    HashMapStrorage<int, int> mCachedMemBlocks;                          //quick tracker
    std::mutex mHashMapMutex;
    std::function<buffer_cache_index()> mEvictionAlgo;

    /*
     * @brief       this method will return the index of VALID buffer cache if not return INVALID_INDEX
     *              and copy the value stored to ref parameter.
     *              its guranteed when hash map is being modified read will be halted
     *              means index returned will not be some other line numbers's cache ever
     *
     * @pram        line_number, &value
     * @return      index of free list cache
    */
    buffer_cache_index FindCachedIndex(const line_number& p_LineNumber, value_type& p_Value){

        buffer_cache_index ret_val = INVALID_INDEX;
        std::unique_lock lk(mHashMapMutex, std::defer_lock);
        auto itr = mCachedMemBlocks.find(p_LineNumber);
        if(itr != mCachedMemBlocks.end()){

            if (p_Value == -1){

                value_type v;
                if(this->GetCachedValue(itr->second, v)){

                    ret_val = itr->second;
                    p_Value = v;
                }
            }else{

                lk.lock();
                if(this->SetCachedValue(itr->second, p_Value)){

                    ret_val = itr->second;
                }
            }
        }

        return ret_val;
    }

    /*
     * @brief       this method will update the hash map
     *              its guranteed multiple updated are serialized
     *
     * @return      void
    */
    void UpdateCachedIndex(const line_number& p_LineNumber,const buffer_cache_index& p_Position){

        //when hash map is being modified write is prohibited
        std::lock_guard lk(mHashMapMutex);
        mCachedMemBlocks[p_LineNumber] = p_Position;
    }

    /*
     * @brief       this method will return value stored in buffer cache
     *              if the buffer has NOT been populated yet return false
     *
     * @return      true if data is valid false otherwise
     *
     * @pram        p_Index is index in free list to query, p_Value is found
    */
    bool GetCachedValue(buffer_cache_index p_Index, value_type& p_Value){

        bool ret_val;
        assert(p_Index < mNumberOfBuffers);
        auto &old_val = mFreeList.at(p_Index);
        CacheBuffer temp = mFreeList.at(p_Index).load(std::memory_order_acquire);
        //if status is free value in it must be out-dated
        if(temp.status == (short)BUFFER_STATUS::FREE){

            ret_val = false;
        }else{

            CacheBuffer new_buf = temp;
            do{
                new_buf.frequency++;
            }while(!old_val.compare_exchange_weak(temp,new_buf));
            p_Value = temp.data;
            ret_val = true;
        }
        return ret_val;
    }

    /*
     * @brief       this method will update the cache buffer with updated value
     *              if the buffer is in the process of being evicted no action will be performed
     *
     * @return      if data exchange/update is sucessfull returns true false otherwise
    */
    bool SetCachedValue(buffer_cache_index p_Index,const value_type& p_Value){

        bool ret_val = true;
        assert(p_Index < mNumberOfBuffers);
        auto &old_val = mFreeList.at(p_Index);
        CacheBuffer temp = mFreeList.at(p_Index).load(std::memory_order_acquire);

        //if status is BUSY buffer must have been in the process of eviction means it might be some other line's data
        if(temp.status == (short)BUFFER_STATUS::BUSY){

            ret_val = false;
        }else{
            CacheBuffer new_buf = temp;
            do{
                new_buf.data = p_Value;
                new_buf.frequency++;
                new_buf.status = (short)BUFFER_STATUS::DIRTY;
            }while(!old_val.compare_exchange_weak(temp,new_buf));
        }
        return ret_val;
    }

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
                continue;
            }
            assert(least_frequently_used_buffer_index < mNumberOfBuffers);
            std::atomic<CacheBuffer> &cache = mFreeList.at(least_frequently_used_buffer_index);

            CacheBuffer buf_to_evict = cache.load(std::memory_order_acquire);
            CacheBuffer old_cache = buf_to_evict;
            BUFFER_STATUS old_status = (BUFFER_STATUS)buf_to_evict.status;
            buf_to_evict.status = (short)BUFFER_STATUS::BUSY;
            if(!cache.compare_exchange_strong(old_cache,buf_to_evict)){

                //some other thread must have already modified this buffer so recompute again
                continue;
            }
            //check if buffer have cached data of some mem block but not yet flushed to physical file
            std::pair<line_number, std::string> dirtyCacheData{-1,"-1"};
            if(old_status == BUFFER_STATUS::DIRTY){

                //flush to file
                std::lock_guard lk(mHashMapMutex);
                auto itr = std::find_if(mCachedMemBlocks.begin(), mCachedMemBlocks.end(), [least_frequently_used_buffer_index](auto &item){

                    return(item.second == least_frequently_used_buffer_index);
                });
                if (itr != mCachedMemBlocks.end()){

                    dirtyCacheData = std::make_pair((*itr).first, std::to_string(old_cache.data));
                }
            }
            //also erase the cache reference from hash map
            std::unique_lock lk(mHashMapMutex);
            try{

                auto itr = std::find_if(mCachedMemBlocks.begin(), mCachedMemBlocks.end(),
                                        [least_frequently_used_buffer_index](auto &item){

                    return(item.second == least_frequently_used_buffer_index);
                });
                if(itr != mCachedMemBlocks.end()){

                    mCachedMemBlocks.erase(itr);
                }

            }catch(std::exception &exp){

                lk.unlock();
                //std::cout << "why did this happen: " << exp.what() << std::endl;
            }
            lk.unlock();
            /*
             * Its safe to get the buffer from free list because if the status was set BUSY previously
             * No writes will be done
            */
            assert(least_frequently_used_buffer_index < mNumberOfBuffers);
            auto &updated_cache = mFreeList.at(least_frequently_used_buffer_index);
            CacheBuffer updated_buf = updated_cache.load(std::memory_order_acquire);
            CacheBuffer new_buf;
            do{
                new_buf.data = 0;
                new_buf.status = (short)BUFFER_STATUS::FREE;
                new_buf.frequency = 0;
            }while(!updated_cache.compare_exchange_weak(updated_buf,new_buf));

            // Flush dirty cache
            if (dirtyCacheData.first != -1)
            {
                mFileUtility.InsertDataAtIndex(dirtyCacheData);
            }

            return least_frequently_used_buffer_index;
            //std::cout << __FUNCTION__ << "Check Point: mCacheManager->Get()->3" << std::endl;
        }
    }
};

template<typename Key=short, typename Value=signed int, template<class, class> class HashMapStrorage=std::unordered_map>
class LFUImplementation : public ICacheInterface<Key, Value, HashMapStrorage>
{
    // Make dependent names for derived class
    using value_type = typename ICacheInterface<Key, Value, HashMapStrorage>::value_type;
    using line_number = typename ICacheInterface<Key, Value, HashMapStrorage>::line_number;
    using buffer_cache_index = typename ICacheInterface<Key, Value, HashMapStrorage>::buffer_cache_index;
    using CacheBuffer = typename ICacheInterface<Key, Value, HashMapStrorage>::CacheBuffer;
    using ALGO = typename ICacheInterface<Key, Value, HashMapStrorage>::ALGO;
    using BUFFER_STATUS = typename ICacheInterface<Key, Value, HashMapStrorage>::BUFFER_STATUS;
    using ICacheInterface<Key, Value, HashMapStrorage>::mFreeList;
    using ICacheInterface<Key, Value, HashMapStrorage>::INVALID_INDEX;
    using ICacheInterface<Key, Value, HashMapStrorage>::mFileUtility;
    using ICacheInterface<Key, Value, HashMapStrorage>::mEvictionAlgo;

public:
    explicit LFUImplementation(int max_size, const std::string& p_FileName)
        :ICacheInterface<Key, Value, HashMapStrorage>(max_size, p_FileName){

        ICacheInterface<Key, Value, HashMapStrorage>::mEvictionAlgo = [this]()->buffer_cache_index{

                // When Get/Put happening do not judge free list
                std::unique_lock lk(this->mHashMapMutex);
                buffer_cache_index least_frequently_used_buffer_index = INVALID_INDEX;
                short least_count = std::numeric_limits<short>::max();
                for (const auto& item : mFreeList | boost::adaptors::indexed(0)){

                    CacheBuffer temp = item.value().load(std::memory_order_acquire);
                    //some other thread must be trying to acquire the same buffer
                    if(temp.status != (short)BUFFER_STATUS::BUSY){

                        least_count = std::min(least_count,temp.frequency);
                        if(least_count == temp.frequency){

                            least_frequently_used_buffer_index = (buffer_cache_index)item.index();
                        }
                    }
                }
                lk.unlock();
                return least_frequently_used_buffer_index;
        };
    }

    /*
     * @brief       This Method will get the value from cache if cache miss happens
     *              data is loaded from physical file and update the cache
     *
     * @return      data at specified line p_Position
    */
    const bool Get(const line_number& p_Position, value_type& p_PositionValue) override{

        bool cache_miss_happened = false;
        for(;;){

            //first try to get from the cache
            p_PositionValue = -1;
            buffer_cache_index desired_cache_index = this->FindCachedIndex(p_Position, p_PositionValue);
            //std::cout << __FUNCTION__ << "Check Point: 2" << std::endl;
            //line number is not cached yet
            if(desired_cache_index == INVALID_INDEX){

                cache_miss_happened = true;
                for(;;){

                    buffer_cache_index new_buf_index = this->GetNewBufferFromCache();
                    //std::cout << __FUNCTION__ << "Check Point: 2.1" << std::endl;
                    assert(new_buf_index < this->mNumberOfBuffers);
                    auto &new_cache = mFreeList.at(new_buf_index);
                    CacheBuffer new_buf = new_cache.load(std::memory_order_acquire);
                    CacheBuffer to_update_buf;
                    p_PositionValue = to_update_buf.data = mFileUtility.ReadFileAtIndex(p_Position);//read the value from file
                    to_update_buf.status = (short)BUFFER_STATUS::DIRTY;
                    to_update_buf.frequency = 1;
                    if(!new_cache.compare_exchange_strong(new_buf,to_update_buf) == true){

                        //std::cout <<"buf consumed re-comute" << std::endl;
                        continue;
                    }
                    this->UpdateCachedIndex(p_Position,new_buf_index);
                    //std::cout << __FUNCTION__ << "Check Point: 2.2" << std::endl;
                    break;
                }
            }else{

                //line number has been already cached so return the data
                //std::cout << __FUNCTION__ << "Check Point: 3" << std::endl;
                break;
            }
        }
        //std::cout << __FUNCTION__ << "Check Point: 4" << std::endl;
        return cache_miss_happened;
    }

    /*
     * @brief       This Method will put the value to the cache and update frequency
     *              if cache miss happens data is loaded from physical file and cache is updated
     *
     * @return      void
    */
    void Put(const line_number& p_Position, const value_type& p_Value) override{

        for(;;){

            //first try to get from the cache
            value_type value = p_Value;
            buffer_cache_index desired_cache_index = this->FindCachedIndex(p_Position, value);
            //std::cout << __FUNCTION__ << "  Check Point: 2" << std::endl;
            //line number is not cached yet
            if(desired_cache_index == INVALID_INDEX){

                //run through the buffer cache list and find the least frequently used buffer

                buffer_cache_index new_buf_index = this->GetNewBufferFromCache();
                assert(new_buf_index < this->mNumberOfBuffers);
                auto &new_cache = mFreeList.at(new_buf_index);
                CacheBuffer new_buf = new_cache.load(std::memory_order_acquire);
                CacheBuffer to_update_buf;
                to_update_buf.data = p_Value;
                to_update_buf.status = (short)BUFFER_STATUS::DIRTY;
                to_update_buf.frequency = 1;
                if(!new_cache.compare_exchange_strong(new_buf,to_update_buf) == true){

                    //std::cout <<"buf consumed re-comute" << std::endl;
                    continue;
                }
                this->UpdateCachedIndex(p_Position, new_buf_index);
                break;
            }else{

                break;
            }
        }
    }
};

template<typename Key=short, typename Value=signed int, template<class, class> class HashMapStrorage=std::unordered_map>
class CacheManager : public std::enable_shared_from_this<CacheManager<Key, Value, HashMapStrorage>>
{
    using cache_impl_type = std::unique_ptr<ICacheInterface<Key, Value, HashMapStrorage>>;
    using self_type = CacheManager<Key, Value, HashMapStrorage>;
    using self_type_ptr = std::shared_ptr<self_type>;
    using ALGO = typename ICacheInterface<Key, Value, HashMapStrorage>::ALGO;
    using BUFFER_STATUS = typename ICacheInterface<Key, Value, HashMapStrorage>::BUFFER_STATUS;
    using kernel_parameter_time_seconds = std::chrono::seconds;

public:
    using key_type = Key;
    using value_type = Value;

    CacheManager(const cache_config& p_Config) noexcept
        :mCacheConfig(p_Config){

        mImplementor = nullptr;
        mCacheTimeOut = std::chrono::seconds(mCacheConfig.data().cache_timeout);
        ALGO s = (mCacheConfig.data().stratergy == 0 ? ALGO::LFU : ALGO::LFU);
        try{

            setStratergy(s, mCacheConfig.data().cache_size);
        }
        catch(...){

            //std::cout << "cache manager failed retry..";
        }
    }
    CacheManager(const CacheManager &rhs) = delete;
    CacheManager& operator=(const CacheManager &rhs) = delete;

    operator bool(){

        // Without settings Algorithm CacheManager is Invalid
        return (mImplementor != nullptr);
    }

    void setProgramExit(){

        /*
         * Exit the thread flusing cache to file
         * memory mapped file will be unmapped once FileUtility object gets deleted
        */
        mDone = true;
    }

    self_type_ptr Self(){

        return this->shared_from_this();
    }

    const Value Get(const Key& p_Key, Value& p_Value){

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

            while(!mDone){

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
    cache_impl_type mImplementor = nullptr;
    const cache_config& mCacheConfig;
    kernel_parameter_time_seconds mCacheTimeOut;        //buffer cache flush timeout - BDFLUSHR
    kernel_parameter_time_seconds mDelayedWriteTimeout; //delayed write flush timeout - NAUTOUP
};

#endif // CACHEMANAGER_H
