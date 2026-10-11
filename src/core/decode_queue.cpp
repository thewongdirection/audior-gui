#include "decode_queue.h"

#include <utility>

namespace audiorgui {

bool DecodeQueue::Push(DecodeJob &&job, Lost *outLost)
{
    const int s = StreamIndex(job.stream);
    Waiting waiting{std::move(job), arrivals_++};
    if (!waiting.job.isFinal) {
        partial_[s] = std::move(waiting);               /* newer than the one waiting, if any */
        return false;
    }
    if (partial_[s] && partial_[s]->job.id <= waiting.job.id)
        partial_[s].reset();                            /* the settled phrase overtakes its drafts */
    finals_.push_back(std::move(waiting));
    if (finals_.size() <= kMaxFinals)
        return false;
    if (outLost) {
        const DecodeJob &gone = finals_.front().job;
        *outLost = Lost{gone.stream, gone.id, gone.startSample};
    }
    finals_.pop_front();
    ++dropped_;
    return true;
}

bool DecodeQueue::Pop(DecodeJob *job)
{
    if (!finals_.empty()) {
        *job = std::move(finals_.front().job);
        finals_.pop_front();
        return true;
    }
    int pick = -1;
    for (int s = 0; s < kStreamCount; ++s) {
        if (partial_[s] && (pick < 0 || partial_[s]->order < partial_[pick]->order))
            pick = s;
    }
    if (pick < 0)
        return false;
    *job = std::move(partial_[pick]->job);
    partial_[pick].reset();
    return true;
}

bool DecodeQueue::Empty() const
{
    return finals_.empty() && !partial_[0] && !partial_[1];
}

}  // namespace audiorgui
