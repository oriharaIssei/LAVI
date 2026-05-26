#include "ActionQueue.h"

void ActionQueue::Enqueue(ActionCommand cmd) {
    queue_.push_back(std::move(cmd));
}

bool ActionQueue::HasPending() const {
    return !queue_.empty();
}

ActionCommand ActionQueue::Dequeue() {
    ActionCommand cmd = std::move(queue_.front());
    queue_.pop_front();
    return cmd;
}

void ActionQueue::Clear() {
    queue_.clear();
}

size_t ActionQueue::Size() const {
    return queue_.size();
}
