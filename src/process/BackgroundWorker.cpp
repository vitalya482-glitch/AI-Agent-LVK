#include "process/BackgroundWorker.h"

namespace lvk::process {
BackgroundWorker::BackgroundWorker():thread_(&BackgroundWorker::run,this){}
BackgroundWorker::~BackgroundWorker(){{std::lock_guard lock(mutex_);stopping_=true;}wake_.notify_one();if(thread_.joinable())thread_.join();}
void BackgroundWorker::submit(std::function<void()> job){{std::lock_guard lock(mutex_);if(stopping_)return;jobs_.push(std::move(job));}wake_.notify_one();}
void BackgroundWorker::run(){for(;;){std::function<void()> job;{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||!jobs_.empty();});if(stopping_&&jobs_.empty())return;job=std::move(jobs_.front());jobs_.pop();}try{job();}catch(...){}}}
}
