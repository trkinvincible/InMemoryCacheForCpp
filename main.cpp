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

#include <iostream>
#include <shared_mutex>
#include <chrono>
#include <unordered_map>
#include <memory>

#include "config.h"
#include "fileutility.h"
#include "cachemanager.h"
#include "writer.h"
#include "reader.h"
#include "gtest.h"

unsigned int Command::mMaxThreadAllowed = 1;
std::atomic_int Command::mCurrThreadsAlive{0};

std::shared_mutex gCheckProgramExit;
std::condition_variable_any gCheckProgramExitConVar;
using namespace std::chrono_literals;

int main(int argc, char *argv[])
{
    //Input: cache <size_of_cache> <reader_file> <writer_file> <items_file>
    cache_config config([](cache_config_data &d, boost::program_options::options_description &desc){
        desc.add_options()
            //("cache.size_of_cache", boost::program_options::value<std::string>(&d.log_file_name)->required(), "cache size available")
            ("cache.size_of_cache", boost::program_options::value<short>(&d.cache_size)->default_value(4), "cache size available")
            ("cache.reader_file", boost::program_options::value<std::string>(&d.reader_file_name)->default_value("../InMemoryCacheForCpp/res/reader_file.txt"), "reader file path+name")
            ("cache.writer_file", boost::program_options::value<std::string>(&d.writer_file_name)->default_value("../InMemoryCacheForCpp/res/writer_file.txt"), "writer file path+name")
            ("cache.items_file", boost::program_options::value<std::string>(&d.items_file_name)->default_value("../InMemoryCacheForCpp/res/item_file.txt"), "item file to write to")
            ("cache.stratergy", boost::program_options::value<short>(&d.stratergy)->default_value(0), "Choose Cache Algorithm LFU: 0, LRU: 1")
            ("cache.cache_timeout", boost::program_options::value<int>(&d.cache_timeout)->default_value(5), "Choose Cache Algorithm LFU: 0, LRU: 1")
            ("cache.run_test", boost::program_options::value<short>(&d.run_test)->default_value(0), "choose to run test");
    });

    try {

        config.parse(argc, argv);
    }
    catch(std::exception const& e) {

        std::cout << e.what();
        return 0;
    }
    //std::cout << config;

    if (!config.data().run_test){

        // using redis-client key/value storage(opensource) or boost::multi_index_container will give  better performance
        auto cache_manager = std::make_shared<CacheManager<short, double, std::unordered_map>>(config);

        auto start = std::chrono::high_resolution_clock::now();

        auto w = std::make_unique<Writer<double>>(cache_manager->Self());
        auto r = std::make_unique<Reader<double>>(cache_manager->Self());
        auto func_writer = [&w](){

            std::cout << "Excecuting Writer.." << std::endl;
            w->execute();
        };
        std::thread wt(func_writer);
        auto func_reader = [&r](){

            std::cout << "Excecuting Reader.." << std::endl;
            r->execute();
        };
        std::thread rt(func_reader);

        rt.join();
        wt.join();

        std::unique_lock locker(gCheckProgramExit);
        gCheckProgramExitConVar.wait_for(locker, 10s, []()mutable {

            int c = Command::mCurrThreadsAlive.load(std::memory_order_acquire);
            return (!c);
        });

        w.reset();
        r.reset();

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> diff = end-start;
        std::cout << "Time to Complete: " << diff.count() << std::endl;
    }else{

        RunGTest(argc, argv);
    }

    return 0;
}
