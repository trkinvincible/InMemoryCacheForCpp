#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "cachemanager.h"

class Command
{
public:
    Command(){

        mMaxThreadAllowed = std::thread::hardware_concurrency();
    }
    virtual ~Command(){}
    virtual void execute()=0;
public:
    static unsigned int mMaxThreadAllowed;
    static std::atomic_int mcurrThreadsAlive;
};

#endif // COMMAND_H
