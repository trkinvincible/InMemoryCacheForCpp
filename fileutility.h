#ifndef FILE_UTILITY_H
#define FILE_UTILITY_H

#include <string>
#include <ostream>
#include <atomic>
#include <shared_mutex>
#include <iomanip>
#include <type_traits>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/utility/string_view.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "config.h"

class FileUtility
{
public:
    explicit FileUtility(const std::string& p_FileName){

        /*
         * Create items file of fixed size of 10,000 as marked in excercise
         * and width of 10 digits only
        */
        std::ofstream itemsFile(p_FileName, std::ios::binary | std::ios_base::trunc | std::ios_base::out);
        int i = 1;
        do{
            itemsFile << std::left << std::setw(10) << " " << std::endl;
        }while(++i <= mMaxLineNumber);
        itemsFile.flush();
        itemsFile.close();
        try{

            /*
             * Entire file is mapped in virtual memory so threads can access independently
             * without staggering the disk read head
            */
            const boost::interprocess::file_mapping input_file_mapped(p_FileName.c_str(),boost::interprocess::read_write);
            boost::interprocess::mapped_region mapped_region(input_file_mapped, boost::interprocess::read_write);
            mMappedRegion.swap(mapped_region);
            char* start_address = reinterpret_cast<char*>(mMappedRegion.get_address());
            // light weight no memory allocation
            boost::string_view sv(start_address);
            mMappedRegionStringView.swap(sv);
        }catch(std::exception &exp){

            std::cout << exp.what() << std::endl;
            boost::interprocess::file_mapping::remove(p_FileName.c_str());
        }
    }

    FileUtility(const FileUtility& rhs) = default;

    int ReadFileAtIndex(const int p_Index)
    {
        /*
         * Multiple read must happen simultaneously unless some thread need to write
        */
        std::shared_lock lock(mItemFileGuard);
        int count = 0;
        int pos = 0;
        while(true){

            if (p_Index == 1) break;
            pos = mMappedRegionStringView.find_first_of('\n', pos+1);
            if (pos != boost::string_view::npos) count++;
            if (count == p_Index - 1) break;
        }
        if (mMappedRegionStringView[pos] == '\n') pos++;
        char* start_address = reinterpret_cast<char*>(mMappedRegion.get_address());
        std::string v(start_address, pos, 10);
        boost::algorithm::trim(v);
        return std::stoi(v);
    }

    void InsertDataAtIndex(const std::pair<int, std::string>& p_Data)
    {
        int line_number = p_Data.first;
        assert(line_number <= 50);
        auto value = p_Data.second;
        int count = 0;
        int pos = 0;

        /*
         * Multiple read must happen simultaneously unless some thread need to write
        */
        std::unique_lock lock(mItemFileGuard);
        while(true){

            if (line_number == 1) break;
            pos = mMappedRegionStringView.find_first_of('\n', pos+1);
            if (pos != boost::string_view::npos) count++;
            if (count == line_number - 1) break;
        }
        if (mMappedRegionStringView[pos] == '\n') pos++;
        std::locale loc;
        for (int i = pos, j=0; mMappedRegionStringView[i] != '\n'; i++, j++){

            char c = (std::isdigit(value[j],loc) || value[j] == '.') ? value[j] : ' ';
            const_cast<char&>(mMappedRegionStringView[i]) = c;
        }
        lock.unlock();
        mMappedRegion.flush(0,mMappedRegion.get_size(), true);
    }

private:
    const int mMaxLineNumber = 10000;
    std::shared_mutex mItemFileGuard;
    boost::string_view mMappedRegionStringView; // light weight no memory allocation
    boost::interprocess::mapped_region mMappedRegion;
};
#endif // FILE_UTILITY_H
