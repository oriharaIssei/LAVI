#include "MediaCaptureDemoSystem.h"
#include "SharedMediaContext.h"
#include "LaviContext.h"
#include "AppConfig.h"
#include "MicrophonePanel.h"
#include "WebCameraPanel.h"
#include "ScreenCapturePanel.h"
#include "VoiceVoxPanel.h"
#include "VisionPanel.h"
#include "LLMChatPanel.h"
#include "GatekeeperManager.h"
#include "GatekeeperPanel.h"
#include "TurnController.h" // DrawTurnControlUI が TurnController/TurnConfig/TurnState を参照
#include "MemoryPanel.h"
#include "KnowledgeBase.h"
#include "WebAction.h"
#include "winApp/WinApp.h"

#include "Engine.h"

#include "imgui/imgui.h"

#include <filesystem>
#include <fstream>

MediaCaptureDemoSystem::MediaCaptureDemoSystem()
	: OriGine::ISystem(OriGine::SystemCategory::Render){}

MediaCaptureDemoSystem::~MediaCaptureDemoSystem() = default;

void MediaCaptureDemoSystem::Initialize(){
	ctx_ = &LaviContext::Get(); // 共有状態はシングルトンサービスで一元管理（非所有）
	// config ロード・タグ軸ロード・ホットキー登録・システムトレイ初期化は AppLifecycleSystem(Initialize) へ移譲（ECS 分解、Release 稼働化）。

	// MicrophonePanel の所有・駆動は MicrophoneSystem(Input) へ移譲（ECS 分解）。共有は LaviContext::Get().micPanel 経由。
	camPanel_ = std::make_unique<WebCameraPanel>();
	screenPanel_ = std::make_unique<ScreenCapturePanel>();
	voiceVoxPanel_ = std::make_unique<VoiceVoxPanel>();
	visionPanel_ = std::make_unique<VisionPanel>();
	llmPanel_ = std::make_unique<LLMChatPanel>();
	gkPanel_ = std::make_unique<GatekeeperPanel>();
	// MemoryPanel の所有・駆動は MemorySystem(Movement) へ移譲（ECS 分解）。共有は LaviContext::Get().memoryPanel 経由。

	camPanel_->Initialize(ctx_);
	screenPanel_->Initialize(ctx_);
	voiceVoxPanel_->Initialize(ctx_);
	visionPanel_->Initialize(ctx_);
	llmPanel_->Initialize(ctx_);
	// GatekeeperManager は GatekeeperSystem(Movement) が所有・駆動（ECS 分解）。
	// gkPanel/memoryPanel は使用時(Draw/Update)に LaviContext::Get().gkManager を遅延 pull する。
	gkPanel_->Initialize(ctx_);
	// MemoryPanel は MemorySystem が生成・公開する。System 初期化順は非決定のため、
	// llmPanel_->SetMemoryPanel は Initialize ではなく Update で毎フレーム ctx から渡す。

	// マイク入力・転写は MicrophoneSystem(Input) へ、発話区間検出・ターン制御は TurnSystem(StateTransition) へ移譲（ECS 分解）。
	// micPanel は MicrophoneSystem が LaviContext::Get().micPanel に公開し TurnSystem 等が参照する。

	// config/タグ軸ロード・ホットキー登録・システムトレイは AppLifecycleSystem(Initialize) へ移譲（ECS 分解）。
	// アクション実行(ActionPipeline) は ActionSystem が担当（ECS 分解）。消費側は LaviContext::Get().actionPipeline を参照する。
	// Web 検索は WebSearchSystem が担当（ECS 分解）。消費側は LaviContext::Get().webSearch を参照する。

	// 現在地は LocationSystem が担当（ECS 分解）。LLMChatPanel は LaviContext::Get().location を参照する。

	// 埋め込み(SentenceEmbedding)と知識ベース RAG(KnowledgeBase) は KnowledgeSystem へ移譲（ECS 分解）。
	// 消費側は LaviContext::Get().knowledgeBase / .embedding を参照する。
}

void MediaCaptureDemoSystem::Finalize(){
	// ホットキー解除・システムトレイ終了は AppLifecycleSystem(Finalize) へ移譲（ECS 分解）。

	// micPanel は MicrophoneSystem、memoryPanel は MemorySystem が所有・解放する（ECS 分解）。
	gkPanel_->Finalize();
	llmPanel_->Finalize();
	visionPanel_->Finalize();
	voiceVoxPanel_->Finalize();
	screenPanel_->Finalize();
	camPanel_->Finalize();

	gkPanel_.reset();
	llmPanel_.reset();
	visionPanel_.reset();
	voiceVoxPanel_.reset();
	screenPanel_.reset();
	camPanel_.reset();
	ctx_ = nullptr; // シングルトン（LaviContext）は所有しないので破棄しない
}

void MediaCaptureDemoSystem::DrawTurnControlUI(){
	// 所有は TurnSystem/GatekeeperSystem。UI は LaviContext から共有インスタンスを引いて表示する（ECS 分解）。
	TurnController* turnController_ = LaviContext::Get().turnController;
	if(!turnController_) return;
	GatekeeperManager* gkManager_ = LaviContext::Get().gkManager; // null の可能性あり（下で個別ガード）

	if(ImGui::CollapsingHeader("Turn Control (発話区間検出)")){
		TurnConfig& cfg = turnController_->Config();

		static const char* kStateName[] = {"Idle", "UserSpeaking", "Processing", "LaviSpeaking"};
		int st = static_cast<int>(turnController_->State());
		ImGui::Text("State: %s", kStateName[st]);
		ImGui::SameLine();
		float rms = turnController_->CurrentRms();
		ImGui::Text("  RMS: %.4f", rms);
		// 実効開始しきい値に対する現在レベルのバー（適応時は動的に変化する）
		float effStart = turnController_->EffectiveStartThreshold();
		float disp = effStart > 0.0f ? (rms / (effStart * 2.0f)) : 0.0f;
		ImGui::ProgressBar(disp > 1.0f ? 1.0f : disp, ImVec2(-1, 0), "");
		ImGui::Text("NoiseFloor: %.4f  EffStart: %.4f  EffEnd: %.4f",
			turnController_->NoiseFloor(), effStart, turnController_->EffectiveEndThreshold());

		ImGui::Checkbox("Enabled##turn", &cfg.enabled);
		ImGui::SameLine();
		ImGui::Checkbox("Barge-in##turn", &cfg.bargeInEnabled);

		// 自動音声応答の LLM バックエンド（GateKeeper 経由）
		if(gkManager_){
			ImGui::Checkbox("Voice via Local LLM##turn", &gkManager_->config().useLocalLLM);
			ImGui::SameLine();
			ImGui::TextDisabled(gkManager_->config().useLocalLLM
				? "(未ロード時はクラウドへフォールバック)"
				: "(クラウド Claude を使用)");

			// 時事対策: 鮮度クエリ検出時に Web 検索結果を文脈注入（ローカルは自前fetch / クラウドはweb_search）
			ImGui::Checkbox("Web Context (時事対策)##turn", &gkManager_->config().useWebContext);
			ImGui::SameLine();
			ImGui::TextDisabled("「最新/今日/ニュース」等で自動的に Web 検索を文脈注入");
		}

		// 閉じるボタンの挙動: ON=トレイに常駐 / OFF=×で通常終了。即時反映＋保存。
		if(ImGui::Checkbox("閉じるボタンでトレイに最小化##tray", &ctx_->config.minimizeToTrayOnClose)){
			if(auto* wa = OriGine::Engine::GetInstance()->GetWinApp()){
				wa->SetMinimizeToTrayOnClose(ctx_->config.minimizeToTrayOnClose);
			}
			SaveAppConfig(ctx_->config);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("OFFにすると×ボタンで通常終了");

		ImGui::SliderFloat("Start Thld##turn", &cfg.startThreshold, 0.001f, 0.2f, "%.4f");
		ImGui::SliderFloat("End Thld##turn", &cfg.endThreshold, 0.001f, 0.2f, "%.4f");
		ImGui::SliderFloat("Min Speech (ms)##turn", &cfg.minSpeechMs, 0.0f, 1000.0f, "%.0f");
		ImGui::SliderFloat("Silence Hangover (ms)##turn", &cfg.silenceHangoverMs, 100.0f, 3000.0f, "%.0f");
		ImGui::SliderFloat("Barge-in (ms)##turn", &cfg.bargeInMs, 50.0f, 1500.0f, "%.0f");

		ImGui::Separator();
		ImGui::Checkbox("Adaptive Noise Floor##turn", &cfg.adaptiveNoise);
		ImGui::TextDisabled("環境ノイズを推定し開始/終了しきい値を動的化（Start/End Thld は下限として作用）");
		if(cfg.adaptiveNoise){
			ImGui::SliderFloat("Noise Start x##turn", &cfg.noiseStartMult, 1.5f, 8.0f, "%.2f");
			ImGui::SliderFloat("Noise End x##turn", &cfg.noiseEndMult, 1.2f, 6.0f, "%.2f");
			ImGui::SliderFloat("Adapt Up (ms)##turn", &cfg.noiseAdaptUpMs, 500.0f, 8000.0f, "%.0f");
			ImGui::SliderFloat("Adapt Down (ms)##turn", &cfg.noiseAdaptDownMs, 100.0f, 3000.0f, "%.0f");
		}

		if(ImGui::Button("Save Turn Config")){
			turnController_->SaveConfig();
		}
		ImGui::TextDisabled("発話を検知すると自動で転写します。マイクのキャプチャを開始してください。");
	}
}

void MediaCaptureDemoSystem::DrawKnowledgeBaseUI(){
	if(!ImGui::CollapsingHeader("Knowledge Base (知識ベース RAG)")) return;

	// 所有は KnowledgeSystem。UI は LaviContext から共有インスタンスを引いて表示する（ECS 分解）。
	KnowledgeBase* knowledgeBase_ = LaviContext::Get().knowledgeBase;
	if(!knowledgeBase_ || !knowledgeBase_->IsReady()){
		ImGui::TextDisabled("埋め込みモデル未ロードのため無効です（application/resource/embedding）。");
		return;
	}

	// std::string を ImGui に渡すためのリサイズコールバック（このプロジェクトの既存パターン）
	auto resizeCb = [](ImGuiInputTextCallbackData* data) -> int {
		if(data->EventFlag == ImGuiInputTextFlags_CallbackResize){
			auto* str = static_cast<std::string*>(data->UserData);
			str->resize(data->BufTextLen);
			data->Buf = str->data();
		}
		return 0;
	};
	if(kbSourceBuf_.capacity() < 128) kbSourceBuf_.reserve(128);
	if(kbTextBuf_.capacity() < 1024) kbTextBuf_.reserve(1024);

	ImGui::Text("登録チャンク数: %d", knowledgeBase_->ChunkCount());

	std::vector<std::string> sources = knowledgeBase_->ListSources();
	if(!sources.empty() && ImGui::TreeNode("出典一覧##kb")){
		for(const auto& s : sources){
			ImGui::BulletText("%s", s.c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton((std::string("削除##kb_") + s).c_str())){
				knowledgeBase_->RemoveSource(s);
				kbStatus_ = "削除: " + s;
			}
		}
		ImGui::TreePop();
	}

	ImGui::Separator();
	ImGui::InputText("出典名##kb", kbSourceBuf_.data(), kbSourceBuf_.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize, resizeCb, &kbSourceBuf_);
	ImGui::InputTextMultiline("本文##kb", kbTextBuf_.data(), kbTextBuf_.capacity() + 1,
		ImVec2(-1, 100), ImGuiInputTextFlags_CallbackResize, resizeCb, &kbTextBuf_);

	if(ImGui::Button("追加 / 更新##kb")){
		std::string src = kbSourceBuf_.empty() ? std::string("manual") : kbSourceBuf_;
		int added = knowledgeBase_->AddDocument(src, kbTextBuf_);
		kbStatus_ = "追加 " + std::to_string(added) + " チャンク (" + src + ")";
		kbTextBuf_.clear();
	}
	ImGui::SameLine();
	if(ImGui::Button("フォルダ再取込##kb")){
		int files = knowledgeBase_->ImportFolder("application/resource/knowledge");
		kbStatus_ = "取込ファイル数: " + std::to_string(files);
	}
	ImGui::SameLine();
	if(ImGui::Button("全削除##kb")){
		knowledgeBase_->Clear();
		kbStatus_ = "全削除しました";
	}

	if(!kbStatus_.empty()) ImGui::TextDisabled("%s", kbStatus_.c_str());
	ImGui::TextDisabled("knowledge/ に .txt/.md を置くと起動時に取り込みます。質問に関連するチャンクを自動で文脈注入します。");
}

void MediaCaptureDemoSystem::Update(){
	// TTS 完了処理・発話区間検出・ターン制御・発話→自動送信は TurnSystem(StateTransition) へ移譲（ECS 分解）。
	// ウェイクワード検出は WakeWordSystem(Input) へ移譲。共有は LaviContext 経由。

	// マイク転写(MicrophoneSystem) / 記憶(MemorySystem) の機能 Update は各 System(常時稼働) へ移譲（ECS 分解）。
	// 当 System は DEBUG 限定の調整 UI 描画のみを担う。共有インスタンスは LaviContext から pull する。
	MicrophonePanel* micPanel_  = LaviContext::Get().micPanel;
	MemoryPanel* memoryPanel_   = LaviContext::Get().memoryPanel;
	llmPanel_->SetMemoryPanel(memoryPanel_); // System 初期化順非依存に毎フレーム最新を渡す

	// GatekeeperManager の駆動は GatekeeperSystem(Movement) へ移譲（ECS 分解）。
	gkPanel_->SyncOnce(); // GK 既定パラメータの初回反映（Gatekeeper タブ非選択でも確実に反映）

	ImGui::Begin("Media Capture Demo");

	DrawTurnControlUI();
	DrawKnowledgeBaseUI();

	if(ImGui::BeginTabBar("MediaTabs")){
		if(ImGui::BeginTabItem("Microphone")){
			if(micPanel_) micPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("WebCamera")){
			camPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("ScreenCapture")){
			screenPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("VoiceVox")){
			voiceVoxPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Vision")){
			visionPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("LLM")){
			llmPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Gatekeeper")){
			gkPanel_->Draw();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Memory")){
			if(memoryPanel_) memoryPanel_->Draw();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}
