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

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <iostream>
#include <fstream>
#include <chrono>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

namespace po = boost::program_options;

template <typename DATA>
class config {
    using config_data_t = DATA;
    using add_options_type = std::function<void (config_data_t&, po::options_description&)>;
public:

    config(const add_options_type &add_options):
        add_options(add_options)
    {
        desc.add_options()
                ("help", "produce help")
                ("config", po::value<std::string>(&config_name)->default_value(config_name_default), "config file name")
                ;
    }
    config(const config&) = delete;

    void parse(int argc, char *argv[]) noexcept(false) {
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
        store(parsed, vm);
        notify(vm);

        if (vm.count("help")) {
            std::stringstream ss;
            ss << desc << std::endl;
            throw std::runtime_error(ss.str());
        }

        add_options(config_data, desc);

        std::ifstream file(config_name.c_str());
        if(!file) {
            std::stringstream ss;
            ss << "Failed to open config file " << config_name << std::endl;
            throw std::runtime_error(ss.str());
        }
        store(po::parse_command_line(argc, argv, desc), vm);
        store(po::parse_config_file(file, desc, true), vm);

        notify(vm);
    }

    template <typename T = std::string>
    auto &get(const char *needle) noexcept(false) {
        try {
            return vm[needle].template as<T>();
        }
        catch(const boost::bad_any_cast &) {
            std::stringstream ss;
            ss << "Get error <" <<  typeid(T).name() << ">(" << needle << ")"  << std::endl;
            throw std::runtime_error(ss.str());
        }
    }
    const config_data_t& data() const {
        return config_data;
    }
    template<typename DATA_TYPE>
    friend std::ostream& operator<<(std::ostream&, const config<DATA_TYPE>&);
private:
    const add_options_type add_options;
    static constexpr const char* config_name_default{"../InMemoryCacheForCpp/config.cfg"};
    std::string config_name;
    po::variables_map vm;
    po::options_description desc;
    config_data_t config_data;
};
template <typename DATA>
std::ostream& operator<<(std::ostream& s, const config<DATA>& c) {
    for (auto &it : c.vm) {
        s << it.first.c_str() << " ";
        auto& value = it.second.value();
        if (auto v = boost::any_cast<unsigned short>(&value)) {
            s << *v << std::endl;
        }
        else if (auto v = boost::any_cast<unsigned int>(&value)) {
            s << *v << std::endl;
        }
        else if (auto v = boost::any_cast<short>(&value)) {
            s << *v << std::endl;
        }
        else if (auto v = boost::any_cast<long>(&value)) {
            s << *v << std::endl;
        }
        else if (auto v = boost::any_cast<int>(&value)) {
            s << *v << std::endl;
        }
        else if (auto v = boost::any_cast<std::string>(&value)) {
            s << *v << std::endl;
        }
        else {
            s << "error" << std::endl;
        }
    }
    return s;
}

struct cache_config_data {
    short cache_size;
    std::string reader_file_name;
    std::string writer_file_name;
    std::string items_file_name;
    short stratergy;
    int cache_timeout;
    short run_test;

    cache_config_data() :
        cache_size{}, reader_file_name{}, writer_file_name{}, items_file_name{}, stratergy{},
        cache_timeout{}, run_test{}
    {}
};
using cache_config = config<cache_config_data>;

#endif /* CONFIG_HPP */

