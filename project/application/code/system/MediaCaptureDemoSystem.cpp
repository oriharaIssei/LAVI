#include "MediaCaptureDemoSystem.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "MicrophonePanel.h"
#include "WebCameraPanel.h"
#include "ScreenCapturePanel.h"
#include "VoiceVoxPanel.h"
#include "VisionPanel.h"
#include "LLMChatPanel.h"
#include "GatekeeperManager.h"
#include "GatekeeperPanel.h"
#include "MemoryPanel.h"
#include "SentenceEmbedding.h"
#include "KnowledgeBase.h"
#include "WebSearchClient.h"
#include "LocationProvider.h"
#include "WebAction.h"
#include "system/action/ActionPipeline.h"
#include "winApp/WinApp.h"

#include "Engine.h"

#include "imgui/imgui.h"

#include <filesystem>

MediaCaptureDemoSystem::MediaCaptureDemoSystem()
	: OriGine::ISystem(OriGine::SystemCategory::Render){}

MediaCaptureDemoSystem::~MediaCaptureDemoSystem() = default;

void MediaCaptureDemoSystem::Initialize(){
	ctx_ = std::make_unique<SharedMediaContext>();
	ctx_->config = LoadAppConfig();

	micPanel_ = std::make_unique<MicrophonePanel>();
	camPanel_ = std::make_unique<WebCameraPanel>();
	screenPanel_ = std::make_unique<ScreenCapturePanel>();
	voiceVoxPanel_ = std::make_unique<VoiceVoxPanel>();
	visionPanel_ = std::make_unique<VisionPanel>();
	llmPanel_ = std::make_unique<LLMChatPanel>();
	gkManager_ = std::make_unique<GatekeeperManager>();
	gkPanel_ = std::make_unique<GatekeeperPanel>();
	memoryPanel_ = std::make_unique<MemoryPanel>();

	micPanel_->Initialize(ctx_.get());
	camPanel_->Initialize(ctx_.get());
	screenPanel_->Initialize(ctx_.get());
	voiceVoxPanel_->Initialize(ctx_.get());
	visionPanel_->Initialize(ctx_.get());
	llmPanel_->Initialize(ctx_.get());
	gkManager_->Initialize(ctx_.get());
	gkPanel_->Initialize(ctx_.get(),gkManager_.get());
	memoryPanel_->Initialize(ctx_.get(),gkManager_.get());
	llmPanel_->SetMemoryPanel(memoryPanel_.get());
	gkManager_->SetLongTermMemory(memoryPanel_->GetLongTermMemory());
	gkManager_->SetLocalLLM(memoryPanel_->GetLocalLLM()); // 自動音声応答もローカル LLM で（共有）

	// 発話区間検出・ターン制御
	turnController_ = std::make_unique<TurnController>();
	turnController_->LoadConfig("application/resource/turn/turn_config.json");
	turnController_->SetOnUserSpeechStart([this]() {
		// 発話開始: このターン分の音声を取り直すためバッファをクリア
		micPanel_->ClearAudioBuffer();
	});
	turnController_->SetOnUserSpeechEnd([this]() {
		// 発話終了: 自動で転写を開始（結果は MicrophonePanel が ctx_->transcribedText に反映）
		micPanel_->RequestTranscribe();
	});
	turnController_->SetOnBargeIn([this]() {
		// LAVI 発話中にユーザーが割り込んだ → TTS を即停止してユーザーを優先
		if(ctx_->voiceVox){
			ctx_->voiceVox->Stop();
		}
		ctx_->isSpeaking = false;
		// （この直後に onUserSpeechStart が呼ばれ、音声バッファはクリアされる）
	});
	lastTurnTick_ = std::chrono::steady_clock::now();

	LoadTagAxes("application/resource/memory/tag_axes.json");

	actionPipeline_ = std::make_unique<ActionPipeline>();
	actionPipeline_->LoadConfig("application/resource/action/tools.json");
	llmPanel_->SetActionPipeline(actionPipeline_.get());
	gkManager_->SetActionPipeline(actionPipeline_.get());

	// Web 検索（時事対策）。時事キーワードは JSON から読み込む（無ければ既定値）。
	webSearch_ = std::make_unique<WebSearchClient>();
	webSearch_->LoadKeywords("application/resource/web/current_events.json");
	gkManager_->SetWebSearch(webSearch_.get());
	llmPanel_->SetWebSearch(webSearch_.get());

	// 現在地（Windows 位置情報→逆ジオコーディング）をバックグラウンドで取得開始。
	location_ = std::make_unique<LocationProvider>();
	location_->Start();
	llmPanel_->SetLocation(location_.get());

	// ホットキー登録 (Engine API)
	auto* winApp = OriGine::Engine::GetInstance()->GetWinApp();
	if(ctx_->hotkeyEnabled){
		hotkeyRegistered_ = winApp->RegisterGlobalHotkey(1,ctx_->hotkeyModifiers,ctx_->hotkeyVk);
	}

	// システムトレイ
	winApp->EnableSystemTray(L"LAVI");
	winApp->SetMinimizeToTrayOnClose(ctx_->config.minimizeToTrayOnClose);

	// Sentence Embedding (存在すれば読み込み)
	const std::filesystem::path embDir = "application/resource/embedding";
	const std::filesystem::path modelPath = embDir / "model.onnx";
	const std::filesystem::path vocabPath = embDir / "vocab.txt";
	if(std::filesystem::exists(modelPath) && std::filesystem::exists(vocabPath)){
		embedding_ = std::make_unique<SentenceEmbedding>();
		if(embedding_->LoadModel(modelPath.wstring(),vocabPath.string())){
			gkManager_->SetSentenceEmbedding(embedding_.get());
			llmPanel_->SetSentenceEmbedding(embedding_.get());

			// 知識ベース RAG: 埋め込み共有 + SQLite。起動時に knowledge/ フォルダを取り込む。
			knowledgeBase_ = std::make_unique<KnowledgeBase>();
			if(knowledgeBase_->Initialize("application/resource/knowledge/knowledge.db", embedding_.get())){
				knowledgeBase_->ImportFolder("application/resource/knowledge");
				gkManager_->SetKnowledgeBase(knowledgeBase_.get());
			} else{
				knowledgeBase_.reset();
			}
		} else{
			embedding_.reset();
		}
	}
}

void MediaCaptureDemoSystem::Finalize(){
	auto* winApp = OriGine::Engine::GetInstance()->GetWinApp();
	if(hotkeyRegistered_){
		winApp->UnregisterGlobalHotkey(1);
		hotkeyRegistered_ = false;
	}
	winApp->DisableSystemTray();

	memoryPanel_->Finalize();
	gkPanel_->Finalize();
	llmPanel_->Finalize();
	visionPanel_->Finalize();
	voiceVoxPanel_->Finalize();
	screenPanel_->Finalize();
	camPanel_->Finalize();
	micPanel_->Finalize();

	memoryPanel_.reset();
	gkPanel_.reset();
	gkManager_.reset();
	if(knowledgeBase_){ knowledgeBase_->Finalize(); knowledgeBase_.reset(); }
	webSearch_.reset();
	location_.reset();
	llmPanel_.reset();
	visionPanel_.reset();
	voiceVoxPanel_.reset();
	screenPanel_.reset();
	camPanel_.reset();
	micPanel_.reset();
	ctx_.reset();
}

void MediaCaptureDemoSystem::DrawTurnControlUI(){
	if(!turnController_) return;

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
		ImGui::Checkbox("Voice via Local LLM##turn", &gkManager_->config().useLocalLLM);
		ImGui::SameLine();
		ImGui::TextDisabled(gkManager_->config().useLocalLLM
			? "(未ロード時はクラウドへフォールバック)"
			: "(クラウド Claude を使用)");

		// 時事対策: 鮮度クエリ検出時に Web 検索結果を文脈注入（ローカルは自前fetch / クラウドはweb_search）
		ImGui::Checkbox("Web Context (時事対策)##turn", &gkManager_->config().useWebContext);
		ImGui::SameLine();
		ImGui::TextDisabled("「最新/今日/ニュース」等で自動的に Web 検索を文脈注入");

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
	if(ctx_->isSpeaking && ctx_->speakFuture.valid() &&
	   ctx_->speakFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready){
		ctx_->speakFuture.get();
		ctx_->isSpeaking = false;
	}

	// 発話区間検出・ターン制御の駆動（メインスレッドで毎フレーム）
	{
		auto now = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(now - lastTurnTick_).count();
		lastTurnTick_ = now;
		if(dt > 0.5f) dt = 0.5f; // 初回や停止後のスパイクをクランプ

		if(micPanel_->IsCapturing()){
			turnController_->Update(micPanel_->GetCurrentLevel(), dt, ctx_->isSpeaking);
		}

		// LAVI 発話状態(isSpeaking)のエッジでターン状態を遷移
		if(ctx_->isSpeaking && !prevSpeaking_) turnController_->NotifyResponseStarted();
		if(!ctx_->isSpeaking && prevSpeaking_) turnController_->NotifyResponseEnded();
		prevSpeaking_ = ctx_->isSpeaking;

		// 応答が来ない場合に Processing で固まらないようにする取りこぼし対策
		if(turnController_->State() == TurnState::Processing){
			if(!processingActive_){ processingActive_ = true; processingSince_ = now; }
			else if(!ctx_->isSpeaking && !gkManager_->LlmBusy() &&
					std::chrono::duration<float>(now - processingSince_).count() > 8.0f){
				// 応答が来ない場合のみ待機へ戻す（LLM 処理中は待つ）
				turnController_->Reset();
				processingActive_ = false;
			}
		} else {
			processingActive_ = false;
		}
	}

	// ウェイクワード検出
	if(ctx_->wakeWordEnabled && !ctx_->wakeWord.empty() &&
	   !ctx_->transcribedText.empty() && ctx_->transcribedText != lastWakeWordText_){
		std::string newPart;
		if(lastWakeWordText_.empty() || ctx_->transcribedText.size() <= lastWakeWordText_.size()){
			newPart = ctx_->transcribedText;
		} else{
			newPart = ctx_->transcribedText.substr(lastWakeWordText_.size());
		}
		lastWakeWordText_ = ctx_->transcribedText;

		if(newPart.find(ctx_->wakeWord) != std::string::npos){
			auto* wa = OriGine::Engine::GetInstance()->GetWinApp();
			if(wa->IsMinimizedToTray()){
				wa->RestoreFromTray();
			} else{
				::ShowWindow(wa->GetHwnd(),SW_RESTORE);
				::SetForegroundWindow(wa->GetHwnd());
			}
		}
	}

	// UI のタブ選択に関係なく毎フレーム実行する処理
	micPanel_->Update();   // 転写/校正/集約の完了取り込み（自動転写のため必須）

	// 発話区間検出で確定 → 転写完了したら、GateKeeper 経由で LLM へ自動送信
	if(turnController_->State() == TurnState::Processing &&
	   !ctx_->transcribedText.empty() &&
	   ctx_->transcribedText != lastTurnTranscript_ &&
	   !gkManager_->LlmBusy()){
		lastTurnTranscript_ = ctx_->transcribedText;
		gkManager_->RespondToSpeech();
	}

	gkManager_->Update();
	memoryPanel_->Update();

	ImGui::Begin("Media Capture Demo");

	DrawTurnControlUI();
	DrawKnowledgeBaseUI();

	if(ImGui::BeginTabBar("MediaTabs")){
		if(ImGui::BeginTabItem("Microphone")){
			micPanel_->Draw();
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
			memoryPanel_->Draw();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}
