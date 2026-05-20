#pragma once

#ifdef DEBUG

#include "FrameWork.h"

#include <memory>

namespace OriGine {
	class SceneManager;
}

class LaviEditor : public FrameWork{
public:
	LaviEditor();
	~LaviEditor() override;

	void Initialize(const std::vector<std::string>& _commandLines) override;
	void Finalize() override;
	void Run() override;

private:
	std::unique_ptr<OriGine::SceneManager> sceneManager_ = nullptr;
};

#endif // DEBUG