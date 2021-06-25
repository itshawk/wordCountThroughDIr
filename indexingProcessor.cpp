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

    main thread can simply iterate through checking every file extension and spawning threads/queueing threads as necessary

    worker threads need to share a slot in memory, so we needa have locks for that
    workers need to be able to add to the table every unique word encountered, probably want to use hashmap,
    with key of word and value of encounters
    IMPORTANT: Case insensitive and ignore punctuation

    finished product will output top 10 words and their counts

    show what each thread is doing (have outputs for each saying what file they are on)
*/

static bool is_valid_file(fs::directory_entry const &entry)
{
    return is_regular_file(entry) && !is_other(entry);
}

bool sortbysec(const std::pair<std::string, int> &a,
               const std::pair<std::string, int> &b)
{
    return (a.second > b.second);
}

void print_map(std::string_view comment, const std::map<std::string, int> &m)
{
    std::cout << comment;
    for (const auto &[key, value] : m)
    {
        std::cout << key << " = " << value << "; ";
    }
    std::cout << "\n";
}

bool cmp(std::pair<std::string, int> &a,
         std::pair<std::string, int> &b)
{
    return a.second > b.second;
}

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

        std::cout << it.first << ' '
                  << it.second << "\n";
        if (i++ == 10)
        {
            return;
        }
    }
}

// wordcount

struct WordCount
{
    std::mutex mut;
    std::map<std::string, int> words;
    std::vector<std::pair<std::string, int>> topTenWords;
};

// queue of files to deal with
struct WorkerQueue
{
    std::mutex mut;
    std::queue<fs::path> queue;
    int finished = 0;
    std::condition_variable_any cv;
};

// indicates when all the workers are finished
std::mutex workersRunningMut;
int workersRunning;

void workerThread(WorkerQueue &wQueue, WordCount &wordCount)
{
    std::string word;
    std::cout << std::this_thread::get_id() << " Thread Started\n";
    int filesHandled = 0;
    while (1)
    {
        // try lock until take
        if (wQueue.mut.try_lock())
        {
            //std::cout << "Took Lock on " << std::this_thread::get_id() << std::endl;
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
                //std::cout << "Unlocked Lock on " << std::this_thread::get_id() << std::endl;
                // try to take lock on the running workers and -- it so we have accurate count of still running workers
                while (1)
                {
                    if (workersRunningMut.try_lock())
                    {
                        workersRunning--;
                        std::cout << "Thread " << std::this_thread::get_id() << " Handled " << filesHandled << "\n"
                                  << "Is Queue Empty? " << wQueue.queue.empty() << "\n";

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
            filesHandled++;
            // iterate through file and grab words,
            std::ifstream stream(path.c_str(), std::ios::binary);
            while (stream >> word)
            {
                transform(word.begin(), word.end(), word.begin(), ::tolower);
                //fprintf(stderr, "word: %s found %d times.\n", word.c_str(), wordCount[word] + 1);
                std::regex rgx("\\W+");
                std::sregex_token_iterator iter(word.begin(), word.end(), rgx, -1);
                std::sregex_token_iterator end;
                wordCount.mut.lock();
                for (; iter != end; ++iter)
                {
                    if (iter->str().length() > 0)
                    {

                        wordCount.words[*iter]++;
                        // if (wordCount.topTenWords.size() < 10)
                        // {
                        //     int done = 0;
                        //     for (auto w : wordCount.topTenWords)
                        //     {
                        //         if (w.first.compare(*iter))
                        //         {
                        //             w.second++;
                        //             done = 1;
                        //         }
                        //     }
                        //     if (!done)
                        //     {
                        //         wordCount.topTenWords.push_back(std::pair<std::string, int>(*iter, ++wordCount.words[*iter]));
                        //         if (wordCount.topTenWords.size() == 10)
                        //         {
                        //             std::sort(wordCount.topTenWords.begin(), wordCount.topTenWords.end(), sortbysec);
                        //         }
                        //     }
                        //     else
                        //     {
                        //         std::sort(wordCount.topTenWords.begin(), wordCount.topTenWords.end(), sortbysec);
                        //     }
                        // }
                        // else
                        // {
                        //     if (++wordCount.words[*iter] > wordCount.topTenWords.back().second)
                        //     {
                        //         int done = 0;
                        //         for (auto w : wordCount.topTenWords)
                        //         {
                        //             if (w.first.compare(*iter))
                        //             {
                        //                 w.second++;
                        //                 done = 1;
                        //             }
                        //         }
                        //         if (!done)
                        //         {
                        //             wordCount.topTenWords.pop_back();
                        //             wordCount.topTenWords.push_back(std::pair<std::string, int>(*iter, wordCount.words[*iter]));
                        //             std::sort(wordCount.topTenWords.begin(), wordCount.topTenWords.end(), sortbysec);
                        //         }
                        //         else
                        //         {
                        //             std::sort(wordCount.topTenWords.begin(), wordCount.topTenWords.end(), sortbysec);
                        //         }
                        //     }
                        // }
                    }
                }
                wordCount.mut.unlock();
            }
        }
    }
}

// main thread to iterate through file system
int searcher(fs::path path, std::unordered_map<std::string, bool> exts, WorkerQueue &wQueue)
{
    for (const auto &file : fs::recursive_directory_iterator(path))
    {

        //std::cout << file.path() << "\n";
        if (is_valid_file(file))
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
    }
    wQueue.mut.lock();
    wQueue.finished = 1;
    wQueue.cv.notify_all();
    wQueue.mut.unlock();

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
    s.detach();

    // setup worker threads whatever is smaller max hardware or asked for threads, -1 for hardware because we need a main thread
    int numThreads = std::min((int)std::thread::hardware_concurrency() - 1, atoi(argv[2]));

    // setup thread pool and startup worker threads with maps to put info in
    std::vector<std::thread> threadPool;
    WordCount workerCounts;
    // counter for use  on 145, waiting for workers to finish
    workersRunning = numThreads;

    for (int i = 0; i < numThreads; i++)
    {
        threadPool.push_back(std::thread(workerThread, std::ref(wQueue), std::ref(workerCounts)));
    }
    for (std::thread &every_thread : threadPool)
    {
        every_thread.detach();
    }

    // wait for workers to finish up
    while (workersRunning)
    {
        fprintf(stderr, "Workers: %d Size of Map: %lld\n", workersRunning, workerCounts.words.size());
    }

    // handle top 10 in the actual hashmap
    // std::vector<std::pair<std::string, int>> sortWords;
    // for (auto elem : workerCounts.words)
    // {
    //     sortWords.push_back(std::pair<std::string, int>(elem.first, elem.second));
    //     //std::cout << "new thing in vector\n";
    // }
    // std::sort(sortWords.begin(), sortWords.end(), sortbysec);
    //int i = 0;
    // for (auto pair : workerCounts.words)
    // {
    //     std::cout << pair.first << " : " << pair.second << "\n";
    //     if (i++ == 10)
    //     {
    //         break;
    //     }
    // }
    //print_map("Results : ", workerCounts.words);
    sort(workerCounts.words);
    return 0;
}