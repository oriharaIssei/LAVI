#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "WhisperTranscriber.h"
#include "TranscriptRefiner.h"
#include "CorrectionMemory.h"
#include "VoiceBandFilter.h"

#include "mediaCapture/Microphone.h"
struct SharedMediaContext;


class MicrophonePanel{
public:
	void Initialize(SharedMediaContext* ctx);
	void Finalize();
	void Update();   // 非同期処理(転写/校正/集約)の完了取り込み。タブ非表示でも毎フレーム呼ぶ
	void Draw();

	// ターン制御（TurnController）から駆動するための入口
	float GetCurrentLevel();        // 直近の発話帯域 RMS（VAD 用。バンドパス有効時は声帯域のみ）
	float GetBroadbandLevel();      // 直近の広帯域 RMS（表示・比較用）
	void SetVoiceBandEnabled(bool e) { voiceBandEnabled_ = e; }
	bool IsVoiceBandEnabled() const { return voiceBandEnabled_; }
	bool IsCapturing() const;       // マイクキャプチャ中か
	void ClearAudioBuffer();        // 発話開始時に蓄積をクリア
	void RequestTranscribe();       // 発話終了時に非同期転写を開始（モデル要ロード）

	// 起動時の自動開始用（MicrophoneSystem から呼ぶ）。DEBUG パネルのボタンと同等処理を公開。
	bool StartDefaultCapture(int deviceIndex = -1);                 // 既定/指定デバイスを開いて撮影開始
	bool SwitchDevice(int deviceIndex);                            // 撮影中でも指定デバイスへ切替（停止→開き直し）
	bool LoadWhisperModel(const std::string& modelPath,            // Whisper モデル + VAD パスをロード（同期）
	                      const std::string& vadModelPath);
	bool IsModelLoaded() const;                                    // Whisper モデルがロード済みか

private:
	SharedMediaContext* ctx_ = nullptr;

	std::unique_ptr<OriGine::Microphone> microphone_;
	std::vector<OriGine::MicrophoneDeviceInfo> micDevices_;
	int selectedMicDevice_ = 0;

	std::mutex audioMutex_;
	float currentAudioLevel_ = 0.0f;   // 広帯域 RMS（表示用）
	float voiceBandLevel_ = 0.0f;      // 声帯域バンドパス後の RMS（VAD 用）
	float peakAudioLevel_ = 0.0f;
	std::string recordFilePath_ = "recorded.wav";

	// 声帯域バンドパス（約 300-3400Hz）。雑音を落として小声と分離するため。
	VoiceBandFilter voiceBandFilter_;  // オーディオコールバックスレッド専用
	bool voiceBandEnabled_ = true;

	std::unique_ptr<WhisperTranscriber> transcriber_;
	std::string whisperModelPath_ = "application/resource/whisper/ggml-large-v3.bin";
	std::string vadModelPath_ = "application/resource/whisper/ggml-silero-v6.2.0.bin";
	WhisperResult detailedResult_;
	bool showDetailedResult_ = false;
	std::future<bool> transcribeFuture_;
	bool isTranscribing_ = false;

	// 固有名詞（人名・グループ名など）。1 行 1 語で編集し、initial_prompt に合成する
	std::string vocabularyPath_ = "application/resource/whisper/vocabulary.json";
	std::string vocabularyText_;
	void LoadVocabulary();          // JSON -> vocabularyText_ + transcriber へ適用
	void SaveVocabulary();          // vocabularyText_ -> JSON
	void ApplyVocabulary();         // vocabularyText_ -> transcriber へ適用

	// LLM による校正（転写直後に自動実行、トグルで OFF 可）
	std::unique_ptr<TranscriptRefiner> refiner_;
	bool refineEnabled_ = true;
	bool isRefining_ = false;
	std::string rawTranscript_;     // 校正前の原文（比較・再校正用）
	std::future<std::string> refineFuture_;
	void StartRefine(const std::string& rawText);

	// 自己進化（校正ログから固有名詞・誤りルールを学習し語彙/プロンプトへ反映）
	// ※ correctionMemory_ は consolidateFuture_ より先に宣言する（破棄順で参照を生かす）
	CorrectionMemory correctionMemory_;
	std::string correctionMemoryPath_ = "application/resource/whisper/correction_memory.json";
	bool learningEnabled_ = true;
	bool isConsolidating_ = false;
	std::future<bool> consolidateFuture_;
	void ApplyEffectiveVocabulary();   // ユーザー語彙 ∪ 学習済み名 + 学習済みルールを反映
};
