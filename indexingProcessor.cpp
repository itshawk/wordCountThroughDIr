#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <windows.h>
#include <algorithm>
#include <unordered_set>
#include <map>
#include <mutex>
#include <queue>
#include <fstream>
#include <regex>
#include <locale>
#include <condition_variable>
#include <iterator>
namespace fs = std::filesystem;
/*
    Plan:
    start with cmd line input from for directory and worker thread num
    N + 1 total threads onethread for searching file path

    search thread can simply iterate through checking every file extension and spawning threads/queueing threads as necessary

    worker threads need to share a slot in memory, so we needa have locks for that
    workers need to be able to add to the table every unique word encountered, probably want to use hashmap, (ended up with ordered)
    with key of word and value of encounters
    IMPORTANT: Case insensitive and ignore punctuation

    finished product will output top 10 words and their counts

*/

// reverse comparison function for map sort
bool cmp(std::pair<std::string, int> &a,
         std::pair<std::string, int> &b)
{
    return a.second > b.second;
}

// sort map and print top 10
void sort(std::map<std::string, int> &M)
{

    // Declare vector of pairs
    std::vector<std::pair<std::string, int>> A;

    // Copy key-value pair from Map
    // to vector of pairs
    for (auto &it : M)
    {
        A.push_back(it);
    }

    // Sort using comparator function
    sort(A.begin(), A.end(), cmp);
    int i = 0;
    // Print the sorted value
    for (auto &it : A)
    {

        printf("%s : %d\n", it.first.c_str(), it.second);
        if (i++ == 10)
        {
            return;
        }
    }
}

// wordcount, map + mutex
struct WordCount
{
    std::mutex mut;
    std::map<std::string, int> words;
};

// queue of files to deal with, mutex, queue, finished signalling completed serach, conditional for serach to signal it has provided something
struct WorkerQueue
{
    std::mutex mut;
    std::queue<fs::path> queue;
    int finished = 0;
    std::condition_variable_any cv;
};

// indicates when all the workers are finished and mutex for watching it
std::mutex workersRunningMut;
int workersRunning;

void workerThread(WorkerQueue &wQueue, WordCount &wordCount)
{
    std::string word;
    while (1)
    {
        // try lock until take
        if (wQueue.mut.try_lock())
        {
            // if queue is empty we are done, cleanup and leave
            if (wQueue.queue.empty())
            {
                if (!wQueue.finished)
                {
                    wQueue.cv.wait(wQueue.mut);
                    wQueue.mut.unlock();
                    continue;
                }
                wQueue.mut.unlock();
                // try to take lock on the running workers and -- it so we have accurate count of still running workers
                // then return because we are finishing
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
            // unlock after we used the queue
            wQueue.mut.unlock();
            // iterate through file and grab words,
            std::ifstream stream(path.c_str(), std::ios::binary);
            while (stream >> word)
            {
                // transform to lower to ignore case
                transform(word.begin(), word.end(), word.begin(), ::tolower);
                // rgx iterator to split on anything that isnt a word character i.e. a-z, 0-9
                std::regex rgx("\\W+");
                std::sregex_token_iterator iter(word.begin(), word.end(), rgx, -1);
                std::sregex_token_iterator end;
                for (; iter != end; ++iter)
                {
                    // remove any empty strings (happens with some leftover after regex iteration)
                    if (iter->str().length() > 0)
                    {
                        wordCount.mut.lock();
                        wordCount.words[*iter]++;
                        wordCount.mut.unlock();
                    }
                }
            }
        }
    }
}

// main thread to iterate through file system
void searcher(fs::path path, std::unordered_map<std::string, bool> exts, WorkerQueue &wQueue)
{
    // fail gracefully when iteration/windows file system breaks
    try
    {
        // recursively iterate through directory and all subdirectories
        for (const auto &file : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied))
        {
            //checking for extension in valid list
            if (exts[file.path().extension().string()])
            {
                wQueue.mut.lock();
                wQueue.queue.emplace(file.path());
                wQueue.cv.notify_one();
                wQueue.mut.unlock();
            }
        }
        // take lock to update and notify worker threads to go until queue empty
        wQueue.mut.lock();
        wQueue.finished = 1;
        wQueue.cv.notify_all();
        wQueue.mut.unlock();

        return;
    }
    catch (fs::filesystem_error &e)
    {
        std::cerr << "Failed with: " << e.what() << "\n";
        exit(1);
    }
}
// expect three or more arguements while command line
// args in order are
// program path numWorkers <space seperated extensions>
int main(int argc, char **argv)
{
    std::unordered_map<std::string, bool> extensions;
    if (argc < 3)
    {
        printf("Expects three or more arguements only received %d\nUsage: indexingProcessor.exe <directoryPath> <workerthread count> <space seperated extensions>\n", argc - 1);
        return 1;
    }
    int givenThreads = strtol(argv[2], NULL, 10);
    if (givenThreads < 1)
    {
        printf("Please give a valid thread count over one!\nUsage: indexingProcessor.exe <directoryPath> <workerthread count> <space seperated extensions>\n");
        return 1;
    }
    for (int i = 3; i < argc; i++)
    {
        // populate map with extension and boolean possibly unordered set is better
        if (argv[i][0] == '.')
        {
            printf("Accepted Extension Added %s\n", argv[i]);
            extensions[argv[i]] = true;
        }
        else
        {
            printf("Please give a list of valid extensions!\nUsage: indexingProcessor.exe <directoryPath> <workerthread count> <space seperated extensions>\n");
            return 1;
        }
    }

    // setup file queue
    WorkerQueue wQueue;

    // start file searcher
    if (fs::exists(argv[1]))
    {
        std::thread s(searcher, argv[1], extensions, std::ref(wQueue));
        s.detach();
    }
    else
    {
        printf("Invalid file path provided: \nUsage: indexingProcessor <directoryPath> <workerthread count> <space seperated extensions>\n");
        return 1;
    }

    // setup worker threads whatever is smaller max hardware or asked for threads, -1 for hardware because we need a main thread
    int numThreads = min((int)std::thread::hardware_concurrency() - 1, givenThreads);

    // setup and startup worker threads
    WordCount workerCounts;
    // counter for use  on 244, waiting for workers to finish
    workersRunning = numThreads;

    for (int i = 0; i < numThreads; i++)
    {
        std::thread t(workerThread, std::ref(wQueue), std::ref(workerCounts));
        t.detach();
    }

    // wait for workers to finish up
    while (workersRunning)
    {
        //fprintf(stdout, "Workers: %d Size of Map: %lld\n", workersRunning, workerCounts.words.size());
    }
    // sort and print
    sort(workerCounts.words);
    return 0;
}