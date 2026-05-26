#pragma once

#include "ActionTypes.h"

#include <deque>

class ActionQueue {
public:
    void Enqueue(ActionCommand cmd);
    bool HasPending() const;
    ActionCommand Dequeue();
    void Clear();
    size_t Size() const;

    // TODO: void EnqueueWait(int ms);
    // TODO: void EnqueueGuiClick(int x, int y);
    // TODO: void EnqueueOcrVerify(const std::string& expected);

private:
    std::deque<ActionCommand> queue_;
};
