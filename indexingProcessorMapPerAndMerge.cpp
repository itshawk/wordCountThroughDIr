#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <windows.h>
#include <algorithm>
#include <unordered_set>
#include <mutex>
#include <queue>
#include <fstream>

namespace fs = std::filesystem;
/*
    Plan:
    start with cmd line input from for directory and worker thread num
    N + 1 total threads onethread for searching file path

    main thread can simply iterate through checking every file extension and spawning threads/queueing threads as necessary

    worker threads need to share a slot in memory, so we needa have locks for that
    workers need to be able to add to the table every unique word encountered, probably want to use hashmap,
    with key of word and value of encounters
    IMPORTANT: Case insensitive and ignore punctuation

    finished product will output top 10 words and their counts

    show what each thread is doing (have outputs for each saying what file they are on)
*/

bool sortbysec(const std::pair<std::string, int> &a,
               const std::pair<std::string, int> &b)
{
    return (a.second > b.second);
}

// queue of files to deal with
struct WorkerQueue
{
    std::mutex mut;
    std::queue<fs::path> queue;
};

// indicates when all the workers are finished
std::mutex workersRunningMut;
int workersRunning;

void workerThread(WorkerQueue &wQueue, std::unordered_map<std::string, int> &wordCount)
{
    std::string word;
    while (1)
    {
        // try lock until take
        if (wQueue.mut.try_lock())
        {
            //std::cout << "Took Lock on " << std::this_thread::get_id() << std::endl;
            // if queue is empty we are done, cleanup and leave
            if (wQueue.queue.empty())
            {
                wQueue.mut.unlock();
                //std::cout << "Unlocked Lock on " << std::this_thread::get_id() << std::endl;
                // try to take lock on the running workers and -- it so we have accurate count of still running workers
                while (1)
                {
                    if (workersRunningMut.try_lock())
                    {
                        workersRunning--;
                        workersRunningMut.unlock();
                        return;
                    }
                }
            }
            // grab front of queue path and pop it while we have lock
            fs::path path = wQueue.queue.front();
            wQueue.queue.pop();
            //std::cout << "Unlocked Lock on " << std::this_thread::get_id() << std::endl;
            // unlock after we used the queue
            wQueue.mut.unlock();

            // iterate through file and grab words,
            // TODO delimit by anything other than alphanumeric, atm its by whitespace, case insensitive
            std::ifstream stream(path.c_str(), std::ios::binary);
            while (stream >> word)
            {
                //fprintf(stderr, "word: %s found %d times.\n", word.c_str(), wordCount[word] + 1);
                wordCount[word]++;
            }
        }
    }
}

// main thread to iterate through file system
int searcher(fs::path path, std::unordered_map<std::string, bool> exts, WorkerQueue &wQueue)
{
    for (const auto &file : fs::recursive_directory_iterator(path))
    {
        //checking for extension in valid list
        if (exts[file.path().extension().string()])
        {
            printf("%ls is a valid file\n", file.path().c_str());
            // we don't need to lock here since no other threads will work with this until this finishes
            wQueue.queue.emplace(file.path());
        }
    }
    return 0;
}

// expect two arguements while command line
// args in order are
// program path numWorkers <space seperated extensions>
int main(int argc, char **argv)
{
    std::unordered_map<std::string, bool> extensions;
    if (argc < 3)
    {
        printf("Expects two arguements only received %d\nUsage: indexingProcessor <directoryPath> <workerthread count> <space seperated extensions>\n", argc - 1);
        return 1;
    }
    for (int i = 3; i < argc; i++)
    {
        // populate map with extension and boolean possibly unordered set is better
        printf("Accepted Extension Added %s\n", argv[i]);
        extensions[argv[i]] = true;
    }

    // setup file queue
    WorkerQueue wQueue;

    // start file searcher
    std::thread s(searcher, argv[1], extensions, std::ref(wQueue));
    s.join();

    // setup worker threads whatever is smaller max hardware or asked for threads, -1 for hardware because we need a main thread
    int numThreads = std::min((int)std::thread::hardware_concurrency() - 1, atoi(argv[2]));
    //printf("numThreads: %d, hardward thing: %d, argv[2]: %d\n", numThreads, (int)std::thread::hardware_concurrency(), atoi(argv[2]));

    // setup thread pool and startup worker threads with maps to put info in
    std::vector<std::thread> threadPool;
    std::vector<std::unordered_map<std::string, int>> workerThreadCounts(numThreads);
    // counter for use  on 145, waiting for workers to finish
    workersRunning = numThreads;

    for (int i = 0; i < numThreads; i++)
    {
        threadPool.push_back(std::thread(workerThread, std::ref(wQueue), std::ref(workerThreadCounts[i])));
    }
    for (std::thread &every_thread : threadPool)
    {
        every_thread.detach();
    }

    // wait for workers to finish up
    while (workersRunning)
    {
        //fprintf(stderr, "%d\n", workersRunning);
    }

    // merge maps into one and print it out for now
    // TODO: grab top ten
    std::unordered_map<std::string, int> retMap;
    for (std::unordered_map<std::string, int> map : workerThreadCounts)
    {
        //retMap.merge(map);
        for (auto elem : map)
        {
            if (retMap[elem.first])
            {
                retMap[elem.first] += elem.second;
            }
            else
            {
                retMap[elem.first] = elem.second;
            }
        }
        //std::cout << "\n\n";
    }
    std::vector<std::pair<std::string, int>> sortWords;
    for (auto elem : retMap)
    {
        sortWords.push_back(std::pair<std::string, int>(elem.first, elem.second));
        //std::cout << "new thing in vector\n";
    }
    std::sort(sortWords.begin(), sortWords.end(), sortbysec);
    for (int i = 0; i < std::min(10, (int)sortWords.size()); i++)
    {
        std::cout << sortWords.at(i).first << " : " << sortWords.at(i).second << "\n";
    }
    printf("10 or size of vector: %lld\n", sortWords.size());
    return 0;
}