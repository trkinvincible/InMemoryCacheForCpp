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

#pragma once

#include <iostream>
#include <thread>
#include <future>
#include <regex>

#include "boost/algorithm/string.hpp"
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include "command.h"
#include "fileutility.h"

extern std::shared_mutex gCheckProgramExit;
extern std::condition_variable_any gCheckProgramExitConVar;

template<typename DATA>
class Writer : public Command
{
    using key_type = typename CacheManager<short, DATA>::key_type;
    using value_type = typename CacheManager<short, DATA>::value_type;

public:
    Writer(auto cache_manager)
        :mCacheManager(cache_manager){}

    virtual ~Writer(){

        std::cout << "Writer Delete..: "<< mCacheManager.use_count() << std::endl;
    }

    void execute()
    {
        const std::string& filename = mCacheManager->getConfig().data().writer_file_name;
        std::vector<std::future<std::string>> vec_future;
        const boost::interprocess::file_mapping input_file_mapped(filename.c_str(),boost::interprocess::read_only);
        boost::interprocess::mapped_region mapped_region(input_file_mapped,boost::interprocess::read_only);
        try{

            const char* start_address = reinterpret_cast<const char*>(mapped_region.get_address());
            /*
             * Using std::string_view to gurantee "Zero Copying" to yield better performance
            */
            std::string_view input_text(start_address);
            std::regex r("^.*#");
            /*
             * raison d'être:
             * linux with fair scheduler will not let thread priority numbers
             * So find out high clock speed cores and assign high priority task
             * trick is divide task to run only in designated cores to avoid posibility
             * of one task blocking other for longer period
            */
            std::once_flag flg;
            cpu_set_t cpuset;
            std::call_once(flg, [&cpuset](){

                CPU_ZERO(&cpuset);
                /*
                 * Do hyper-threading for low latency as my i7 can support already with 4 core per processor.
                 * CPU affinity:
                 *  0,2, etc.. for writer
                 *  1,3, etc.. fostartConsumerr reader
                */
                for(int cpu_index = 0; cpu_index < mMaxThreadAllowed; cpu_index += 2){

                    CPU_SET(cpu_index,&cpuset);
            }});
            for(std::cregex_iterator i = std::cregex_iterator(input_text.begin(), input_text.end(), r);
                i != std::cregex_iterator();
                ++i)
            {
                const std::cmatch m = *i;
                /*
                 * When spawning (no. of threads > available computing units) it will do more harm as
                 * frequent context switching is costly
                */
                while(static_cast<int>(mCurrThreadsAlive.load(std::memory_order_relaxed)) >= mMaxThreadAllowed){

                    std::this_thread::sleep_for(10ms);
                }

                std::packaged_task<std::string(std::string)> task(std::bind(&Writer::writeToOutput,this,
                                                                            std::placeholders::_1));
                vec_future.push_back(std::move(task.get_future()));
                std::string f = m.str();
                std::ifstream test(f);
                if (!test)
                {
                    //std::cout << "The file doesn't exist" << std::endl;
                    continue;
                }
                std::thread t(std::move(task), f);
                int rc = pthread_setaffinity_np(t.native_handle(),sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {

                    std::cerr << "Error calling pthread_setaffinity_np: " << rc << std::endl;
                }
                Command::mCurrThreadsAlive.fetch_add(1, std::memory_order_acq_rel);
                std::cout << __PRETTY_FUNCTION__ << "   mCurrThreadsAlive: " << mCurrThreadsAlive.load() << std::endl;
                t.detach();
            }
        }catch(std::exception &exp){

            std::cout << "missing writer_file exp: " << exp.what() << std::endl;
        }
    }
    /*
     * @brief       This method will read the writer file for line number and data to write
     *              writes the data to cache eventually cache manager will flush the changes to physical file
     *
     * @param1      filename to read line number and data
    */
    std::string writeToOutput(std::string filename)
    {
        std::shared_lock lk(gCheckProgramExit);
        try{

            const boost::interprocess::file_mapping input_file_mapped(filename.c_str(),boost::interprocess::read_only);
            boost::interprocess::mapped_region mapped_region(input_file_mapped,boost::interprocess::read_only);
            const char* start_address = reinterpret_cast<const char*>(mapped_region.get_address());
            std::string input_text(start_address);

            std::regex r(R"([^.*\n]+)");
            for(std::sregex_iterator i = std::sregex_iterator(input_text.begin(), input_text.end(), r);
                i != std::sregex_iterator();
                ++i)
            {
                const std::smatch m = *i;
                const std::string temp = m.str();
                if (temp.length()){

                    std::vector<std::string> values;
                    boost::algorithm::split(values, temp, boost::is_any_of(" "));
                    assert(values.size() >= 2);
                    if(values.size() >= 2){

                        try{
                            key_type key = boost::lexical_cast<key_type>(values[0]);
                            value_type value = boost::lexical_cast<value_type>(values[1]);
                            //std::cout << __FUNCTION__ << "  Check Point: 1" << std::endl;
                            mCacheManager->Put(key, value);
                            //std::cout << __FUNCTION__ << "  Check Point: 10" << std::endl;

                        }catch(std::exception &exp){

                            std::cout << exp.what() << std::endl;
                            continue;
                        }
                    }
                }
            }
        }catch(std::exception &exp){

            lk.unlock();
            std::cout << exp.what() << std::endl;
        }

        Command::mCurrThreadsAlive.fetch_sub(1, std::memory_order_acq_rel);
        std::cout << "Completed : " << filename << std::endl;
        return "success";
    }

private:
    std::shared_ptr<CacheManager<short, DATA>> mCacheManager;
};
