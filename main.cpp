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
std::atomic_int Command::mcurrThreadsAlive{0};

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
            ("cache.reader_file", boost::program_options::value<std::string>(&d.reader_file_name)->default_value("./res/reader_file.txt"), "reader file path+name")
            ("cache.writer_file", boost::program_options::value<std::string>(&d.writer_file_name)->default_value("./res/writer_file.txt"), "writer file path+name")
            ("cache.items_file", boost::program_options::value<std::string>(&d.items_file_name)->default_value("./res/item_file.txt"), "item file to write to")
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
    std::cout << config;

    /*
     * Though a static variable is just enough for singleton used this pattern for below reasons
     * #1 - not all compiler(older ones) gurantee singleton on local static variable
     * #2 - when memory foot print is more good to keep in heap than in global segment with limited space
     *
     * quiet difficult to design a perfect multi-threaded singleton class for spurious wakeups
     * like "double-checked locking optimization"
     * so do eager initialization when possible for better data sync
     */

    if (!config.data().run_test){

        // using redis-client key/value storage(opensource) or boost::multi_index_container will give  better performance
        auto cache_manager = std::make_shared<CacheManager<uint, float, std::unordered_map>>(config);

        auto start = std::chrono::high_resolution_clock::now();

        auto w = std::make_unique<Writer<float>>(cache_manager->Self());
        auto r = std::make_unique<Reader<float>>(cache_manager->Self());
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
        static int c = w->mcurrThreadsAlive;
        gCheckProgramExitConVar.wait_for(locker, 5s, [c]()mutable {

            c--;
            return (!c);
        });
        locker.unlock();

        cache_manager->setProgramExit();

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
